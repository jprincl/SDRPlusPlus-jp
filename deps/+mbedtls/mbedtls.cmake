#
# Mbed-TLS — TLS backend for the Android libcurl build.
#
# Only Android pulls this in (via DEP_libcurl_DEPENDS=mbedtls on Android in
# deps/+libcurl/libcurl.cmake). Other platforms use Schannel, Secure Transport,
# or system OpenSSL. The recipe stays platform-agnostic so SDRPP_DEP_FORCE_*
# overrides on other platforms still work.
#
# Pinned to v3.6.2 — the LTS line. Upstream does not ship CMake config
# packages, so the imported target is synthesized by sdrpp_emit_imported_config
# (one per library: mbedtls/mbedx509/mbedcrypto). Curl finds them via the
# legacy MBEDTLS_* variables we feed in libcurl.cmake.
#
add_cmake_project(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        v3.6.2
    GIT_SHALLOW    ON
    CMAKE_ARGS
        -DENABLE_PROGRAMS=OFF
        -DENABLE_TESTING=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

sdrpp_validate_dep(mbedtls
    LIB_NAMES   mbedtls
    HEADER      version.h
    INCLUDE_SUBDIR mbedtls)
