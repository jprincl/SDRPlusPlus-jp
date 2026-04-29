#include "http.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <inttypes.h>
#include <stdexcept>
#include <thread>
#include <vector>

namespace net::http {
    namespace {
        std::string trimAscii(const std::string& value) {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) { return {}; }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        bool equalsIgnoreAsciiCase(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) { return false; }
            for (size_t i = 0; i < a.size(); i++) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        bool containsHeaderToken(const std::string& value, const std::string& token) {
            size_t start = 0;
            while (start <= value.size()) {
                const size_t end = value.find(',', start);
                std::string part = trimAscii(value.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start));
                if (equalsIgnoreAsciiCase(part, token)) {
                    return true;
                }
                if (end == std::string::npos) { break; }
                start = end + 1;
            }
            return false;
        }

        bool parseContentLength(const std::string& value, size_t& length) {
            if (value.empty()) { return false; }
            try {
                size_t consumed = 0;
                length = std::stoull(value, &consumed);
                return consumed == value.size();
            }
            catch (...) {
                return false;
            }
        }

        int remainingMsOrThrow(std::chrono::steady_clock::time_point deadline, const char* what) {
            using namespace std::chrono;
            const auto remaining = duration_cast<milliseconds>(deadline - steady_clock::now()).count();
            if (remaining <= 0) {
                throw std::runtime_error(std::string("http: ") + what + " timed out");
            }
            return static_cast<int>(std::min<int64_t>(remaining, 1000));
        }

        std::string requestPathFor(const url::HttpHostPort& endpoint) {
            if (endpoint.path.empty()) { return "/"; }
            if (endpoint.path[0] == '?' || endpoint.path[0] == '#') {
                return "/" + endpoint.path;
            }
            return endpoint.path;
        }

        void sendAll(std::shared_ptr<Socket>& sock, const std::string& data,
                     std::chrono::steady_clock::time_point deadline) {
            size_t sent = 0;
            while (sent < data.size()) {
                int n = sock->send(reinterpret_cast<const uint8_t*>(data.data()) + sent, data.size() - sent);
                if (n > 0) {
                    sent += static_cast<size_t>(n);
                    continue;
                }
                if (!sock->isOpen()) {
                    throw std::runtime_error("http: request send failed");
                }
                remainingMsOrThrow(deadline, "request send");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        ResponseHeader recvResponseHeader(std::shared_ptr<Socket>& sock,
                                          std::chrono::steady_clock::time_point deadline) {
            std::string data;
            while (sock->isOpen()) {
                std::string line;
                int n = sock->recvline(line, 8192, remainingMsOrThrow(deadline, "header read"));
                if (n <= 0) {
                    throw std::runtime_error("http: response header read failed");
                }
                if (line == "\r" || line.empty()) {
                    break;
                }
                data += line + "\n";
                if (data.size() > 64 * 1024) {
                    throw std::runtime_error("http: response header too large");
                }
            }

            ResponseHeader header;
            header.deserialize(data);
            return header;
        }

        void appendBytes(std::shared_ptr<Socket>& sock, std::string& body, size_t count, size_t maxBody,
                         std::chrono::steady_clock::time_point deadline) {
            if (body.size() + count > maxBody) {
                throw std::runtime_error("http: response body too large");
            }

            std::vector<uint8_t> buf(1024);
            while (count > 0) {
                const size_t want = std::min(buf.size(), count);
                int n = sock->recv(buf.data(), want, false, remainingMsOrThrow(deadline, "body read"));
                if (n <= 0) {
                    throw std::runtime_error("http: response body read failed");
                }
                body.append(reinterpret_cast<const char*>(buf.data()), n);
                count -= static_cast<size_t>(n);
            }
        }

        std::string recvChunkedBody(std::shared_ptr<Socket>& sock, size_t maxBody,
                                    std::chrono::steady_clock::time_point deadline) {
            std::string body;
            while (true) {
                std::string line;
                int n = sock->recvline(line, 8192, remainingMsOrThrow(deadline, "chunk header read"));
                if (n <= 0) {
                    throw std::runtime_error("http: chunk header read failed");
                }

                line = trimAscii(line);
                const auto extension = line.find(';');
                if (extension != std::string::npos) { line.resize(extension); }

                size_t consumed = 0;
                size_t chunkSize = 0;
                try {
                    chunkSize = std::stoull(line, &consumed, 16);
                }
                catch (...) {
                    throw std::runtime_error("http: malformed chunk size");
                }
                if (consumed != line.size()) {
                    throw std::runtime_error("http: malformed chunk size");
                }

                if (chunkSize == 0) {
                    while (true) {
                        std::string trailer;
                        int trailerLen = sock->recvline(trailer, 8192, remainingMsOrThrow(deadline, "chunk trailer read"));
                        if (trailerLen <= 0) {
                            throw std::runtime_error("http: chunk trailer read failed");
                        }
                        if (trailer == "\r" || trailer.empty()) { break; }
                    }
                    break;
                }

                appendBytes(sock, body, chunkSize, maxBody, deadline);

                std::string crlf;
                int crlfLen = sock->recvline(crlf, 2, remainingMsOrThrow(deadline, "chunk terminator read"));
                if (crlfLen <= 0 || crlf != "\r") {
                    throw std::runtime_error("http: malformed chunk terminator");
                }
            }
            return body;
        }

        std::string recvBody(std::shared_ptr<Socket>& sock, ResponseHeader& header, size_t maxBody,
                             std::chrono::steady_clock::time_point deadline) {
            if (containsHeaderToken(getHeaderValue(header, "Transfer-Encoding"), "chunked")) {
                return recvChunkedBody(sock, maxBody, deadline);
            }

            size_t contentLength = 0;
            const bool hasContentLength = parseContentLength(getHeaderValue(header, "Content-Length"), contentLength);
            if (hasContentLength && contentLength > maxBody) {
                throw std::runtime_error("http: response body too large");
            }

            std::string body;
            std::vector<uint8_t> buf(1024);
            while (body.size() < maxBody) {
                size_t want = buf.size();
                if (hasContentLength) {
                    if (body.size() >= contentLength) { break; }
                    want = std::min(want, contentLength - body.size());
                }

                int n = sock->recv(buf.data(), want, false, remainingMsOrThrow(deadline, "body read"));
                if (n < 0) {
                    throw std::runtime_error("http: response body read failed");
                }
                if (n == 0) {
                    if (hasContentLength && body.size() < contentLength) {
                        throw std::runtime_error("http: response body ended before Content-Length");
                    }
                    break;
                }
                body.append(reinterpret_cast<const char*>(buf.data()), n);
            }

            if (!hasContentLength && body.size() >= maxBody) {
                throw std::runtime_error("http: response body too large");
            }
            return body;
        }
    }

    std::string MessageHeader::serialize() {
        std::string data;

        // Add start line
        data += serializeStartLine() + "\r\n";

        // Add fields
        for (const auto& [key, value] : fields) {
            data += key + ": " + value + "\r\n";
        }

        // Add terminator
        data += "\r\n";

        return data;
    }

    void MessageHeader::deserialize(const std::string& data) {
        // Clear existing fields
        fields.clear();

        // Parse first line
        std::string line;
        int offset = readLine(data, line);
        deserializeStartLine(line);

        // Parse fields
        while (offset < data.size()) {
            // Read line
            offset = readLine(data, line, offset);

            // If empty line, the header is done
            if (line.empty()) { break; }

            // Read until first ':' for the key
            int klen = 0;
            for (; klen < line.size(); klen++) {
                if (line[klen] == ':') { break; }
            }
            
            // Find offset of value
            int voff = klen + 1;
            for (; voff < line.size(); voff++) {
                if (line[voff] != ' ' && line[voff] != '\t') { break; }
            }

            // Save field
            fields[line.substr(0, klen)] = line.substr(voff);
        }
    }

    std::map<std::string, std::string>& MessageHeader::getFields() {
        return fields;
    }

    const std::map<std::string, std::string>& MessageHeader::getFields() const {
        return fields;
    }

    bool MessageHeader::hasField(const std::string& name) {
        return fields.find(name) != fields.end();
    }

    std::string MessageHeader::getField(const std::string name) {
        // TODO: Check if exists
        return fields[name];
    }

    void MessageHeader::setField(const std::string& name, const std::string& value) {
        fields[name] = value;
    }

    void MessageHeader::clearField(const std::string& name) {
        // TODO: Check if exists (but maybe no error?)
        fields.erase(name);
    }

    int MessageHeader::readLine(const std::string& str, std::string& line, int start) {
        // Get line length
        int len = 0;
        bool cr = false;
        for (int i = start; i < str.size(); i++) {
            if (str[i] == '\n') {
                if (len && str[i-1] == '\r') { cr = true; }
                break;
            }
            len++;
        }

        // Copy line
        line = str.substr(start, len - (cr ? 1:0));
        return start + len + 1;
    }

    RequestHeader::RequestHeader(Method method, std::string uri, std::string host) {
        this->method = method;
        this->uri = uri;
        setField("Host", host);
    }

    RequestHeader::RequestHeader(const std::string& data) {
        deserialize(data);
    }

    Method RequestHeader::getMethod() {
        return method;
    }

    void RequestHeader::setMethod(Method method) {
        this->method = method;
    }

    std::string RequestHeader::getURI() {
        return uri;
    }

    void RequestHeader::setURI(const std::string& uri) {
        this->uri = uri;
    }

    void RequestHeader::deserializeStartLine(const std::string& data) {
        // TODO
    }

    std::string RequestHeader::serializeStartLine() {
        // TODO: Allow to specify version
        return MethodStrings[method] + " " + uri + " HTTP/1.1";
    }

    ResponseHeader::ResponseHeader(StatusCode statusCode) {
        this->statusCode = statusCode;
        if (StatusCodeStrings.find(statusCode) != StatusCodeStrings.end()) {
            this->statusString = StatusCodeStrings[statusCode];
        }
        else {
            this->statusString = "UNKNOWN";
        }
    }

    ResponseHeader::ResponseHeader(StatusCode statusCode, const std::string& statusString) {
        this->statusCode = statusCode;
        this->statusString = statusString;
    }

    ResponseHeader::ResponseHeader(const std::string& data) {
        deserialize(data);
    }

    StatusCode ResponseHeader::getStatusCode() const {
        return statusCode;
    }

    void ResponseHeader::setStatusCode(StatusCode statusCode) {
        this->statusCode = statusCode;
    }

    std::string ResponseHeader::getStatusString() const {
        return statusString;
    }

    void ResponseHeader::setStatusString(const std::string& statusString) {
        this->statusString = statusString;
    }

    void ResponseHeader::deserializeStartLine(const std::string& data) {
        // Parse version
        int offset = 0;
        for (; offset < data.size(); offset++) {
            if (data[offset] == ' ') { break; }
        }
        // TODO: Error if null length
        // TODO: Parse version

        // Skip spaces
        for (; offset < data.size(); offset++) {
            if (data[offset] != ' ' && data[offset] != '\t') { break; }
        }
        
        // Parse status code
        int codeOffset = offset;
        for (; offset < data.size(); offset++) {
            if (data[offset] == ' ') { break; }
        }
        // TODO: Error if null length
        statusCode = (StatusCode)std::stoi(data.substr(codeOffset, codeOffset - offset));
    
        // Skip spaces
        for (; offset < data.size(); offset++) {
            if (data[offset] != ' ' && data[offset] != '\t') { break; }
        }
        
        // Parse status string
        int stringOffset = offset;
        for (; offset < data.size(); offset++) {
            if (data[offset] == ' ') { break; }
        }
        // TODO: Error if null length (maybe?)
        statusString = data.substr(stringOffset, stringOffset - offset);
    }

    std::string ResponseHeader::serializeStartLine() {
        char buf[1024];
        sprintf(buf, "%d %s", (int)statusCode, statusString.c_str());
        return buf;
    }

    ChunkHeader::ChunkHeader(size_t length) {
        this->length = length;
    }

    ChunkHeader::ChunkHeader(const std::string& data) {
        deserialize(data);
    }

    std::string ChunkHeader::serialize() {
        char buf[64];
        sprintf(buf, "%zX\r\n", length);
        return buf;
    }

    void ChunkHeader::deserialize(const std::string& data) {
        // Parse length
        int offset = 0;
        for (; offset < data.size(); offset++) {
            if (data[offset] == ' ') { break; }
        }
        // TODO: Error if null length
        length = (StatusCode)std::stoull(data.substr(0, offset), nullptr, 16);

        // TODO: Parse rest
    }

    size_t ChunkHeader::getLength() {
        return length;
    }

    void ChunkHeader::setLength(size_t length) {
        this->length = length;
    }

    Client::Client(std::shared_ptr<Socket> sock) {
        this->sock = sock;
    }

    int Client::sendRequestHeader(RequestHeader& req) {
        return sock->sendstr(req.serialize());
    }

    int Client::recvRequestHeader(RequestHeader& req, int timeout) {
        // Non-blocking mode not alloowed
        if (!timeout) { return -1; }

        // Read response
        std::string respData;
        int err = recvHeader(respData, timeout);
        if (err) { return err; }

        // Deserialize
        req.deserialize(respData);
        return 0; // Might wanna return size instead
    }

    int Client::sendResponseHeader(ResponseHeader& resp) {
        return sock->sendstr(resp.serialize());
    }

    int Client::recvResponseHeader(ResponseHeader& resp, int timeout) {
        // Non-blocking mode not alloowed
        if (!timeout) { return -1; }

        // Read response
        std::string respData;
        int err = recvHeader(respData, timeout);
        if (err) { return err; }

        // Deserialize
        resp.deserialize(respData);
        return 0; // Might wanna return size instead
    }

    int Client::sendChunkHeader(ChunkHeader& chdr) {
        return sock->sendstr(chdr.serialize());
    }

    int Client::recvChunkHeader(ChunkHeader& chdr, int timeout) {
        std::string respData;
        int err = sock->recvline(respData, 0, timeout);
        if (err <= 0) { return err; }
        if (respData[respData.size()-1] == '\r') {
            respData.pop_back();
        }
        chdr.deserialize(respData);
        return 0;
    }

    int Client::recvHeader(std::string& data, int timeout) {
        while (sock->isOpen()) {
            std::string line;
            int ret = sock->recvline(line, 0, timeout);
            if (line == "\r") { break; }
            if (ret <= 0) { return ret; }
            data += line + "\n";
        }
        return 0;
    }

    std::string hostHeaderFor(const url::HttpHostPort& endpoint) {
        return endpoint.port == 80 ? endpoint.host : (endpoint.host + ":" + std::to_string(endpoint.port));
    }

    std::string getHeaderValue(const MessageHeader& header, const std::string& name) {
        for (const auto& [key, value] : header.getFields()) {
            if (equalsIgnoreAsciiCase(key, name)) {
                return trimAscii(value);
            }
        }
        return {};
    }

    bool isRedirectStatus(int status) {
        return status == STATUS_CODE_MOVED_PERMANENTLY ||
               status == STATUS_CODE_FOUND ||
               status == STATUS_CODE_SEE_OTHER ||
               status == STATUS_CODE_TEMP_REDIRECT ||
               status == STATUS_CODE_PERMANENT_REDIRECT;
    }

    std::optional<url::HttpHostPort> resolveRedirectLocation(
        const url::HttpHostPort& current,
        const ResponseHeader& response) {
        if (!isRedirectStatus(static_cast<int>(response.getStatusCode()))) {
            return std::nullopt;
        }
        const std::string location = getHeaderValue(response, "Location");
        if (location.empty()) {
            return std::nullopt;
        }
        return url::resolveHttpLocation(current, location);
    }

    Response get(const url::HttpHostPort& endpoint, const RequestOptions& options) {
        url::HttpHostPort current = endpoint;
        current.path = requestPathFor(current);

        for (int redirectsLeft = options.maxRedirects; redirectsLeft >= 0; redirectsLeft--) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(options.timeoutMs);
            auto sock = net::connect(current.host, current.port);

            RequestHeader request(METHOD_GET, requestPathFor(current), hostHeaderFor(current));
            request.setField("Connection", "close");
            for (const auto& [key, value] : options.headers) {
                request.setField(key, value);
            }

            sendAll(sock, request.serialize(), deadline);

            Response response;
            response.header = recvResponseHeader(sock, deadline);
            response.endpoint = current;

            const int status = static_cast<int>(response.header.getStatusCode());
            if (isRedirectStatus(status)) {
                auto redirected = resolveRedirectLocation(current, response.header);
                if (redirectsLeft == 0 || !redirected) {
                    sock->close();
                    throw std::runtime_error("http: redirect cannot be followed: " +
                                             getHeaderValue(response.header, "Location"));
                }
                sock->close();
                current = *redirected;
                current.path = requestPathFor(current);
                continue;
            }

            response.body = recvBody(sock, response.header, options.maxBody, deadline);
            sock->close();
            return response;
        }

        throw std::runtime_error("http: redirect limit exceeded");
    }
}
