#include "url.h"

#include <cctype>
#include <cstdlib>

namespace url {

    static const std::string HTTP_SCHEME = "http://";

    static bool equalsIgnoreAsciiCase(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) { return false; }
        for (size_t i = 0; i < a.size(); i++) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    static bool startsWithIgnoreAsciiCase(const std::string& value, const std::string& prefix) {
        return value.size() >= prefix.size() &&
               equalsIgnoreAsciiCase(value.substr(0, prefix.size()), prefix);
    }

    static std::string trimAscii(const std::string& value) {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) { return {}; }
        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    static std::string normalizeHttpPath(std::string path) {
        if (path.empty()) { return "/"; }
        if (path[0] == '?' || path[0] == '#') { return "/" + path; }
        return path;
    }

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
        if (!startsWithIgnoreAsciiCase(url, HTTP_SCHEME)) {
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

    std::optional<HttpHostPort> resolveHttpLocation(
        const HttpHostPort& current,
        const std::string& location) {
        std::string loc = trimAscii(location);
        if (loc.empty()) { return std::nullopt; }

        if (startsWithIgnoreAsciiCase(loc, "http://")) {
            auto parsed = parseHttpHostPort(loc);
            if (!parsed) { return std::nullopt; }
            parsed->path = normalizeHttpPath(parsed->path);
            return parsed;
        }

        if (loc.size() >= 2 && loc[0] == '/' && loc[1] == '/') {
            auto parsed = parseHttpHostPort("http:" + loc);
            if (!parsed) { return std::nullopt; }
            parsed->path = normalizeHttpPath(parsed->path);
            return parsed;
        }

        if (loc[0] == '/') {
            return HttpHostPort{ current.host, current.port, loc };
        }

        if (loc[0] == '?') {
            std::string base = normalizeHttpPath(current.path);
            const auto query = base.find_first_of("?#");
            if (query != std::string::npos) { base.resize(query); }
            return HttpHostPort{ current.host, current.port, base + loc };
        }

        std::string base = normalizeHttpPath(current.path);
        const auto query = base.find_first_of("?#");
        if (query != std::string::npos) { base.resize(query); }
        const auto slash = base.find_last_of('/');
        base = slash == std::string::npos ? "/" : base.substr(0, slash + 1);
        return HttpHostPort{ current.host, current.port, base + loc };
    }

}
