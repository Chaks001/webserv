#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>
#include <map>

struct LocationConfig {
    std::string path;
    std::string root;
    std::string index;
    bool autoindex;
    std::vector<std::string> allow_methods;
    std::string return_url; // For redirections
    int return_code;        // For redirections
    std::string upload_store;
    std::map<std::string, std::string> cgi_pass; // extension -> interpreter
    unsigned long client_max_body_size;
    bool has_client_max_body_size;

    LocationConfig() : autoindex(false), return_code(0), client_max_body_size(0), has_client_max_body_size(false) {}
};

struct ServerConfig {
    int port;
    std::string host;
    std::string server_name;
    std::string root;
    std::string index;
    unsigned long client_max_body_size; // in bytes
    std::map<int, std::string> error_pages;
    std::vector<LocationConfig> locations;

    ServerConfig() : port(8080), host("127.0.0.1"), client_max_body_size(static_cast<unsigned long>(-1)) {}
};

#endif
