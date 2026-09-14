#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <string>
#include <map>

class HttpResponse {
private:
    int _statusCode;
    std::string _reasonPhrase;
    std::map<std::string, std::string> _headers;
    std::string _body;

public:
    HttpResponse();
    ~HttpResponse();

    void setStatus(int code, const std::string &reason);
    void setHeader(const std::string &key, const std::string &value);
    void setBody(const std::string &body);
    std::string toString(bool includeBody = true) const;
};

#endif
