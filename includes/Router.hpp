#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "ServerConfig.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

struct RouteResult {
    bool isCgi;
    HttpResponse response;
    std::string cgiScriptPath;
    std::string cgiInterpreterPath;

    RouteResult() : isCgi(false) {}
};

class Router {
private:
    ServerConfig _config;

    const LocationConfig *matchLocation(const std::string &uri) const;
    bool isMethodAllowed(const LocationConfig &loc, const std::string &method) const;
    std::string getFullPath(const LocationConfig &loc, const std::string &uri) const;
    bool isDirectory(const std::string &path) const;
    bool fileExists(const std::string &path) const;
    std::string readFile(const std::string &path) const;
    std::string generateAutoindex(const std::string &path, const std::string &uri) const;
    std::string detectContentType(const std::string &path) const;
    std::string getErrorPagePath(const std::string &configuredPath) const;

public:
    Router(const ServerConfig &config);
    ~Router();

    RouteResult resolveRequest(const HttpRequest &request) const;
    HttpResponse makeErrorResponse(int statusCode, const std::string &reason, const std::string &defaultBody) const;
    HttpResponse buildCgiResponse(const std::string &cgiOutput, int exitStatus) const;
};

#endif
