#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include "../ascii.h"
#include "../net.h"
#include "../url.h"

namespace net::http {
    enum Method {
        METHOD_OPTIONS,
        METHOD_GET,
        METHOD_HEAD,
        METHOD_POST,
        METHOD_PUT,
        METHOD_DELETE,
        METHOD_TRACE,
        METHOD_CONNECT
    };

    struct MethodNamePair {
        Method method;
        std::string_view name;
    };

    inline constexpr MethodNamePair MethodStrings[] = {
        { METHOD_OPTIONS,   "OPTIONS" },
        { METHOD_GET,       "GET"     },
        { METHOD_HEAD,      "HEAD"    },
        { METHOD_POST,      "POST"    },
        { METHOD_PUT,       "PUT"     },
        { METHOD_DELETE,    "DELETE"  },
        { METHOD_TRACE,     "TRACE"   },
        { METHOD_CONNECT,   "CONNECT" }
    };

    inline constexpr std::string_view methodName(Method m) {
        for (const auto& e : MethodStrings) {
            if (e.method == m) { return e.name; }
        }
        return {};
    }

    enum StatusCode {
        STATUS_CODE_INVALID                     = 0,

        STATUS_CODE_CONTINUE                    = 100,
        STATUS_CODE_SWITCH_PROTO                = 101,

        STATUS_CODE_OK                          = 200,
        STATUS_CODE_CREATED                     = 201,
        STATUS_CODE_ACCEPTED                    = 202,
        STATUS_CODE_NON_AUTH_INFO               = 203,
        STATUS_CODE_NO_CONTENT                  = 204,
        STATUS_CODE_RESET_CONTENT               = 205,
        STATUS_CODE_PARTIAL_CONTENT             = 206,
        
        STATUS_CODE_MULTIPLE_CHOICES            = 300,
        STATUS_CODE_MOVED_PERMANENTLY           = 301,
        STATUS_CODE_FOUND                       = 302,
        STATUS_CODE_SEE_OTHER                   = 303,
        STATUS_CODE_NOT_MODIFIED                = 304,
        STATUS_CODE_USE_PROXY                   = 305,
        STATUS_CODE_TEMP_REDIRECT               = 307,
        STATUS_CODE_PERMANENT_REDIRECT          = 308,

        STATUS_CODE_BAD_REQUEST                 = 400,
        STATUS_CODE_UNAUTHORIZED                = 401,
        STATUS_CODE_PAYMENT_REQUIRED            = 402,
        STATUS_CODE_FORBIDDEN                   = 403,
        STATUS_CODE_NOT_FOUND                   = 404,
        STATUS_CODE_METHOD_NOT_ALLOWED          = 405,
        STATUS_CODE_NOT_ACCEPTABLE              = 406,
        STATUS_CODE_PROXY_AUTH_REQ              = 407,
        STATUS_CODE_REQUEST_TIEMOUT             = 408,
        STATUS_CODE_CONFLICT                    = 409,
        STATUS_CODE_GONE                        = 410,
        STATUS_CODE_LENGTH_REQUIRED             = 411,
        STATUS_CODE_PRECONDITION_FAILED         = 412,
        STATUS_CODE_REQ_ENTITY_TOO_LARGE        = 413,
        STATUS_CODE_REQ_URI_TOO_LONG            = 414,
        STATUS_CODE_UNSUPPORTED_MEDIA_TYPE      = 415,
        STATUS_CODE_REQ_RANGE_NOT_SATISFIABLE   = 416,
        STATUS_CODE_EXPECTATION_FAILED          = 417,
        STATUS_CODE_IM_A_TEAPOT                 = 418,
        STATUS_CODE_ENHANCE_YOUR_CALM           = 420,

        STATUS_CODE_INTERNAL_SERVER_ERROR       = 500,
        STATUS_CODE_NOT_IMPLEMENTED             = 501,
        STATUS_CODE_BAD_GATEWAY                 = 502,
        STATUS_CODE_SERVICE_UNAVAILABLE         = 503,
        STATUS_CODE_GATEWAY_TIMEOUT             = 504,
        STATUS_CODE_HTTP_VERSION_UNSUPPORTED    = 505
    };

    struct StatusCodeNamePair {
        StatusCode code;
        std::string_view name;
    };

    inline constexpr StatusCodeNamePair StatusCodeStrings[] = {
        { STATUS_CODE_CONTINUE                 , "CONTINUE"                 },
        { STATUS_CODE_SWITCH_PROTO             , "SWITCH_PROTO"             },

        { STATUS_CODE_OK                       , "OK"                       },
        { STATUS_CODE_CREATED                  , "CREATED"                  },
        { STATUS_CODE_ACCEPTED                 , "ACCEPTED"                 },
        { STATUS_CODE_NON_AUTH_INFO            , "NON_AUTH_INFO"            },
        { STATUS_CODE_NO_CONTENT               , "NO_CONTENT"               },
        { STATUS_CODE_RESET_CONTENT            , "RESET_CONTENT"            },
        { STATUS_CODE_PARTIAL_CONTENT          , "PARTIAL_CONTENT"          },

        { STATUS_CODE_MULTIPLE_CHOICES         , "MULTIPLE_CHOICES"         },
        { STATUS_CODE_MOVED_PERMANENTLY        , "MOVED_PERMANENTLY"        },
        { STATUS_CODE_FOUND                    , "FOUND"                    },
        { STATUS_CODE_SEE_OTHER                , "SEE_OTHER"                },
        { STATUS_CODE_NOT_MODIFIED             , "NOT_MODIFIED"             },
        { STATUS_CODE_USE_PROXY                , "USE_PROXY"                },
        { STATUS_CODE_TEMP_REDIRECT            , "TEMP_REDIRECT"            },
        { STATUS_CODE_PERMANENT_REDIRECT       , "PERMANENT_REDIRECT"       },

        { STATUS_CODE_BAD_REQUEST              , "BAD_REQUEST"              },
        { STATUS_CODE_UNAUTHORIZED             , "UNAUTHORIZED"             },
        { STATUS_CODE_PAYMENT_REQUIRED         , "PAYMENT_REQUIRED"         },
        { STATUS_CODE_FORBIDDEN                , "FORBIDDEN"                },
        { STATUS_CODE_NOT_FOUND                , "NOT_FOUND"                },
        { STATUS_CODE_METHOD_NOT_ALLOWED       , "METHOD_NOT_ALLOWED"       },
        { STATUS_CODE_NOT_ACCEPTABLE           , "NOT_ACCEPTABLE"           },
        { STATUS_CODE_PROXY_AUTH_REQ           , "PROXY_AUTH_REQ"           },
        { STATUS_CODE_REQUEST_TIEMOUT          , "REQUEST_TIEMOUT"          },
        { STATUS_CODE_CONFLICT                 , "CONFLICT"                 },
        { STATUS_CODE_GONE                     , "GONE"                     },
        { STATUS_CODE_LENGTH_REQUIRED          , "LENGTH_REQUIRED"          },
        { STATUS_CODE_PRECONDITION_FAILED      , "PRECONDITION_FAILED"      },
        { STATUS_CODE_REQ_ENTITY_TOO_LARGE     , "REQ_ENTITY_TOO_LARGE"     },
        { STATUS_CODE_REQ_URI_TOO_LONG         , "REQ_URI_TOO_LONG"         },
        { STATUS_CODE_UNSUPPORTED_MEDIA_TYPE   , "UNSUPPORTED_MEDIA_TYPE"   },
        { STATUS_CODE_REQ_RANGE_NOT_SATISFIABLE, "REQ_RANGE_NOT_SATISFIABLE"},
        { STATUS_CODE_EXPECTATION_FAILED       , "EXPECTATION_FAILED"       },
        { STATUS_CODE_IM_A_TEAPOT              , "IM_A_TEAPOT"              },
        { STATUS_CODE_ENHANCE_YOUR_CALM        , "ENHANCE_YOUR_CALM"        },

        { STATUS_CODE_INTERNAL_SERVER_ERROR    , "INTERNAL_SERVER_ERROR"    },
        { STATUS_CODE_NOT_IMPLEMENTED          , "NOT_IMPLEMENTED"          },
        { STATUS_CODE_BAD_GATEWAY              , "BAD_GATEWAY"              },
        { STATUS_CODE_SERVICE_UNAVAILABLE      , "SERVICE_UNAVAILABLE"      },
        { STATUS_CODE_GATEWAY_TIMEOUT          , "GATEWAY_TIMEOUT"          },
        { STATUS_CODE_HTTP_VERSION_UNSUPPORTED , "HTTP_VERSION_UNSUPPORTED" }
    };

    inline constexpr std::string_view statusCodeName(StatusCode c) {
        for (const auto& e : StatusCodeStrings) {
            if (e.code == c) { return e.name; }
        }
        return {};
    }

    /**
     * HTTP Message Header
     */
    class MessageHeader {
    public:
        // Field names are looked up case-insensitively per RFC 7230 §3.2.
        using FieldMap = std::map<std::string, std::string, ascii::CaseInsensitiveLess>;

        /**
         * Serialize header to string.
         * @return Header in string form.
         */
        std::string serialize();

        /**
         * Deserialize header from string.
         * @param data Header in string form.
         */
        void deserialize(const std::string& data);

        /**
         * Get field list. Iteration order is case-insensitive lexicographic.
         */
        FieldMap& getFields();
        const FieldMap& getFields() const;

        /**
         * Check if a field exists in the header.
         * @return True if the field exists, false otherwise.
         */
        bool hasField(const std::string& name) const;

        /**
         * Get field value.
         * @param name Name of the field.
         * @return Field value, or empty string if not present.
         */
        std::string getField(const std::string& name) const;

        /**
         * Set field.
         * @param name Field name.
         * @param value Field value.
         */
        void setField(const std::string& name, const std::string& value);

        /**
         * Delete field.
         * @param name Field name.
         */
        void clearField(const std::string& name);

    private:
        int readLine(const std::string& str, std::string& line, int start = 0);
        virtual std::string serializeStartLine() = 0;
        virtual void deserializeStartLine(const std::string& data) = 0;
        FieldMap fields;
    };

    /**
     * HTTP Request Header
     */
    class RequestHeader : public MessageHeader {
    public:
        RequestHeader() = default;

        /**
         * Create request header from the mandatory parameters.
         * @param method HTTP Method.
         * @param uri URI of request.
         * @param host Server host passed in the 'Host' field.
         */
        RequestHeader(Method method, std::string uri, std::string host);

        /**
         * Create request header from its serialized string form.
         * @param data Request header in string form.
         */
        RequestHeader(const std::string& data);
        
        /**
         * Get HTTP Method.
         * @return HTTP Method.
         */
        Method getMethod();
        void setMethod(Method method);
        std::string getURI();
        void setURI(const std::string& uri);

    private:
        void deserializeStartLine(const std::string& data);
        std::string serializeStartLine();

        Method method = METHOD_GET;
        std::string uri = "/";
    };

    class ResponseHeader : public MessageHeader {
    public:
        ResponseHeader() = default;
        ResponseHeader(StatusCode statusCode);
        ResponseHeader(StatusCode statusCode, const std::string& statusString);
        ResponseHeader(const std::string& data);

        StatusCode getStatusCode() const;
        void setStatusCode(StatusCode statusCode);
        std::string getStatusString() const;
        void setStatusString(const std::string& statusString);

    private:
        void deserializeStartLine(const std::string& data);
        std::string serializeStartLine();

        StatusCode statusCode = STATUS_CODE_INVALID;
        std::string statusString;
    };

    class ChunkHeader {
    public:
        ChunkHeader() = default;
        ChunkHeader(size_t length);
        ChunkHeader(const std::string& data);

        std::string serialize();
        void deserialize(const std::string& data);

        size_t getLength();
        void setLength(size_t length);

    private:
        size_t length = 0;
    };

    class Client {
    public:
        Client() {}
        Client(std::shared_ptr<Socket> sock);

        int sendRequestHeader(RequestHeader& req);
        int recvRequestHeader(RequestHeader& req, int timeout = -1);
        int sendResponseHeader(ResponseHeader& resp);
        int recvResponseHeader(ResponseHeader& resp, int timeout = -1);
        int sendChunkHeader(ChunkHeader& chdr);
        int recvChunkHeader(ChunkHeader& chdr, int timeout = -1);

    private:
        int recvHeader(std::string& data, int timeout = -1);
        std::shared_ptr<Socket> sock;

    };

    struct RequestOptions {
        int timeoutMs = 5000;
        int maxRedirects = 0;
        size_t maxBody = 64 * 1024;
        std::map<std::string, std::string> headers;
    };

    struct Response {
        ResponseHeader header;
        std::string body;
        url::HttpHostPort endpoint;
    };

    std::string hostHeaderFor(const url::HttpHostPort& endpoint);
    std::string getHeaderValue(const MessageHeader& header, const std::string& name);
    bool isRedirectStatus(int status);
    std::optional<url::HttpHostPort> resolveRedirectLocation(
        const url::HttpHostPort& current,
        const ResponseHeader& response);
    Response get(const url::HttpHostPort& endpoint, const RequestOptions& options = RequestOptions());

    
}
