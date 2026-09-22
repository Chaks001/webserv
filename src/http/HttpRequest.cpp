#include "HttpRequest.hpp"
#include <sstream>
#include <cstdlib>
#include <cctype>

namespace {
    const size_t kMaxHeaderSize = 64 * 1024;
    const size_t kMaxUriSize = 8 * 1024;
    const size_t kMaxChunkSize = 1024 * 1024 * 1024;
    const size_t kUnlimitedBodySize = static_cast<size_t>(-1);

    std::string toLower(std::string value) {
        for (size_t i = 0; i < value.size(); ++i) {
            value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
        }
        return value;
    }
}

HttpRequest::HttpRequest()
    : _errorCode(0),
      _headersParsed(false),
      _bodyParsed(false),
      _contentLength(0),
      _isChunked(false) {}

HttpRequest::~HttpRequest() {}

bool HttpRequest::parse(const char *data, size_t size, size_t maxBodySize) {
    if (hasError()) {
        return true;
    }

    _rawBuffer.append(data, size);

    if (!_headersParsed) {
        if (_rawBuffer.size() > kMaxHeaderSize) {
            setError(431, "Request Header Fields Too Large", "431 Request Header Fields Too Large");
            return true;
        }

        size_t separatorLength = 4;
        size_t headerEnd = _rawBuffer.find("\r\n\r\n");
        size_t lineFeedEnd = _rawBuffer.find("\n\n");
        if (lineFeedEnd != std::string::npos && (headerEnd == std::string::npos || lineFeedEnd < headerEnd)) {
            headerEnd = lineFeedEnd;
            separatorLength = 2;
        }
        if (headerEnd != std::string::npos) {
            std::string headerPart = _rawBuffer.substr(0, headerEnd);
            std::string bodyPart = _rawBuffer.substr(headerEnd + separatorLength);

            std::stringstream ss(headerPart);
            std::string line;

            if (std::getline(ss, line)) {
                if (!line.empty() && line[line.size() - 1] == '\r') {
                    line.erase(line.size() - 1);
                }
                parseRequestLine(line);
                if (hasError()) {
                    return true;
                }
            } else {
                setError(400, "Bad Request", "400 Bad Request");
                return true;
            }

            while (std::getline(ss, line)) {
                if (!line.empty() && line[line.size() - 1] == '\r') {
                    line.erase(line.size() - 1);
                }
                if (line.empty()) {
                    break;
                }
                parseHeader(line);
            }

            _headersParsed = true;

            std::string cl = getHeader("Content-Length");
            if (!cl.empty()) {
                if (cl.find_first_not_of("0123456789") != std::string::npos || cl.size() > 19) {
                    setError(400, "Bad Request", "400 Bad Request");
                    return true;
                }
                _contentLength = static_cast<size_t>(std::strtoul(cl.c_str(), NULL, 10));
                if (maxBodySize != kUnlimitedBodySize && _contentLength > maxBodySize) {
                    setError(413, "Payload Too Large", "413 Payload Too Large");
                    return true;
                }
            }

            std::string te = getHeader("Transfer-Encoding");
            if (toLower(te).find("chunked") != std::string::npos) {
                _isChunked = true;
            }

            _rawBuffer = bodyPart;
        }
    }

    if (_headersParsed && !_bodyParsed) {
        if (_isChunked) {
            if (maxBodySize != kUnlimitedBodySize && _rawBuffer.size() > maxBodySize + 64) {
                setError(413, "Payload Too Large", "413 Payload Too Large");
                return true;
            }
            while (true) {
                size_t pos = _rawBuffer.find("\r\n");
                if (pos == std::string::npos) {
                    if (_rawBuffer.size() > 32) {
                        setError(400, "Bad Request", "400 Bad Request");
                        return true;
                    }
                    break;
                }

                std::string hexSize = _rawBuffer.substr(0, pos);
                size_t chunkSize = 0;
                std::stringstream ss;
                ss << std::hex << hexSize;
                ss >> chunkSize;

                if (ss.fail()) {
                    setError(400, "Bad Request", "400 Bad Request");
                    return true;
                }

                if (maxBodySize == kUnlimitedBodySize && chunkSize > kMaxChunkSize) {
                    setError(413, "Payload Too Large", "413 Payload Too Large");
                    return true;
                }

                if (_rawBuffer.size() < pos + 2 + chunkSize + 2) {
                    break;
                }

                if (chunkSize == 0) {
                    _bodyParsed = true;
                    _rawBuffer.erase(0, pos + 4);
                    break;
                }

                if (maxBodySize != kUnlimitedBodySize && _body.size() + chunkSize > maxBodySize) {
                    setError(413, "Payload Too Large", "413 Payload Too Large");
                    return true;
                }

                _body.append(_rawBuffer.substr(pos + 2, chunkSize));
                _rawBuffer.erase(0, pos + 2 + chunkSize + 2);
            }
        } else {
            if (maxBodySize != kUnlimitedBodySize && _rawBuffer.size() > maxBodySize) {
                setError(413, "Payload Too Large", "413 Payload Too Large");
                return true;
            }

            if (_rawBuffer.size() >= _contentLength) {
                _body = _rawBuffer.substr(0, _contentLength);
                _bodyParsed = true;
            }
        }
    }

    return hasError() || _bodyParsed || (_headersParsed && _contentLength == 0 && !_isChunked);
}

void HttpRequest::parseRequestLine(const std::string &line) {
    std::stringstream ss(line);
    ss >> _method >> _uri >> _version;

    if (_method.empty() || _uri.empty() || _version.empty() || _uri[0] != '/') {
        setError(400, "Bad Request", "400 Bad Request");
        return;
    }

    if (_uri.size() > kMaxUriSize) {
        setError(414, "URI Too Long", "414 URI Too Long");
        return;
    }

    if (_version != "HTTP/1.0" && _version != "HTTP/1.1") {
        setError(505, "HTTP Version Not Supported", "505 HTTP Version Not Supported");
        return;
    }

    size_t queryPos = _uri.find('?');
    if (queryPos != std::string::npos) {
        _path = _uri.substr(0, queryPos);
        _queryString = _uri.substr(queryPos + 1);
    } else {
        _path = _uri;
        _queryString = "";
    }
}

void HttpRequest::parseHeader(const std::string &line) {
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        size_t first = value.find_first_not_of(" \t");
        if (first != std::string::npos) {
            value = value.substr(first);
        }
        size_t last = value.find_last_not_of(" \t");
        if (last != std::string::npos) {
            value = value.substr(0, last + 1);
        }

        std::string lowered = toLower(key);
        if ((lowered == "host" || lowered == "content-length") && !getHeader(key).empty()) {
            setError(400, "Bad Request", "400 Bad Request");
            return;
        }

        _headers[key] = value;
    }
}

void HttpRequest::setError(int code, const std::string &reason, const std::string &body) {
    _errorCode = code;
    _errorReason = reason;
    _errorBody = body;
}

bool HttpRequest::isComplete() const {
    return _headersParsed && (_bodyParsed || (_contentLength == 0 && !_isChunked));
}

bool HttpRequest::hasError() const {
    return _errorCode != 0;
}

const std::string &HttpRequest::getMethod() const { return _method; }
const std::string &HttpRequest::getUri() const { return _uri; }
const std::string &HttpRequest::getPath() const { return _path; }
const std::string &HttpRequest::getQueryString() const { return _queryString; }
const std::string &HttpRequest::getVersion() const { return _version; }
const std::string &HttpRequest::getBody() const { return _body; }

int HttpRequest::getErrorCode() const { return _errorCode; }
const std::string &HttpRequest::getErrorReason() const { return _errorReason; }
const std::string &HttpRequest::getErrorBody() const { return _errorBody; }

const std::map<std::string, std::string> &HttpRequest::getHeaders() const {
    return _headers;
}

std::string HttpRequest::getHeader(const std::string &key) const {
    std::string wanted = toLower(key);
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin(); it != _headers.end(); ++it) {
        if (toLower(it->first) == wanted) {
            return it->second;
        }
    }
    return "";
}
