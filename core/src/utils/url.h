#pragma once
#include <optional>
#include <string>

namespace url {

    struct HttpHostPort {
        std::string host;
        int port;
        std::string path;   // includes any "?query" / "#fragment"; defaults to "/"
    };

    /**
     * Parse an http:// URL into host, port, and path.
     * The port defaults to 80 when not present, and the path defaults to "/"
     * when not present.
     *
     * IPv4 addresses and hostnames only — IPv6 bracketed URLs are not supported.
     *
     * @return The parsed components, or std::nullopt if the input does not
     *         start with "http://" or is otherwise malformed.
     */
    std::optional<HttpHostPort> parseHttpHostPort(const std::string& url);

    /**
     * Split a "host:port" string into host and port.
     *
     * @return The parsed host and port, or std::nullopt if the colon is missing
     *         or the port suffix is not a valid integer.
     */
    std::optional<HttpHostPort> splitHostPort(const std::string& hostPort);

    /**
     * Decode a percent-encoded string (application/x-www-form-urlencoded
     * style): "%XX" hex escapes become the corresponding byte, and "+" is
     * treated as space. Malformed "%" escapes (incomplete or non-hex) are
     * passed through unchanged.
     */
    std::string decode(const std::string& in);

}
