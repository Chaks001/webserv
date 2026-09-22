#include "Router.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <vector>
#include <utility>

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
            case 302: return "Found";
            case 303: return "See Other";
            case 307: return "Temporary Redirect";
            case 308: return "Permanent Redirect";
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 413: return "Payload Too Large";
            case 414: return "URI Too Long";
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 504: return "Gateway Timeout";
            case 505: return "HTTP Version Not Supported";
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
                if (high >= 0 && low >= 0 && high * 16 + low != 0) {
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

    std::string headerParameter(const std::string &value, const std::string &name) {
        const std::string key = name + "=";
        size_t pos = value.find(key);
        while (pos != std::string::npos) {
            if (pos == 0 || value[pos - 1] == ';' || value[pos - 1] == ' ') {
                size_t start = pos + key.size();
                if (start < value.size() && value[start] == '"') {
                    size_t closingQuote = value.find('"', start + 1);
                    return closingQuote == std::string::npos ? std::string() : value.substr(start + 1, closingQuote - start - 1);
                }
                size_t end = value.find_first_of("; \r\n", start);
                return value.substr(start, end == std::string::npos ? std::string::npos : end - start);
            }
            pos = value.find(key, pos + 1);
        }
        return std::string();
    }

    std::string safeFileName(const std::string &name) {
        size_t slash = name.find_last_of("/\\");
        std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
        if (base == "." || base == "..") {
            return std::string();
        }
        return base;
    }

    bool writeFile(const std::string &path, const std::string &data) {
        std::ofstream file(path.c_str(), std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        file.write(data.c_str(), static_cast<std::streamsize>(data.size()));
        return file.good();
    }

    bool parseMultipart(const std::string &body, const std::string &boundary,
                        std::vector<std::pair<std::string, std::string> > &files) {
        const std::string delimiter = "--" + boundary;
        const std::string nextDelimiter = "\r\n" + delimiter;
        size_t pos = body.find(delimiter);
        while (pos != std::string::npos) {
            pos += delimiter.size();
            if (body.compare(pos, 2, "--") == 0) {
                return true;
            }
            size_t headersEnd = body.find("\r\n\r\n", pos);
            size_t next = body.find(nextDelimiter, headersEnd);
            if (headersEnd == std::string::npos || next == std::string::npos) {
                return false;
            }
            size_t contentStart = headersEnd + 4;
            std::string filename = safeFileName(headerParameter(body.substr(pos, headersEnd - pos), "filename"));
            if (!filename.empty()) {
                std::string content = next > contentStart ? body.substr(contentStart, next - contentStart) : std::string();
                files.push_back(std::make_pair(filename, content));
            }
            pos = next + 2;
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

    std::string path = getFullPath(*loc, decodePercentEncodedPath(request.getPath()));
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
            if (isDirectory(path)) {
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
        const std::string contentType = request.getHeader("Content-Type");
        if (contentType.find("multipart/form-data") == 0) {
            const std::string boundary = headerParameter(contentType, "boundary");
            std::vector<std::pair<std::string, std::string> > files;
            if (boundary.empty() || !parseMultipart(request.getBody(), boundary, files) || files.empty()) {
                result.response = makeErrorResponse(400, "Bad Request", "400 Bad Request");
                return result;
            }
            for (size_t i = 0; i < files.size(); ++i) {
                if (!writeFile(joinPaths(loc->upload_store, files[i].first), files[i].second)) {
                    result.response = makeErrorResponse(500, "Internal Server Error", "Could not save file");
                    return result;
                }
            }
        } else {
            std::string filename = isDirectory(path) ? std::string() : path.substr(path.find_last_of("/") + 1);
            if (filename.empty()) filename = "uploaded_file";
            if (!writeFile(joinPaths(loc->upload_store, filename), request.getBody())) {
                result.response = makeErrorResponse(500, "Internal Server Error", "Could not save file");
                return result;
            }
        }
        result.response.setStatus(201, "Created");
        result.response.setBody("File uploaded successfully");
        return result;
    }

    if (method == "POST") {
        result.response.setStatus(200, "OK");
        result.response.setHeader("Content-Type", "text/plain");
        result.response.setBody(request.getBody());
        return result;
    }

    if (isDirectory(path)) {
        const std::string &requestUri = request.getUri();
        if (requestUri.empty() || requestUri[requestUri.size() - 1] != '/') {
            result.response.setStatus(301, "Moved Permanently");
            result.response.setHeader("Location", request.getUri() + "/");
            result.response.setBody("");
            return result;
        }
        std::string indexFile = loc->index.empty() ? _config.index : loc->index;
        if (!indexFile.empty() && fileExists(joinPaths(path, indexFile))) {
            path = joinPaths(path, indexFile);
            size_t newDot = path.find_last_of(".");
            if (newDot != std::string::npos) {
                std::string newExt = path.substr(newDot);
                std::map<std::string, std::string>::const_iterator cgiIt = loc->cgi_pass.find(newExt);
                if (cgiIt != loc->cgi_pass.end() && (method == "GET" || method == "POST")) {
                    result.isCgi = true;
                    result.cgiScriptPath = path;
                    result.cgiInterpreterPath = cgiIt->second;
                    return result;
                }
            }
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

    if (!fileExists(path)) {
        result.response = makeErrorResponse(404, "Not Found", "404 Not Found");
    } else if (access(path.c_str(), R_OK) != 0) {
        result.response = makeErrorResponse(403, "Forbidden", "403 Forbidden");
    } else {
        result.response.setStatus(200, "OK");
        result.response.setHeader("Content-Type", detectContentType(path));
        result.response.setBody(readFile(path));
    }

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
    if (exitStatus != 0) return makeErrorResponse(500, "Internal Server Error", "CGI failed");
    size_t separatorLength = 4;
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        headerEnd = cgiOutput.find("\n\n");
        separatorLength = 2;
    }
    if (headerEnd == std::string::npos) {
        HttpResponse response;
        response.setStatus(200, "OK");
        response.setBody(cgiOutput);
        return response;
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
    std::string suffix = uri.size() >= loc.path.size() ? uri.substr(loc.path.size()) : std::string();
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
