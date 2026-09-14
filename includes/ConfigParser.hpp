#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "ServerConfig.hpp"
#include <string>
#include <vector>
#include <sstream>

class ConfigParser {
private:
    std::string _configPath;
    std::vector<ServerConfig> _servers;

    void parseFile();
    void validateServers() const;
    unsigned long parseNumericValue(std::stringstream &ss, const std::string &directive);
    void parseServerBlock(std::stringstream &ss);
    void parseLocationBlock(std::stringstream &ss, ServerConfig &server);
    std::string parseValue(std::stringstream &ss);

public:
    ConfigParser(const std::string &path);
    ~ConfigParser();

    const std::vector<ServerConfig> &getServers() const;
};

#endif
