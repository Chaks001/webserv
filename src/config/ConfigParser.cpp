#include "ConfigParser.hpp"
#include <iostream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>

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
    } else if (value.find(';') != std::string::npos) {
        value.erase(value.find(';'));
    } else {
        std::string semi;
        ss >> semi;
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

    while (ss >> token) {
        if (token == "}") {
            _servers.push_back(server);
            return;
        } else if (token == "listen") {
            unsigned long parsedPort = parseNumericValue(ss, "listen");
            if (parsedPort < 1 || parsedPort > 65535) {
                std::stringstream oss;
                oss << "listen port out of range (1-65535): " << parsedPort;
                throw std::runtime_error(oss.str());
            }
            server.port = static_cast<int>(parsedPort);
        } else if (token == "server_name") {
            server.server_name = parseValue(ss);
        } else if (token == "host") {
            server.host = parseValue(ss);
        } else if (token == "root") {
            server.root = parseValue(ss);
        } else if (token == "index") {
            server.index = parseValue(ss);
        } else if (token == "client_max_body_size") {
            server.client_max_body_size = parseNumericValue(ss, "client_max_body_size");
        } else if (token == "error_page") {
            int code;
            ss >> code;
            server.error_pages[code] = parseValue(ss);
        } else if (token == "location") {
            parseLocationBlock(ss, server);
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
    while (ss >> token) {
        if (token == "}") {
            server.locations.push_back(loc);
            return;
        } else if (token == "root") {
            loc.root = parseValue(ss);
        } else if (token == "index") {
            loc.index = parseValue(ss);
        } else if (token == "autoindex") {
            std::string val = parseValue(ss);
            loc.autoindex = (val == "on");
        } else if (token == "allow_methods") {
            while (ss >> token) {
                if (token.find(";") != std::string::npos) {
                    token.erase(token.find(";"));
                    if (!token.empty()) loc.allow_methods.push_back(token);
                    break;
                }
                loc.allow_methods.push_back(token);
            }
        } else if (token == "return") {
            ss >> loc.return_code;
            loc.return_url = parseValue(ss);
        } else if (token == "upload_store") {
            loc.upload_store = parseValue(ss);
        } else if (token == "cgi_pass") {
            std::string ext;
            ss >> ext;
            loc.cgi_pass[ext] = parseValue(ss);
        } else if (token == "client_max_body_size") {
            loc.client_max_body_size = parseNumericValue(ss, "client_max_body_size");
            loc.has_client_max_body_size = true;
        }
    }
    throw std::runtime_error("Unexpected end of file inside location block");
}
