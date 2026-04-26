#include "ConfigParser.hpp"
#include <iostream>
#include <stdexcept>

ConfigParser::ConfigParser(const std::string &path) : _configPath(path) {
    parseFile();
}

ConfigParser::~ConfigParser() {}

const std::vector<ServerConfig> &ConfigParser::getServers() const {
    return _servers;
}

// Helper function to parse a value and strip trailing semicolon
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
            ss >> server.port;
            std::string semi; ss >> semi;
        } else if (token == "server_name") {
            server.server_name = parseValue(ss);
        } else if (token == "host") {
            server.host = parseValue(ss);
        } else if (token == "root") {
            server.root = parseValue(ss);
        } else if (token == "index") {
            server.index = parseValue(ss);
        } else if (token == "client_max_body_size") {
            ss >> server.client_max_body_size;
            std::string semi; ss >> semi;
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
            ss >> loc.client_max_body_size;
            loc.has_client_max_body_size = true;
            std::string semi; ss >> semi;
        }
    }
    throw std::runtime_error("Unexpected end of file inside location block");
}
