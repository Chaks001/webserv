#include "CgiHandler.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <cctype>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>

namespace {
    bool setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            return false;
        }
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    std::string scriptDirectory(const std::string &scriptPath) {
        size_t separator = scriptPath.find_last_of("/\\");
        if (separator == std::string::npos) {
            return ".";
        }
        if (separator == 0) {
            return scriptPath.substr(0, 1);
        }
        return scriptPath.substr(0, separator);
    }

    std::string scriptName(const std::string &scriptPath) {
        size_t separator = scriptPath.find_last_of("/\\");
        if (separator == std::string::npos) {
            return scriptPath;
        }
        return scriptPath.substr(separator + 1);
    }

    bool startsWith(const std::string &value, const std::string &prefix) {
        return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
    }

    bool shouldPassScriptArgument(const std::string &path) {
        std::string name = scriptName(path);
        return startsWith(name, "python")
            || name == "php" || name == "php-cgi"
            || name == "perl"
            || name == "ruby"
            || name == "node"
            || name == "sh" || name == "bash";
    }

    bool isAbsolutePath(const std::string &path) {
        return !path.empty() && path[0] == '/';
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

    std::string makeAbsolutePath(const std::string &path) {
        if (isAbsolutePath(path)) {
            return path;
        }

        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            return path;
        }
        return joinPaths(cwd, path);
    }

    std::string headerToEnvName(const std::string &headerName) {
        std::string result = "HTTP_";
        for (size_t i = 0; i < headerName.size(); ++i) {
            char c = headerName[i];
            if (c == '-') {
                result += '_';
            } else {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        return result;
    }
}

CgiProcess::CgiProcess()
    : active(false),
      pid(0),
      inputFd(-1),
      outputFd(-1),
      inputOffset(0),
      inputClosed(false),
      outputClosed(false),
      childExited(false),
      exitStatus(0),
      timedOut(false),
      startedAt(0) {}

CgiHandler::CgiHandler(const std::string &scriptPath, const std::string &interpreterPath)
    : _scriptPath(makeAbsolutePath(scriptPath)), _interpreterPath(makeAbsolutePath(interpreterPath)) {}

CgiHandler::~CgiHandler() {}

bool CgiHandler::launch(const HttpRequest &request, const ServerConfig &config, CgiProcess &process, std::string &errorMessage) {
    setupEnv(request, config);

    int pipeIn[2];
    int pipeOut[2];

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0) {
        errorMessage = "pipe failed";
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeIn[0]);
        close(pipeIn[1]);
        close(pipeOut[0]);
        close(pipeOut[1]);
        errorMessage = "fork failed";
        return false;
    }

    if (pid == 0) {
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);

        close(pipeIn[0]);
        close(pipeIn[1]);
        close(pipeOut[0]);
        close(pipeOut[1]);

        char **envp = createEnvArray();
        char *argv[3] = {NULL, NULL, NULL};
        std::string executablePath = makeAbsolutePath(_interpreterPath);
        argv[0] = const_cast<char*>(executablePath.c_str());
        std::string workingDirectory = scriptDirectory(_scriptPath);
        std::string executableScript = scriptName(_scriptPath);
        if (shouldPassScriptArgument(executablePath)) {
            argv[1] = const_cast<char*>(executableScript.c_str());
            argv[2] = NULL;
        } else {
            argv[1] = NULL;
        }

        if (chdir(workingDirectory.c_str()) != 0) {
            freeEnvArray(envp);
            std::cerr << "chdir failed" << std::endl;
            _exit(1);
        }

        execve(executablePath.c_str(), argv, envp);

        freeEnvArray(envp);
        std::cerr << "execve failed" << std::endl;
        _exit(1);
    }

    close(pipeIn[0]);
    close(pipeOut[1]);

    if (!setNonBlocking(pipeIn[1]) || !setNonBlocking(pipeOut[0])) {
        close(pipeIn[1]);
        close(pipeOut[0]);
        waitpid(pid, NULL, 0);
        errorMessage = "failed to switch CGI pipes to non-blocking mode";
        return false;
    }

    process.active = true;
    process.pid = pid;
    process.inputFd = pipeIn[1];
    process.outputFd = pipeOut[0];
    process.startedAt = time(NULL);
    return true;
}

void CgiHandler::setupEnv(const HttpRequest &request, const ServerConfig &config) {
    _env["REQUEST_METHOD"] = request.getMethod();
    _env["SCRIPT_FILENAME"] = _scriptPath;
    _env["SCRIPT_NAME"] = request.getPath();
    _env["REQUEST_URI"] = request.getUri();
    _env["QUERY_STRING"] = request.getQueryString();
    _env["CONTENT_LENGTH"] = request.getHeader("Content-Length");
    _env["CONTENT_TYPE"] = request.getHeader("Content-Type");
    _env["GATEWAY_INTERFACE"] = "CGI/1.1";
    _env["SERVER_PROTOCOL"] = "HTTP/1.1";
    _env["SERVER_SOFTWARE"] = "Webserv/1.0";
    _env["SERVER_NAME"] = config.server_name;
    _env["REMOTE_ADDR"] = "127.0.0.1";
    _env["REMOTE_PORT"] = "0";
    
    std::stringstream portSs;
    portSs << config.port;
    _env["SERVER_PORT"] = portSs.str();

    _env["PATH_INFO"] = request.getPath();
    _env["PATH_TRANSLATED"] = "";
    _env["REDIRECT_STATUS"] = "200";
    
    const std::map<std::string, std::string> &headers = request.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        std::string envName = headerToEnvName(it->first);
        if (envName != "HTTP_CONTENT_LENGTH" && envName != "HTTP_CONTENT_TYPE") {
            _env[envName] = it->second;
        }
    }
}

char **CgiHandler::createEnvArray() const {
    char **envp = new char*[_env.size() + 1];
    int i = 0;
    for (std::map<std::string, std::string>::const_iterator it = _env.begin(); it != _env.end(); ++it) {
        std::string s = it->first + "=" + it->second;
        envp[i] = new char[s.size() + 1];
        std::strcpy(envp[i], s.c_str());
        i++;
    }
    envp[i] = NULL;
    return envp;
}

void CgiHandler::freeEnvArray(char **envp) const {
    for (int i = 0; envp[i]; ++i) {
        delete[] envp[i];
    }
    delete[] envp;
}
