#
# libopus — Opus audio codec, used by core's utils/wav writer for the lossy
# demodulated-audio recorder (.opus / Ogg-Opus container). The float encode
# API is used (opus_encode_float), which is enabled by default. Standalone C
# library; the Ogg framing is handled separately via libogg. License:
# BSD-3-Clause.
#
set(_opus_cmake_args
    -DOPUS_BUILD_PROGRAMS=OFF
    -DOPUS_BUILD_TESTING=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

add_cmake_project(opus
    URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
    URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
    CMAKE_ARGS
        ${_opus_cmake_args}
)

# Upstream installs OpusConfig.cmake to lib/cmake/Opus exporting Opus::opus.
# The headers land in include/opus/ (opus.h, opus_defines.h, ...).
sdrpp_validate_dep(opus
    TARGET         Opus::opus
    PACKAGE_NAME   Opus
    LIB_NAMES      opus
    DLL_NAMES      opus.dll
    HEADER         opus.h
    INCLUDE_SUBDIR opus
    REQUIRES_CONFIG)
