#include "ConfigParser.hpp"
#include <fstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
#include <set>

namespace {
    void rejectDuplicate(std::set<std::string> &seen, const std::string &directive, const std::string &block) {
        if (!seen.insert(directive).second) {
            throw std::runtime_error(directive + " is defined more than once in the same " + block + " block");
        }
    }
}

ConfigParser::ConfigParser(const std::string &path) : _configPath(path) {
    parseFile();
    validateServers();
}

void ConfigParser::validateServers() const {
    if (_servers.empty()) {
        throw std::runtime_error("Config file contains no server block");
    }
    for (size_t i = 0; i < _servers.size(); ++i) {
        for (size_t j = i + 1; j < _servers.size(); ++j) {
            if (_servers[i].host == _servers[j].host
                && _servers[i].port == _servers[j].port
                && _servers[i].server_name == _servers[j].server_name) {
                std::stringstream oss;
                oss << "Duplicate server: " << _servers[i].host << ":" << _servers[i].port
                    << " with server_name '" << _servers[i].server_name << "' is defined more than once";
                throw std::runtime_error(oss.str());
            }
        }
    }
}

ConfigParser::~ConfigParser() {}

unsigned long ConfigParser::parseNumericValue(std::stringstream &ss, const std::string &directive) {
    std::string value = parseValue(ss);
    if (value.empty()) {
        throw std::runtime_error(directive + " requires a numeric value");
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            throw std::runtime_error(directive + " must be a positive number, got: " + value);
        }
    }
    return std::strtoul(value.c_str(), NULL, 10);
}

const std::vector<ServerConfig> &ConfigParser::getServers() const {
    return _servers;
}

std::string ConfigParser::parseValue(std::stringstream &ss) {
    std::string value;
    ss >> value;
    if (!value.empty() && value[value.size() - 1] == ';') {
        value.erase(value.size() - 1);
    } else {
        std::string semi;
        ss >> semi;
        if (semi != ";") {
            throw std::runtime_error("Expected ';' after " + value);
        }
    }
    return value;
}

void ConfigParser::parseFile() {
    std::ifstream file(_configPath.c_str());
    if (!file.is_open()) {
        throw std::runtime_error("Could not open config file: " + _configPath);
    }

    std::stringstream ss;
    ss << file.rdbuf();
    file.close();

    std::string token;
    while (ss >> token) {
        if (token == "server") {
            std::string brace;
            ss >> brace;
            if (brace != "{") {
                throw std::runtime_error("Expected '{' after server");
            }
            parseServerBlock(ss);
        } else {
            throw std::runtime_error("Unexpected token: " + token);
        }
    }
}

void ConfigParser::parseServerBlock(std::stringstream &ss) {
    ServerConfig server;
    std::string token;
    std::set<std::string> seen;

    while (ss >> token) {
        if (token == "}") {
            for (size_t i = 0; i < server.locations.size(); ++i) {
                const LocationConfig &loc = server.locations[i];
                if (server.root.empty() && loc.root.empty() && loc.return_code == 0) {
                    throw std::runtime_error("root is missing for location " + loc.path);
                }
            }
            _servers.push_back(server);
            return;
        } else if (token == "listen") {
            rejectDuplicate(seen, token, "server");
            unsigned long parsedPort = parseNumericValue(ss, "listen");
            if (parsedPort < 1 || parsedPort > 65535) {
                std::stringstream oss;
                oss << "listen port out of range (1-65535): " << parsedPort;
                throw std::runtime_error(oss.str());
            }
            server.port = static_cast<int>(parsedPort);
        } else if (token == "server_name") {
            rejectDuplicate(seen, token, "server");
            server.server_name = parseValue(ss);
        } else if (token == "host") {
            rejectDuplicate(seen, token, "server");
            server.host = parseValue(ss);
        } else if (token == "root") {
            rejectDuplicate(seen, token, "server");
            server.root = parseValue(ss);
        } else if (token == "index") {
            rejectDuplicate(seen, token, "server");
            server.index = parseValue(ss);
        } else if (token == "client_max_body_size") {
            rejectDuplicate(seen, token, "server");
            server.client_max_body_size = parseNumericValue(ss, "client_max_body_size");
        } else if (token == "error_page") {
            int code = 0;
            ss >> code;
            std::stringstream key;
            key << "error_page " << code;
            rejectDuplicate(seen, key.str(), "server");
            server.error_pages[code] = parseValue(ss);
        } else if (token == "location") {
            parseLocationBlock(ss, server);
            rejectDuplicate(seen, "location " + server.locations.back().path, "server");
        } else {
            throw std::runtime_error("Unknown directive in server block: " + token);
        }
    }
    throw std::runtime_error("Unexpected end of file inside server block");
}

void ConfigParser::parseLocationBlock(std::stringstream &ss, ServerConfig &server) {
    LocationConfig loc;
    ss >> loc.path;
    std::string brace;
    ss >> brace;
    if (brace != "{") throw std::runtime_error("Expected '{' after location path");

    std::string token;
    std::set<std::string> seen;
    while (ss >> token) {
        if (token == "}") {
            server.locations.push_back(loc);
            return;
        } else if (token == "root") {
            rejectDuplicate(seen, token, "location");
            loc.root = parseValue(ss);
        } else if (token == "index") {
            rejectDuplicate(seen, token, "location");
            loc.index = parseValue(ss);
        } else if (token == "autoindex") {
            rejectDuplicate(seen, token, "location");
            std::string val = parseValue(ss);
            if (val != "on" && val != "off") {
                throw std::runtime_error("autoindex must be on or off, got: " + val);
            }
            loc.autoindex = (val == "on");
        } else if (token == "allow_methods") {
            rejectDuplicate(seen, token, "location");
            while (ss >> token) {
                if (token.find(";") != std::string::npos) {
                    token.erase(token.find(";"));
                    if (!token.empty()) loc.allow_methods.push_back(token);
                    break;
                }
                loc.allow_methods.push_back(token);
            }
        } else if (token == "return") {
            rejectDuplicate(seen, token, "location");
            ss >> loc.return_code;
            loc.return_url = parseValue(ss);
        } else if (token == "upload_store") {
            rejectDuplicate(seen, token, "location");
            loc.upload_store = parseValue(ss);
        } else if (token == "cgi_pass") {
            std::string ext;
            ss >> ext;
            rejectDuplicate(seen, "cgi_pass " + ext, "location");
            loc.cgi_pass[ext] = parseValue(ss);
        } else if (token == "client_max_body_size") {
            rejectDuplicate(seen, token, "location");
            loc.client_max_body_size = parseNumericValue(ss, "client_max_body_size");
            loc.has_client_max_body_size = true;
        } else {
            throw std::runtime_error("Unknown directive in location block: " + token);
        }
    }
    throw std::runtime_error("Unexpected end of file inside location block");
}
