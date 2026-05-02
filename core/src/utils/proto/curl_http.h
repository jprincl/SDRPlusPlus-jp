#pragma once

#include <cstddef>
#include <map>
#include <string>

#include <module.h>

namespace net::http {

    struct CurlRequestOptions {
        int connectTimeoutMs = 15000;
        int timeoutMs = 30000;
        int maxRedirects = 5;
        size_t maxBody = 2 * 1024 * 1024;
        std::map<std::string, std::string> headers;
    };

    struct CurlResponse {
        long status = 0;
        std::string body;
        std::string effectiveUrl;
    };

    // HTTPS-capable GET implemented by core through libcurl. Modules should
    // use this instead of including <curl/curl.h> or linking libcurl directly.
    SDRPP_EXPORT CurlResponse curlGet(const std::string& url, const CurlRequestOptions& options = CurlRequestOptions());

}
