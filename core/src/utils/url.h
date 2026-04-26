#pragma once
#include <optional>
#include <string>

namespace url {

    struct HttpHostPort {
        std::string host;
        int port;
    };

    /**
     * Parse an http:// URL into host and port.
     * Strips any path / query / fragment suffix and defaults the port to 80
     * when not present in the URL.
     *
     * IPv4 addresses and hostnames only — IPv6 bracketed URLs are not supported.
     *
     * @return The parsed host and port, or std::nullopt if the input does not
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

}
