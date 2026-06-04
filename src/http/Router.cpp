#include "Router.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
    bool hasSuffix(const std::string &value, const std::string &suffix) {
        return value.size() >= suffix.size()
            && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string joinPaths(const std::string &base, const std::string &suffix) {
        if (base.empty()) return suffix;
        if (suffix.empty()) return base;

        std::string normalizedBase = base;
        std::string normalizedSuffix = suffix;

        while (!normalizedBase.empty() && normalizedBase[normalizedBase.size() - 1] == '/')
            normalizedBase.erase(normalizedBase.size() - 1);
        while (!normalizedSuffix.empty() && normalizedSuffix[0] == '/')
            normalizedSuffix.erase(0, 1);

        if (normalizedSuffix.empty()) return normalizedBase;
        return normalizedBase + "/" + normalizedSuffix;
    }

    std::string statusText(int statusCode) {
        switch (statusCode) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 301: return "Moved Permanently";
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 413: return "Payload Too Large";
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 504: return "Gateway Timeout";
            default: return "Error";
        }
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

    int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string decodePercentEncodedPath(const std::string &path) {
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                int high = hexValue(path[i + 1]);
                int low = hexValue(path[i + 2]);
                if (high >= 0 && low >= 0) {
                    decoded += static_cast<char>(high * 16 + low);
                    i += 2;
                    continue;
                }
            }
            decoded += path[i];
        }
        return decoded;
    }

    bool hasParentDirectorySegment(const std::string &path) {
        std::string decodedPath = decodePercentEncodedPath(path);
        size_t start = 0;
        while (start <= decodedPath.size()) {
            size_t end = decodedPath.find_first_of("/\\", start);
            std::string segment = decodedPath.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (segment == "..") {
                return true;
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return false;
    }
}

Router::Router(const ServerConfig &config) : _config(config) {}

Router::~Router() {}

RouteResult Router::resolveRequest(const HttpRequest &request) const {
    RouteResult result;
    if (hasParentDirectorySegment(request.getPath())) {
        result.response = makeErrorResponse(403, "Forbidden", "403 Forbidden");
        return result;
    }

    const LocationConfig *loc = matchLocation(request.getPath());

    if (!loc) {
        result.response = makeErrorResponse(404, "Not Found", "404 Not Found");
        return result;
    }

    std::string method = request.getMethod();

    if (!isMethodAllowed(*loc, method)) {
        result.response = makeErrorResponse(405, "Method Not Allowed", "405 Method Not Allowed");
        return result;
    }

    if (loc->return_code != 0) {
        result.response.setStatus(loc->return_code, statusText(loc->return_code));
        result.response.setHeader("Location", loc->return_url);
        result.response.setBody("");
        return result;
    }

    std::string path = getFullPath(*loc, request.getPath());
    std::string extension;
    size_t dot = path.find_last_of(".");
    if (dot != std::string::npos) extension = path.substr(dot);

    if (method == "DELETE") {
        if (fileExists(path)) {
            if (std::remove(path.c_str()) == 0) result.response.setStatus(204, "No Content");
            else result.response = makeErrorResponse(500, "Internal Server Error", "Could not delete file");
        } else {
            result.response = makeErrorResponse(404, "Not Found", "404 Not Found");
        }
        return result;
    }

    if (!extension.empty()) {
        std::map<std::string, std::string>::const_iterator cgiIt = loc->cgi_pass.find(extension);
        if (cgiIt != loc->cgi_pass.end() && (method == "GET" || method == "POST")) {
            if (!fileExists(path) || isDirectory(path)) {
                result.response = makeErrorResponse(404, "Not Found", "404 Not Found");
                return result;
            }
            result.isCgi = true;
            result.cgiScriptPath = path;
            result.cgiInterpreterPath = cgiIt->second;
            return result;
        }
    }

    if (method == "POST" && !loc->upload_store.empty()) {
        std::string filename = path.substr(path.find_last_of("/") + 1);
        if (filename.empty()) filename = "uploaded_file";
        std::string targetPath = joinPaths(loc->upload_store, filename);
        std::ofstream outfile(targetPath.c_str(), std::ios::binary);
        if (outfile.is_open()) {
            outfile << request.getBody();
            outfile.close();
            result.response.setStatus(201, "Created");
            result.response.setBody("File uploaded successfully");
        } else {
            result.response = makeErrorResponse(500, "Internal Server Error", "Could not save file");
        }
        return result;
    }

    if (method == "POST") {
        result.response.setStatus(200, "OK");
        result.response.setHeader("Content-Type", "text/plain");
        result.response.setBody(request.getBody());
        return result;
    }

    if (isDirectory(path)) {
        if (request.getUri()[request.getUri().length() - 1] != '/') {
            result.response.setStatus(301, "Moved Permanently");
            result.response.setHeader("Location", request.getUri() + "/");
            result.response.setBody("");
            return result;
        }
        std::string indexFile = loc->index.empty() ? _config.index : loc->index;
        if (!indexFile.empty() && fileExists(joinPaths(path, indexFile))) {
            path = joinPaths(path, indexFile);
        } else if (loc->autoindex) {
            result.response.setStatus(200, "OK");
            result.response.setHeader("Content-Type", "text/html");
            result.response.setBody(generateAutoindex(path, request.getUri()));
            return result;
        } else if (!indexFile.empty()) {
            result.response = makeErrorResponse(404, "Not Found", "404 Not Found");
            return result;
        } else {
            result.response = makeErrorResponse(403, "Forbidden", "403 Forbidden");
            return result;
        }
    }

    if (fileExists(path)) {
        result.response.setStatus(200, "OK");
        result.response.setHeader("Content-Type", detectContentType(path));
        result.response.setBody(readFile(path));
    } else {
        result.response = makeErrorResponse(404, "Not Found", "404 Not Found");
    }

    std::cout << "Response: " << result.response.getStatusCode() << " Path: " << path << std::endl;
    return result;
}

std::string Router::detectContentType(const std::string &path) const {
    if (hasSuffix(path, ".html") || hasSuffix(path, ".htm")) return "text/html";
    if (hasSuffix(path, ".css")) return "text/css";
    if (hasSuffix(path, ".js")) return "application/javascript";
    if (hasSuffix(path, ".png")) return "image/png";
    if (hasSuffix(path, ".jpg") || hasSuffix(path, ".jpeg")) return "image/jpeg";
    if (hasSuffix(path, ".gif")) return "image/gif";
    if (hasSuffix(path, ".txt") || hasSuffix(path, ".bad_extension")) return "text/plain";
    return "application/octet-stream";
}

HttpResponse Router::makeErrorResponse(int statusCode, const std::string &reason, const std::string &defaultBody) const {
    HttpResponse response;
    response.setStatus(statusCode, reason);
    std::map<int, std::string>::const_iterator it = _config.error_pages.find(statusCode);
    if (it != _config.error_pages.end()) {
        std::string errorPath = getErrorPagePath(it->second);
        if (fileExists(errorPath) && !isDirectory(errorPath)) {
            response.setHeader("Content-Type", detectContentType(errorPath));
            response.setBody(readFile(errorPath));
            return response;
        }
    }
    response.setHeader("Content-Type", "text/plain");
    response.setBody(defaultBody);
    return response;
}

HttpResponse Router::buildCgiResponse(const std::string &cgiOutput, int exitStatus) const {
    if (cgiOutput.empty() && exitStatus != 0) return makeErrorResponse(500, "Internal Server Error", "CGI failed");
    size_t separatorLength = 4;
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        headerEnd = cgiOutput.find("\n\n");
        separatorLength = 2;
    }
    if (headerEnd == std::string::npos) {
        if (exitStatus == 0) {
            HttpResponse response;
            response.setStatus(200, "OK");
            response.setBody(cgiOutput);
            return response;
        }
        return makeErrorResponse(500, "Internal Server Error", "Bad CGI Output");
    }
    HttpResponse response;
    response.setStatus(200, "OK");
    response.setBody(cgiOutput.substr(headerEnd + separatorLength));
    std::stringstream ss(cgiOutput.substr(0, headerEnd));
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon), value = line.substr(colon + 1);
        size_t first = value.find_first_not_of(" \t");
        if (first != std::string::npos) value = value.substr(first);
        if (key == "Status") {
            int code = 200; std::stringstream(value) >> code;
            response.setStatus(code, statusText(code));
        } else response.setHeader(key, value);
    }
    return response;
}

HttpResponse Router::handleRequest(const HttpRequest &request) {
    RouteResult result = resolveRequest(request);
    if (result.isCgi) return makeErrorResponse(500, "Internal Server Error", "CGI handled elsewhere");
    return result.response;
}

const LocationConfig *Router::matchLocation(const std::string &uri) const {
    const LocationConfig *bestMatch = NULL;
    size_t bestLen = 0;
    for (size_t i = 0; i < _config.locations.size(); ++i) {
        const std::string &path = _config.locations[i].path;
        if (locationMatches(uri, path) && path.size() >= bestLen) {
            bestMatch = &_config.locations[i];
            bestLen = path.size();
        }
    }
    return bestMatch;
}

bool Router::isMethodAllowed(const LocationConfig &loc, const std::string &method) const {
    for (size_t i = 0; i < loc.allow_methods.size(); ++i) {
        if (loc.allow_methods[i] == method) {
            return true;
        }
    }
    return false;
}

std::string Router::getFullPath(const LocationConfig &loc, const std::string &uri) const {
    std::string root = loc.root.empty() ? _config.root : loc.root;
    std::string suffix = uri.substr(loc.path.size());
    if (!suffix.empty() && suffix[0] == '/') suffix.erase(0, 1);
    return joinPaths(root, suffix);
}

bool Router::isDirectory(const std::string &path) const {
    struct stat s;
    return stat(path.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}

bool Router::fileExists(const std::string &path) const {
    struct stat s;
    return stat(path.c_str(), &s) == 0;
}

std::string Router::readFile(const std::string &path) const {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) return "";
    std::stringstream ss; ss << file.rdbuf();
    return ss.str();
}

std::string Router::generateAutoindex(const std::string &path, const std::string &uri) const {
    std::stringstream ss;
    ss << "<html><head><title>Index of " << uri << "</title></head><body>";
    ss << "<h1>Index of " << uri << "</h1><hr><pre>";
    DIR *dir = opendir(path.c_str());
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string name = ent->d_name;
            if (name == ".") continue;
            ss << "<a href=\"" << (uri == "/" ? "" : uri) << "/" << name << "\">" << name << "</a><br>";
        }
        closedir(dir);
    }
    ss << "</pre><hr></body></html>";
    return ss.str();
}

std::string Router::getErrorPagePath(const std::string &configuredPath) const {
    if (configuredPath.empty()) return "";
    if (configuredPath[0] == '/') return configuredPath;
    return joinPaths(_config.root, configuredPath);
}
