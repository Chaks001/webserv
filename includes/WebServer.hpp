#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "ServerConfig.hpp"
#include "HttpRequest.hpp"
#include "CgiHandler.hpp"
#include <csignal>
#include <vector>
#include <map>
#include <poll.h>

// Client connection state
struct ClientConnection {
    std::vector<ServerConfig> configPool; // All configs associated with this port
    ServerConfig activeConfig;            // Currently selected config based on Host header
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
    std::map<int, std::vector<ServerConfig> > _socketConfigMap; // listening fd -> list of configs
    std::map<int, ClientConnection> _clients;                  // client fd -> connection state
    std::map<int, int> _cgiInputOwners;                        // pipe fd -> client fd
    std::map<int, int> _cgiOutputOwners;                       // pipe fd -> client fd
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

    // Helper to select the right config based on Host header
    const ServerConfig& selectConfig(const ClientConnection &conn, const std::string &hostHeader);

public:
    WebServer(const std::vector<ServerConfig> &configs);
    ~WebServer();

    void run();
    static void handleSignal(int signal);
    static bool isShutdownRequested();
};

#endif
