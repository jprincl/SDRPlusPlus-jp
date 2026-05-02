# Resolves libcurl into the CURL::libcurl imported target.
#
# Mode is selected by the SDRPP_CURL_SOURCE cache variable:
#   - "system"  : find_package(CURL) and verify WebSocket support is compiled in.
#   - "bundled" : FetchContent a static (PIC) libcurl with a platform-native TLS
#                 backend (Schannel on Windows, Secure Transport on macOS,
#                 MbedTLS on Android, OpenSSL elsewhere). Curl is linked
#                 privately into sdrpp_core; plugins use core HTTP/WebSocket
#                 wrappers instead of calling libcurl directly.
#
# No CA bundle is ever shipped. On Android the runtime must point libcurl at
# /system/etc/security/cacerts via CURLOPT_CAPATH (handled in core/utils/curl_init).

include(CheckSymbolExists)
include(FetchContent)

# Pinned versions for bundled mode.
set(_SDRPP_CURL_TAG "curl-8_11_1")           # ≥ 8.11: WebSocket out of experimental.
set(_SDRPP_MBEDTLS_TAG "v3.6.2")             # LTS line for Android bundled mode.

# ---------------------------------------------------------------------------

function(_sdrpp_check_system_curl_websockets)
    # CURL::libcurl is set by find_package; CURL_INCLUDE_DIRS provides the header.
    set(CMAKE_REQUIRED_INCLUDES "${CURL_INCLUDE_DIRS}")
    set(CMAKE_REQUIRED_LIBRARIES CURL::libcurl)
    check_symbol_exists(curl_ws_send "curl/curl.h" SDRPP_CURL_HAS_WS)
    if(NOT SDRPP_CURL_HAS_WS)
        message(FATAL_ERROR
            "System libcurl ${CURL_VERSION_STRING} was built without WebSocket support. "
            "Install a libcurl built with --enable-websockets, or configure with "
            "-DSDRPP_CURL_SOURCE=bundled to fetch a private copy.")
    endif()
endfunction()

# ---------------------------------------------------------------------------

function(_sdrpp_fetch_bundled_curl)
    # Build curl (and on Android, mbedtls) as static + PIC so the object code
    # gets absorbed into libsdrpp_iak_core.so. There's no separate libcurl
    # artifact to install, find via rpath, or ship.
    # Scoped via normal (non-cache) variables; FetchContent_MakeAvailable's
    # add_subdirectory inherits this scope.
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    if(ANDROID)
        # MbedTLS first — curl needs the targets/headers available at config time.
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            mbedtls
            GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
            GIT_TAG        ${_SDRPP_MBEDTLS_TAG}
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(mbedtls)

        # Curl's MbedTLS detection looks for these variables. Pointing them at
        # the FetchContent'd targets/sources lets it skip find_package(MbedTLS).
        set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include" CACHE PATH   "" FORCE)
        set(MBEDTLS_LIBRARY      mbedtls                        CACHE STRING "" FORCE)
        set(MBEDX509_LIBRARY     mbedx509                       CACHE STRING "" FORCE)
        set(MBEDCRYPTO_LIBRARY   mbedcrypto                     CACHE STRING "" FORCE)
    endif()

    # Trim curl down to the protocols we actually use.
    set(BUILD_CURL_EXE       OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING        OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS   OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS      OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL   OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS   ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_FTP     ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_DICT    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TELNET  ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TFTP    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_RTSP    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_SMTP    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_POP3    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_IMAP    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_GOPHER  ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_MQTT    ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_SCP     ON  CACHE BOOL "" FORCE)
    set(CURL_DISABLE_SFTP    ON  CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL      OFF CACHE BOOL "" FORCE)
    set(USE_APPLE_IDN        OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2          OFF CACHE BOOL "" FORCE)
    set(USE_WIN32_IDN        OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2          OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2     OFF CACHE BOOL "" FORCE)
    set(ENABLE_WEBSOCKETS    ON  CACHE BOOL "" FORCE)

    # Per-platform TLS backend.
    if(WIN32)
        set(CURL_USE_SCHANNEL  ON CACHE BOOL "" FORCE)
    elseif(APPLE)
        set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
    elseif(ANDROID)
        set(CURL_USE_MBEDTLS   ON CACHE BOOL "" FORCE)
    else()
        set(CURL_USE_OPENSSL   ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        ${_SDRPP_CURL_TAG}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(curl)

    if(NOT TARGET CURL::libcurl)
        if(TARGET libcurl_shared)
            add_library(CURL::libcurl ALIAS libcurl_shared)
        elseif(TARGET libcurl_static)
            add_library(CURL::libcurl ALIAS libcurl_static)
        else()
            message(FATAL_ERROR "Bundled curl did not create a libcurl target")
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------

if(SDRPP_CURL_SOURCE STREQUAL "system")
    find_package(CURL 8.5 REQUIRED)
    _sdrpp_check_system_curl_websockets()
    message(STATUS "libcurl: system (${CURL_VERSION_STRING})")
elseif(SDRPP_CURL_SOURCE STREQUAL "bundled")
    _sdrpp_fetch_bundled_curl()
    message(STATUS "libcurl: bundled (${_SDRPP_CURL_TAG})")
else()
    message(FATAL_ERROR
        "SDRPP_CURL_SOURCE must be 'system' or 'bundled' (got '${SDRPP_CURL_SOURCE}')")
endif()
