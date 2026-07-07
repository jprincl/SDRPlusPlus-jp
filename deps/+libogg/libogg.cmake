#
# libogg — Ogg bitstream framing, used by core's utils/wav writer to
# encapsulate Opus packets into a standard .opus (Ogg-Opus, RFC 7845) file
# for the lossy audio recorder. Standalone C library, no dependencies.
# License: BSD-3-Clause.
#
set(_libogg_cmake_args
    -DINSTALL_DOCS=OFF
    -DBUILD_TESTING=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

add_cmake_project(libogg
    URL https://github.com/xiph/ogg/releases/download/v1.3.6/libogg-1.3.6.tar.gz
    URL_HASH SHA256=83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638
    CMAKE_ARGS
        ${_libogg_cmake_args}
)

# Upstream installs OggConfig.cmake to lib/cmake/Ogg exporting Ogg::ogg.
sdrpp_validate_dep(libogg
    TARGET         Ogg::ogg
    PACKAGE_NAME   Ogg
    LIB_NAMES      ogg
    DLL_NAMES      ogg.dll
    HEADER         ogg.h
    INCLUDE_SUBDIR ogg
    REQUIRES_CONFIG)
