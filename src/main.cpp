#include <iostream>
#include <signal.h>
#include "ConfigParser.hpp"
#include "WebServer.hpp"

int main(int argc, char **argv) {
    if (argc > 2) {
        std::cerr << "Usage: ./webserv [configuration_file]" << std::endl;
        return 1;
    }

    std::string configPath = (argc == 2) ? argv[1] : "config/default.conf";

    std::cout << "Starting Webserv with config: " << configPath << std::endl;

    try {
        signal(SIGPIPE, SIG_IGN);
        signal(SIGINT, WebServer::handleSignal);

        ConfigParser parser(configPath);
        const std::vector<ServerConfig> &servers = parser.getServers();
        std::cout << "Successfully parsed config. Number of servers: " << servers.size() << std::endl;
        
        WebServer server(servers);
        server.run();

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
