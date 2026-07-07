#
# libFLAC — FLAC encoder used by core's utils/wav writer (recorder FLAC
# container). Encoder-only, native .flac output: the Ogg-FLAC container is
# never written, so Ogg support is disabled outright and libogg is not a
# dependency (qrp73's port needed FindOgg/FindFLAC modules only because it
# linked an Ogg-enabled libFLAC). License: BSD-3-Clause (library).
#
set(_flac_cmake_args
    -DWITH_OGG=OFF
    -DBUILD_CXXLIBS=OFF
    -DBUILD_PROGRAMS=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_DOCS=OFF
    -DINSTALL_MANPAGES=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

add_cmake_project(flac
    URL https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz
    URL_HASH SHA256=f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920
    PATCH_COMMAND ${CMAKE_COMMAND}
                      -DSRC=<SOURCE_DIR>
                      -P ${CMAKE_CURRENT_LIST_DIR}/patch_flac.cmake
    CMAKE_ARGS
        ${_flac_cmake_args}
)

# Upstream installs flac-config.cmake to lib/cmake/FLAC exporting FLAC::FLAC.
# Static builds carry FLAC__NO_DLL as a PUBLIC compile definition on the
# exported target, so MSVC consumers need no manual define. The library file
# is FLAC.lib / libFLAC.a — the capitalized name matters on case-sensitive
# filesystems (Linux AppImage builds), hence the explicit LIB_NAMES.
sdrpp_validate_dep(flac
    TARGET         FLAC::FLAC
    PACKAGE_NAME   FLAC
    LIB_NAMES      FLAC
    DLL_NAMES      FLAC.dll
    HEADER         stream_encoder.h
    INCLUDE_SUBDIR FLAC
    REQUIRES_CONFIG)
