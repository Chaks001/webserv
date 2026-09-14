#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "HttpRequest.hpp"
#include "ServerConfig.hpp"
#include <string>
#include <map>
#include <ctime>
#include <sys/types.h>

typedef pid_t CgiPid;

struct CgiProcess {
    bool active;
    CgiPid pid;
    int inputFd;
    int outputFd;
    size_t inputOffset;
    std::string output;
    bool outputClosed;
    bool childExited;
    int exitStatus;
    bool timedOut;
    time_t lastActivityAt;

    CgiProcess();
};

class CgiHandler {
private:
    std::string _scriptPath;
    std::string _interpreterPath;
    std::map<std::string, std::string> _env;

    void setupEnv(const HttpRequest &request, const ServerConfig &config);
    char **createEnvArray() const;
    void freeEnvArray(char **envp) const;

public:
    CgiHandler(const std::string &scriptPath, const std::string &interpreterPath);
    ~CgiHandler();

    bool launch(const HttpRequest &request, const ServerConfig &config, CgiProcess &process, std::string &errorMessage);
};

#endif
