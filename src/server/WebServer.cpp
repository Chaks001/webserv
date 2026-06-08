#include "WebServer.hpp"
#include "HttpResponse.hpp"
#include "Router.hpp"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <cctype>

namespace {
    const time_t kCgiTimeoutSeconds = 30;

    std::string toLower(std::string value) {
        for (size_t i = 0; i < value.size(); ++i) {
            value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
        }
        return value;
    }

    bool interrupted() {
        return errno == EINTR;
    }

    bool parseIpv4Address(const std::string &host, unsigned long &address) {
        unsigned long octets[4] = {0, 0, 0, 0};
        size_t start = 0;

        for (size_t i = 0; i < 4; ++i) {
            size_t end = host.find('.', start);
            if ((i < 3 && end == std::string::npos)
                || (i == 3 && end != std::string::npos)) {
                return false;
            }

            std::string part = host.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (part.empty()) {
                return false;
            }

            for (size_t j = 0; j < part.size(); ++j) {
                if (!std::isdigit(static_cast<unsigned char>(part[j]))) {
                    return false;
                }
                octets[i] = octets[i] * 10 + static_cast<unsigned long>(part[j] - '0');
                if (octets[i] > 255) {
                    return false;
                }
            }

            start = end + 1;
        }

        address = htonl((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]);
        return true;
    }

    void closeSocketFd(int fd) {
        close(fd);
    }

    bool shouldCloseConnection(const HttpRequest &request) {
        std::string connection = toLower(request.getHeader("Connection"));
        if (request.getVersion() == "HTTP/1.0") {
            return connection.find("keep-alive") == std::string::npos;
        }
        return connection.find("close") != std::string::npos;
    }

    bool locationMatches(const std::string &uri, const std::string &path) {
        if (path == "/") {
            return true;
        }
        if (uri == path) {
            return true;
        }
        if (path.empty()) {
            return false;
        }
        if (uri.size() <= path.size() || uri.compare(0, path.size(), path) != 0) {
            return false;
        }
        return path[path.size() - 1] == '/' || uri[path.size()] == '/';
    }

    const LocationConfig *matchLocation(const ServerConfig &config, const std::string &uri) {
        const LocationConfig *bestMatch = NULL;
        size_t bestLen = 0;
        for (size_t i = 0; i < config.locations.size(); ++i) {
            const std::string &path = config.locations[i].path;
            if (locationMatches(uri, path) && path.size() >= bestLen) {
                bestMatch = &config.locations[i];
                bestLen = path.size();
            }
        }
        return bestMatch;
    }

    unsigned long effectiveClientMaxBodySize(const ServerConfig &config, const std::string &path) {
        const LocationConfig *loc = matchLocation(config, path);
        if (loc && loc->has_client_max_body_size) {
            return loc->client_max_body_size;
        }
        return config.client_max_body_size;
    }

    bool isBodyTooLarge(const HttpRequest &request, unsigned long maxBodySize) {
        if (maxBodySize == static_cast<unsigned long>(-1)) {
            return false;
        }

        std::string contentLength = request.getHeader("Content-Length");
        if (!contentLength.empty()) {
            unsigned long length = std::strtoul(contentLength.c_str(), NULL, 10);
            if (length > maxBodySize) {
                return true;
            }
        }

        return request.getBody().size() > maxBodySize;
    }
}

volatile sig_atomic_t WebServer::_shutdownRequested = 0;

WebServer::WebServer(const std::vector<ServerConfig> &configs) : _configs(configs) {
    setupServers();
}

WebServer::~WebServer() {
    for (std::map<int, ClientConnection>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        cleanupCgi(it->second);
    }

    for (std::map<int, ClientConnection>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        closeSocketFd(it->first);
    }
    for (std::map<int, std::vector<ServerConfig> >::iterator it = _socketConfigMap.begin(); it != _socketConfigMap.end(); ++it) {
        closeSocketFd(it->first);
    }
}

void WebServer::run() {
    std::cout << "Server running..." << std::endl;
    runEventLoop();
}

void WebServer::handleSignal(int signal) {
    if (signal == SIGINT) {
        _shutdownRequested = 1;
    }
}

bool WebServer::isShutdownRequested() {
    return _shutdownRequested != 0;
}

void WebServer::setupServers() {
    // Group configs by host:port
    std::map<std::pair<std::string, int>, std::vector<ServerConfig> > grouped;
    for (size_t i = 0; i < _configs.size(); ++i) {
        std::string host = _configs[i].host.empty() ? "0.0.0.0" : _configs[i].host;
        grouped[std::make_pair(host, _configs[i].port)].push_back(_configs[i]);
    }

    for (std::map<std::pair<std::string, int>, std::vector<ServerConfig> >::iterator it = grouped.begin(); it != grouped.end(); ++it) {
        std::string host = it->first.first;
        int port = it->first.second;

        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Failed to create socket");
        }

        int opt = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
            throw std::runtime_error("setsockopt failed");
        }

        if (fcntl(sockfd, F_SETFL, O_NONBLOCK) < 0) {
            throw std::runtime_error("fcntl failed");
        }

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        unsigned long parsedAddress = 0;
        if (parseIpv4Address(host, parsedAddress)) {
            addr.sin_addr.s_addr = parsedAddress;
        } else {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        addr.sin_port = htons(port);

        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            int savedErrno = errno;
            close(sockfd);
            std::ostringstream oss;
            oss << "Failed to bind to " << host << ":" << port << " (" << strerror(savedErrno) << ")";
            throw std::runtime_error(oss.str());
        }

        if (listen(sockfd, 128) < 0) {
            close(sockfd);
            throw std::runtime_error("listen failed");
        }

        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _fds.push_back(pfd);

        _socketConfigMap[sockfd] = it->second;
        std::cout << "Listening on " << host << ":" << port << " (" << it->second.size() << " virtual hosts)" << std::endl;
    }
}

void WebServer::runEventLoop() {
    while (!isShutdownRequested()) {
        if (_fds.empty()) {
            break;
        }

        int ret = poll(&_fds[0], _fds.size(), 1000);
        if (ret < 0) {
            if (interrupted()) {
                continue;
            }
            std::cerr << "poll error" << std::endl;
            break;
        }

        std::vector<struct pollfd> readyFds = _fds;
        for (size_t i = 0; i < readyFds.size(); ++i) {
            if (readyFds[i].revents == 0) {
                continue;
            }

            int fd = readyFds[i].fd;

            if (_socketConfigMap.count(fd)) {
                if (readyFds[i].revents & POLLIN) {
                    acceptConnection(fd);
                }
                continue;
            }

            if (_clients.count(fd)) {
                if (readyFds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    closeConnection(fd);
                    continue;
                }
                if ((readyFds[i].revents & POLLIN) && _clients.count(fd)) {
                    handleClientRead(fd);
                }
                if ((readyFds[i].revents & POLLOUT) && _clients.count(fd)) {
                    handleClientWrite(fd);
                }
                continue;
            }

            if (_cgiInputOwners.count(fd)) {
                if (readyFds[i].revents & (POLLOUT | POLLHUP | POLLERR | POLLNVAL)) {
                    handleCgiWrite(fd);
                }
                continue;
            }

            if (_cgiOutputOwners.count(fd)) {
                if (readyFds[i].revents & POLLNVAL) {
                    int clientFd = _cgiOutputOwners[fd];
                    closeConnection(clientFd);
                    continue;
                }
                if (readyFds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                    handleCgiRead(fd);
                }
            }
        }

        reapFinishedCgi();
    }
}

void WebServer::acceptConnection(int listenFd) {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientLen);

    if (clientFd < 0) {
        std::cerr << "accept failed" << std::endl;
        return;
    }

    if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
        std::cerr << "fcntl failed" << std::endl;
        close(clientFd);
        return;
    }

    struct pollfd pfd;
    pfd.fd = clientFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _fds.push_back(pfd);

    _clients[clientFd] = ClientConnection(_socketConfigMap[listenFd]);
    std::cout << "New connection accepted: " << clientFd << std::endl;
}

const ServerConfig& WebServer::selectConfig(const ClientConnection &conn, const std::string &hostHeader) {
    if (hostHeader.empty()) {
        return conn.configPool[0];
    }

    // Extract hostname from header (remove port if present)
    std::string host = hostHeader;
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }

    for (size_t i = 0; i < conn.configPool.size(); ++i) {
        if (toLower(conn.configPool[i].server_name) == toLower(host)) {
            return conn.configPool[i];
        }
    }
    return conn.configPool[0]; // Default to first config
}

void WebServer::handleClientRead(int clientFd) {
    char buffer[8192];
    ssize_t bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);

    if (bytesRead <= 0) {
        closeConnection(clientFd);
        return;
    }

    ClientConnection &conn = _clients[clientFd];
    
    conn.request.parse(buffer, static_cast<size_t>(bytesRead),
        static_cast<size_t>(effectiveClientMaxBodySize(conn.configPool[0], conn.request.getPath())));

    if (conn.request.hasError()) {
        Router router(conn.activeConfig);
        queueResponse(
            clientFd,
            router.makeErrorResponse(
                conn.request.getErrorCode(),
                conn.request.getErrorReason(),
                conn.request.getErrorBody()),
            true);
        return;
    }

    if (!conn.request.isComplete()) {
        return;
    }

    // Now that headers are complete, select the final config based on Host header
    std::string hostHeader = conn.request.getHeader("Host");
    
    // HTTP/1.1 requires Host header
    if (conn.request.getVersion() == "HTTP/1.1" && hostHeader.empty()) {
        Router router(conn.activeConfig);
        queueResponse(clientFd, router.makeErrorResponse(400, "Bad Request", "Host header is required for HTTP/1.1"), true);
        return;
    }

    conn.activeConfig = selectConfig(conn, hostHeader);

    std::cout << "Method: " << conn.request.getMethod() << ", Host: " << hostHeader 
              << ", URI: " << conn.request.getUri() << std::endl;

    Router router(conn.activeConfig);
    unsigned long maxBodySize = effectiveClientMaxBodySize(conn.activeConfig, conn.request.getPath());
    if (isBodyTooLarge(conn.request, maxBodySize)) {
        queueResponse(clientFd, router.makeErrorResponse(413, "Payload Too Large", "413 Payload Too Large"), true);
        return;
    }

    RouteResult result = router.resolveRequest(conn.request);
    if (result.isCgi) {
        startCgiRequest(clientFd, conn.request, result.cgiScriptPath, result.cgiInterpreterPath);
        return;
    }

    queueResponse(clientFd, result.response, shouldCloseConnection(conn.request));
}

void WebServer::handleClientWrite(int clientFd) {
    ClientConnection &conn = _clients[clientFd];
    if (conn.response.empty()) {
        updateClientEvents(clientFd);
        return;
    }

    size_t remaining = conn.response.size() - conn.responseOffset;
    ssize_t bytesSent = send(
        clientFd,
        conn.response.c_str() + conn.responseOffset,
        remaining,
        0);

    if (bytesSent <= 0) {
        closeConnection(clientFd);
        return;
    }

    conn.responseOffset += static_cast<size_t>(bytesSent);
    if (conn.responseOffset < conn.response.size()) {
        updateClientEvents(clientFd);
        return;
    }

    conn.response.clear();
    conn.responseOffset = 0;

    if (conn.closeAfterSend) {
        closeConnection(clientFd);
        return;
    }

    conn.request = HttpRequest();
    updateClientEvents(clientFd);
}

void WebServer::closeConnection(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it != _clients.end()) {
        cleanupCgi(it->second);
        removePollFd(clientFd);
        closeSocketFd(clientFd);
        _clients.erase(it);
        std::cout << "Connection closed: " << clientFd << std::endl;
    }
}

void WebServer::queueResponse(int clientFd, HttpResponse response, bool closeAfterSend) {
    ClientConnection &conn = _clients[clientFd];
    conn.closeAfterSend = closeAfterSend;
    conn.awaitingCgi = false;
    response.setHeader("Connection", closeAfterSend ? "close" : "keep-alive");
    
    conn.response = response.toString(conn.request.getMethod() != "HEAD");
    conn.responseOffset = 0;
    updateClientEvents(clientFd);
}

void WebServer::updateClientEvents(int clientFd) {
    std::map<int, ClientConnection>::iterator clientIt = _clients.find(clientFd);
    if (clientIt == _clients.end()) {
        return;
    }

    short events = POLLIN;
    if (!clientIt->second.response.empty()) {
        events = POLLOUT;
    } else if (clientIt->second.awaitingCgi) {
        events = 0;
    }

    for (size_t i = 0; i < _fds.size(); ++i) {
        if (_fds[i].fd == clientFd) {
            _fds[i].events = events;
            _fds[i].revents = 0;
            return;
        }
    }
}

void WebServer::removePollFd(int fd) {
    for (size_t i = 0; i < _fds.size(); ++i) {
        if (_fds[i].fd == fd) {
            _fds.erase(_fds.begin() + i);
            return;
        }
    }
}

void WebServer::startCgiRequest(int clientFd, const HttpRequest &request, const std::string &scriptPath, const std::string &interpreterPath) {
    ClientConnection &conn = _clients[clientFd];
    CgiHandler handler(scriptPath, interpreterPath);
    std::string errorMessage;

    if (!handler.launch(request, conn.activeConfig, conn.cgi, errorMessage)) {
        Router router(conn.activeConfig);
        queueResponse(clientFd, router.makeErrorResponse(500, "Internal Server Error", errorMessage), true);
        return;
    }

    conn.awaitingCgi = true;
    updateClientEvents(clientFd);

    if (conn.cgi.inputFd >= 0) {
        struct pollfd inputPollFd;
        inputPollFd.fd = conn.cgi.inputFd;
        inputPollFd.events = request.getBody().empty() ? 0 : POLLOUT;
        inputPollFd.revents = 0;
        _fds.push_back(inputPollFd);
        _cgiInputOwners[conn.cgi.inputFd] = clientFd;

        if (request.getBody().empty()) {
            closeCgiInput(conn, conn.cgi.inputFd);
        }
    }

    if (conn.cgi.outputFd >= 0) {
        struct pollfd outputPollFd;
        outputPollFd.fd = conn.cgi.outputFd;
        outputPollFd.events = POLLIN;
        outputPollFd.revents = 0;
        _fds.push_back(outputPollFd);
        _cgiOutputOwners[conn.cgi.outputFd] = clientFd;
    }
}

void WebServer::closeCgiInput(ClientConnection &conn, int fd) {
    if (fd >= 0) {
        close(fd);
        removePollFd(fd);
        _cgiInputOwners.erase(fd);
    }
    if (conn.cgi.inputFd == fd) {
        conn.cgi.inputFd = -1;
    }
    conn.cgi.inputClosed = true;
}

void WebServer::handleCgiWrite(int fd) {
    std::map<int, int>::iterator ownerIt = _cgiInputOwners.find(fd);
    if (ownerIt == _cgiInputOwners.end()) {
        return;
    }

    std::map<int, ClientConnection>::iterator clientIt = _clients.find(ownerIt->second);
    if (clientIt == _clients.end()) {
        removePollFd(fd);
        close(fd);
        _cgiInputOwners.erase(ownerIt);
        return;
    }

    ClientConnection &conn = clientIt->second;
    const std::string &body = conn.request.getBody();
    size_t remaining = body.size() - conn.cgi.inputOffset;

    if (remaining == 0) {
        closeCgiInput(conn, fd);
        return;
    }

    ssize_t written = write(fd, body.c_str() + conn.cgi.inputOffset, remaining);
    if (written <= 0) {
        closeCgiInput(conn, fd);
        if (conn.cgi.outputClosed && conn.cgi.childExited) {
            finalizeCgiResponse(clientIt->first);
        }
        return;
    }

    conn.cgi.inputOffset += static_cast<size_t>(written);
    if (conn.cgi.inputOffset >= body.size()) {
        closeCgiInput(conn, fd);
    }
}

void WebServer::handleCgiRead(int fd) {
    std::map<int, int>::iterator ownerIt = _cgiOutputOwners.find(fd);
    if (ownerIt == _cgiOutputOwners.end()) {
        return;
    }

    std::map<int, ClientConnection>::iterator clientIt = _clients.find(ownerIt->second);
    if (clientIt == _clients.end()) {
        removePollFd(fd);
        close(fd);
        _cgiOutputOwners.erase(ownerIt);
        return;
    }

    ClientConnection &conn = clientIt->second;
    char buffer[4096];

    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
    if (bytesRead > 0) {
        conn.cgi.output.append(buffer, bytesRead);
        return;
    }

    if (bytesRead < 0) {
        closeConnection(clientIt->first);
        return;
    }

    close(fd);
    removePollFd(fd);
    _cgiOutputOwners.erase(fd);
    conn.cgi.outputFd = -1;
    conn.cgi.outputClosed = true;
    if (conn.cgi.childExited) {
        finalizeCgiResponse(clientIt->first);
    }
}

void WebServer::finalizeCgiResponse(int clientFd) {
    std::map<int, ClientConnection>::iterator clientIt = _clients.find(clientFd);
    if (clientIt == _clients.end()) {
        return;
    }

    ClientConnection &conn = clientIt->second;
    Router router(conn.activeConfig);
    HttpResponse response;
    bool closeAfterSend = shouldCloseConnection(conn.request);

    if (conn.cgi.timedOut) {
        response = router.makeErrorResponse(504, "Gateway Timeout", "CGI execution timed out");
    } else {
        response = router.buildCgiResponse(conn.cgi.output, conn.cgi.exitStatus);
    }

    cleanupCgi(conn);
    queueResponse(clientFd, response, closeAfterSend);
}

void WebServer::cleanupCgi(ClientConnection &conn) {
    if (conn.cgi.inputFd >= 0) {
        close(conn.cgi.inputFd);
        removePollFd(conn.cgi.inputFd);
        _cgiInputOwners.erase(conn.cgi.inputFd);
        conn.cgi.inputFd = -1;
    }

    if (conn.cgi.outputFd >= 0) {
        close(conn.cgi.outputFd);
        removePollFd(conn.cgi.outputFd);
        _cgiOutputOwners.erase(conn.cgi.outputFd);
        conn.cgi.outputFd = -1;
    }

    if (conn.cgi.active && !conn.cgi.childExited) {
        kill(conn.cgi.pid, SIGKILL);
        waitpid(conn.cgi.pid, NULL, 0);
    }

    conn.cgi = CgiProcess();
    conn.awaitingCgi = false;
}

void WebServer::reapFinishedCgi() {
    std::vector<int> finishedClients;
    time_t now = time(NULL);

    for (std::map<int, ClientConnection>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        ClientConnection &conn = it->second;
        if (!conn.awaitingCgi || !conn.cgi.active || conn.cgi.childExited) {
            continue;
        }

        if (conn.cgi.startedAt != 0 && now - conn.cgi.startedAt > kCgiTimeoutSeconds) {
            kill(conn.cgi.pid, SIGKILL);
            int timeoutStatus = 0;
            waitpid(conn.cgi.pid, &timeoutStatus, 0);
            conn.cgi.childExited = true;
            conn.cgi.exitStatus = 1;
            conn.cgi.timedOut = true;

            if (conn.cgi.inputFd >= 0) {
                close(conn.cgi.inputFd);
                removePollFd(conn.cgi.inputFd);
                _cgiInputOwners.erase(conn.cgi.inputFd);
                conn.cgi.inputFd = -1;
                conn.cgi.inputClosed = true;
            }

            if (conn.cgi.outputClosed) {
                finishedClients.push_back(it->first);
            }
            continue;
        }

        int status = 0;
        pid_t result = waitpid(conn.cgi.pid, &status, WNOHANG);
        if (result <= 0) {
            continue;
        }

        conn.cgi.childExited = true;
        conn.cgi.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

        if (conn.cgi.outputClosed) {
            finishedClients.push_back(it->first);
        }
    }

    for (size_t i = 0; i < finishedClients.size(); ++i) {
        if (_clients.count(finishedClients[i])) {
            finalizeCgiResponse(finishedClients[i]);
        }
    }
}
