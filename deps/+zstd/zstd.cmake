#
# zstd — CMakeLists lives in build/cmake/, hence SOURCE_SUBDIR.
#
sdrpp_dep_get_linkage_option_bools(zstd _zstd_build_shared _zstd_build_static)
if (_zstd_build_shared)
    set(_zstd_imported_target zstd::libzstd_shared)
else ()
    set(_zstd_imported_target zstd::libzstd_static)
endif ()

set(_zstd_cmake_args
    -DZSTD_BUILD_PROGRAMS=OFF
    -DZSTD_BUILD_TESTS=OFF
    -DZSTD_LEGACY_SUPPORT=OFF
    -DZSTD_BUILD_SHARED=${_zstd_build_shared}
    -DZSTD_BUILD_STATIC=${_zstd_build_static}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

if (MSVC)
    list(APPEND _zstd_cmake_args -DZSTD_USE_STATIC_RUNTIME=ON)
endif ()

add_cmake_project(zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    SOURCE_SUBDIR build/cmake
    CMAKE_ARGS
        ${_zstd_cmake_args}
)

sdrpp_validate_dep(zstd
    TARGET           ${_zstd_imported_target}
    LIB_NAMES        zstd zstd_static libzstd_static
    STATIC_LIB_NAMES zstd zstd_static libzstd_static
    SHARED_LIB_NAMES zstd
    DLL_NAMES        zstd.dll
    HEADER           zstd.h
    REQUIRES_CONFIG)
