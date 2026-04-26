#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "ServerConfig.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>

class ConfigParser {
private:
    std::string _configPath;
    std::vector<ServerConfig> _servers;

    void parseFile();
    void parseServerBlock(std::stringstream &ss);
    void parseLocationBlock(std::stringstream &ss, ServerConfig &server);
    std::string parseValue(std::stringstream &ss);

public:
    ConfigParser(const std::string &path);
    ~ConfigParser();

    const std::vector<ServerConfig> &getServers() const;
};

#endif
