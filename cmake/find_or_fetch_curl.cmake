# Resolves libcurl into the CURL::libcurl imported target.
#
# Mode is selected by the SDRPP_CURL_SOURCE cache variable:
#   - "system"  : find_package(CURL) and verify WebSocket support is compiled in.
#   - "bundled" : FetchContent a shared libcurl with a platform-native TLS
#                 backend (Schannel on Windows, Secure Transport on macOS,
#                 MbedTLS on Android, OpenSSL elsewhere).
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
    # Force shared linkage of curl regardless of the project's BUILD_SHARED_LIBS.
    # Set as a normal (non-cache) variable scoped to this function so we don't
    # change linkage for any other dependency. add_subdirectory'd subprojects
    # invoked by FetchContent_MakeAvailable inherit this scope.
    set(BUILD_SHARED_LIBS ON)

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
    set(CURL_USE_LIBPSL      OFF CACHE BOOL "" FORCE)
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

function(sdrpp_stage_bundled_curl TARGET_NAME)
    if(NOT "${SDRPP_CURL_SOURCE}" STREQUAL "bundled")
        return()
    endif()

    if(MSVC AND TARGET ${TARGET_NAME} AND TARGET libcurl_shared)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:libcurl_shared>
                $<TARGET_FILE_DIR:${TARGET_NAME}>
            COMMENT "Staging libcurl.dll next to ${TARGET_NAME}"
            VERBATIM)
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
