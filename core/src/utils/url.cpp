#include "url.h"

namespace url {

    static const std::string HTTP_SCHEME = "http://";

    std::optional<HttpHostPort> parseHttpHostPort(const std::string& url) {
        if (url.compare(0, HTTP_SCHEME.size(), HTTP_SCHEME) != 0) {
            return std::nullopt;
        }
        std::string rest = url.substr(HTTP_SCHEME.size());

        // Strip path / query / fragment.
        auto suffix = rest.find_first_of("/?#");
        if (suffix != std::string::npos) {
            rest = rest.substr(0, suffix);
        }

        if (rest.empty()) {
            return std::nullopt;
        }

        auto colon = rest.find(':');
        HttpHostPort r;
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
