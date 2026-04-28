#include "url.h"

#include <cctype>
#include <cstdlib>

namespace url {

    static const std::string HTTP_SCHEME = "http://";

    std::string decode(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); i++) {
            const char c = in[i];
            if (c == '%' && i + 2 < in.size()
                && std::isxdigit(static_cast<unsigned char>(in[i + 1]))
                && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
                const char hex[3] = { in[i + 1], in[i + 2], 0 };
                out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
                i += 2;
            }
            else if (c == '+') {
                out.push_back(' ');
            }
            else {
                out.push_back(c);
            }
        }
        return out;
    }

    std::optional<HttpHostPort> parseHttpHostPort(const std::string& url) {
        if (url.compare(0, HTTP_SCHEME.size(), HTTP_SCHEME) != 0) {
            return std::nullopt;
        }
        std::string rest = url.substr(HTTP_SCHEME.size());

        std::string pathPart = "/";
        auto suffix = rest.find_first_of("/?#");
        if (suffix != std::string::npos) {
            pathPart = rest.substr(suffix);
            rest = rest.substr(0, suffix);
        }

        if (rest.empty()) {
            return std::nullopt;
        }

        auto colon = rest.find(':');
        HttpHostPort r;
        r.path = pathPart;
        if (colon == std::string::npos) {
            r.host = rest;
            r.port = 80;
            return r;
        }
        r.host = rest.substr(0, colon);
        if (r.host.empty()) {
            return std::nullopt;
        }
        try {
            r.port = std::stoi(rest.substr(colon + 1));
        }
        catch (...) {
            return std::nullopt;
        }
        return r;
    }

    std::optional<HttpHostPort> splitHostPort(const std::string& hostPort) {
        auto colon = hostPort.find(':');
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        HttpHostPort r;
        r.path = "/";
        r.host = hostPort.substr(0, colon);
        if (r.host.empty()) {
            return std::nullopt;
        }
        try {
            r.port = std::stoi(hostPort.substr(colon + 1));
        }
        catch (...) {
            return std::nullopt;
        }
        return r;
    }

}
