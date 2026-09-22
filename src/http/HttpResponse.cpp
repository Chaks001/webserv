#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse() : _statusCode(200), _reasonPhrase("OK") {}

HttpResponse::~HttpResponse() {}

void HttpResponse::setStatus(int code, const std::string &reason) {
    _statusCode = code;
    _reasonPhrase = reason;
}

void HttpResponse::setHeader(const std::string &key, const std::string &value) {
    _headers[key] = value;
}

void HttpResponse::setBody(const std::string &body) {
    _body = body;
    std::stringstream ss;
    ss << _body.size();
    setHeader("Content-Length", ss.str());
}

int HttpResponse::getStatusCode() const {
    return _statusCode;
}

const std::string &HttpResponse::getReasonPhrase() const {
    return _reasonPhrase;
}

std::string HttpResponse::toString(bool includeBody) const {
    std::stringstream ss;
    ss << "HTTP/1.1 " << _statusCode << " " << _reasonPhrase << "\r\n";
    
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        ss << it->first << ": " << it->second << "\r\n";
    }
    
    ss << "\r\n";
    if (includeBody) {
        ss << _body;
    }
    
    return ss.str();
}
