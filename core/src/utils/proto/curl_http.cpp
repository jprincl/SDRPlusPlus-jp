#include "curl_http.h"

#include <stdexcept>
#include <utility>

#include <utils/curl_init.h>

namespace net::http {

    namespace {
        struct ResponseBuffer {
            std::string body;
            size_t maxBody = 0;
            bool overflow = false;
        };

        struct SListGuard {
            curl_slist* list = nullptr;

            ~SListGuard() {
                if (list) {
                    curl_slist_free_all(list);
                }
            }

            SListGuard(const SListGuard&) = delete;
            SListGuard& operator=(const SListGuard&) = delete;
        };

        struct EasyGuard {
            CURL* handle = nullptr;

            explicit EasyGuard(CURL* handle) : handle(handle) {}

            ~EasyGuard() {
                if (handle) {
                    curl_easy_cleanup(handle);
                }
            }

            EasyGuard(const EasyGuard&) = delete;
            EasyGuard& operator=(const EasyGuard&) = delete;
        };

        size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
            size_t total = size * nmemb;
            auto* buffer = static_cast<ResponseBuffer*>(userdata);

            if (buffer->maxBody > 0 && buffer->body.size() + total > buffer->maxBody) {
                buffer->overflow = true;
                return 0;
            }

            buffer->body.append(ptr, total);
            return total;
        }

        void setOption(CURL* handle, CURLoption option, long value, const char* name) {
            CURLcode rc = curl_easy_setopt(handle, option, value);
            if (rc != CURLE_OK) {
                throw std::runtime_error(std::string("curl_easy_setopt(") + name + ") failed: " + curl_easy_strerror(rc));
            }
        }
    }

    CurlResponse curlGet(const std::string& url, const CurlRequestOptions& options) {
        EasyGuard easy(curl::make_easy());
        if (!easy.handle) {
            throw std::runtime_error("curl_easy_init failed");
        }

        char error[CURL_ERROR_SIZE] = {};
        ResponseBuffer buffer;
        buffer.maxBody = options.maxBody;

        CURLcode rc = curl_easy_setopt(easy.handle, CURLOPT_ERRORBUFFER, error);
        if (rc != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_setopt(CURLOPT_ERRORBUFFER) failed: ") + curl_easy_strerror(rc));
        }

        rc = curl_easy_setopt(easy.handle, CURLOPT_URL, url.c_str());
        if (rc != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_setopt(CURLOPT_URL) failed: ") + curl_easy_strerror(rc));
        }

        rc = curl_easy_setopt(easy.handle, CURLOPT_WRITEFUNCTION, writeBody);
        if (rc != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_setopt(CURLOPT_WRITEFUNCTION) failed: ") + curl_easy_strerror(rc));
        }

        rc = curl_easy_setopt(easy.handle, CURLOPT_WRITEDATA, &buffer);
        if (rc != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_setopt(CURLOPT_WRITEDATA) failed: ") + curl_easy_strerror(rc));
        }

        setOption(easy.handle, CURLOPT_CONNECTTIMEOUT_MS, options.connectTimeoutMs, "CURLOPT_CONNECTTIMEOUT_MS");
        if (options.timeoutMs > 0) {
            setOption(easy.handle, CURLOPT_TIMEOUT_MS, options.timeoutMs, "CURLOPT_TIMEOUT_MS");
        }
        setOption(easy.handle, CURLOPT_MAXREDIRS, options.maxRedirects, "CURLOPT_MAXREDIRS");

        SListGuard headers;
        for (const auto& [name, value] : options.headers) {
            std::string header = name + ": " + value;
            curl_slist* next = curl_slist_append(headers.list, header.c_str());
            if (!next) {
                throw std::runtime_error("curl_slist_append failed");
            }
            headers.list = next;
        }
        if (headers.list) {
            rc = curl_easy_setopt(easy.handle, CURLOPT_HTTPHEADER, headers.list);
            if (rc != CURLE_OK) {
                throw std::runtime_error(std::string("curl_easy_setopt(CURLOPT_HTTPHEADER) failed: ") + curl_easy_strerror(rc));
            }
        }

        rc = curl_easy_perform(easy.handle);
        if (rc != CURLE_OK) {
            if (buffer.overflow) {
                throw std::runtime_error("HTTP response exceeded maximum body size");
            }
            const char* detail = error[0] ? error : curl_easy_strerror(rc);
            throw std::runtime_error(std::string("curl_easy_perform failed: ") + detail);
        }

        CurlResponse response;
        response.body = std::move(buffer.body);

        rc = curl_easy_getinfo(easy.handle, CURLINFO_RESPONSE_CODE, &response.status);
        if (rc != CURLE_OK) {
            throw std::runtime_error(std::string("curl_easy_getinfo(CURLINFO_RESPONSE_CODE) failed: ") + curl_easy_strerror(rc));
        }

        char* effectiveUrl = nullptr;
        rc = curl_easy_getinfo(easy.handle, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
        if (rc == CURLE_OK && effectiveUrl) {
            response.effectiveUrl = effectiveUrl;
        }

        return response;
    }

}
