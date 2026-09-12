#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "ServerConfig.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "CgiHandler.hpp"
#include <csignal>
#include <vector>
#include <map>
#include <poll.h>

struct ClientConnection {
    std::vector<ServerConfig> configPool;
    ServerConfig activeConfig;
    HttpRequest request;
    std::string response;
    size_t responseOffset;
    bool closeAfterSend;
    bool awaitingCgi;
    CgiProcess cgi;
    
    ClientConnection() : responseOffset(0), closeAfterSend(false), awaitingCgi(false) {}
    ClientConnection(const std::vector<ServerConfig> &pool)
        : configPool(pool), activeConfig(pool[0]), responseOffset(0), closeAfterSend(false), awaitingCgi(false) {}
};

class WebServer {
private:
    std::vector<ServerConfig> _configs;
    std::vector<struct pollfd> _fds;
    std::map<int, std::vector<ServerConfig> > _socketConfigMap;
    std::map<int, ClientConnection> _clients;
    std::map<int, int> _cgiInputOwners;
    std::map<int, int> _cgiOutputOwners;
    static volatile sig_atomic_t _shutdownRequested;

    void setupServers();
    void runEventLoop();
    void acceptConnection(int listenFd);
    void handleClientRead(int clientFd);
    void handleClientWrite(int clientFd);
    void closeConnection(int clientFd);
    void queueResponse(int clientFd, HttpResponse response, bool closeAfterSend);
    void updateClientEvents(int clientFd);
    void removePollFd(int fd);
    void startCgiRequest(int clientFd, const HttpRequest &request, const std::string &scriptPath, const std::string &interpreterPath);
    void closeCgiInput(ClientConnection &conn, int fd);
    void handleCgiWrite(int fd);
    void handleCgiRead(int fd);
    void finalizeCgiResponse(int clientFd);
    void cleanupCgi(ClientConnection &conn);
    void reapFinishedCgi();

    const ServerConfig& selectConfig(const ClientConnection &conn, const std::string &hostHeader);

public:
    WebServer(const std::vector<ServerConfig> &configs);
    ~WebServer();

    void run();
    static void handleSignal(int signal);
    static bool isShutdownRequested();
};

#endif
