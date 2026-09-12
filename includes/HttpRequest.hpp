#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <map>

class HttpRequest {
private:
    std::string _method;
    std::string _uri;
    std::string _path;         // URI path without query string
    std::string _queryString;  // Query string (after ?)
    std::string _version;
    std::map<std::string, std::string> _headers;
    std::string _body;
    int _errorCode;
    std::string _errorReason;
    std::string _errorBody;
    
    // Parsing state
    std::string _rawBuffer;
    bool _headersParsed;
    bool _bodyParsed;
    size_t _contentLength;
    bool _isChunked;

    void parseRequestLine(const std::string &line);
    void parseHeader(const std::string &line);
    void setError(int code, const std::string &reason, const std::string &body);

public:
    HttpRequest();
    ~HttpRequest();

    bool parse(const char *data, size_t size, size_t maxBodySize);
    bool isComplete() const;
    bool hasError() const;

    const std::string &getMethod() const;
    const std::string &getUri() const;
    const std::string &getPath() const;
    const std::string &getQueryString() const;
    const std::string &getVersion() const;
    const std::string &getBody() const;
    int getErrorCode() const;
    const std::string &getErrorReason() const;
    const std::string &getErrorBody() const;
    const std::map<std::string, std::string> &getHeaders() const;
    std::string getHeader(const std::string &key) const;
};

#endif
