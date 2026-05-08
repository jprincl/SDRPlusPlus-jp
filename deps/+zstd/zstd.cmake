#
# zstd — CMakeLists lives in build/cmake/, hence SOURCE_SUBDIR.
# Pure-C library; safe to share between Release and Debug consumers,
# so it's listed in DEP_DEBUG_EXCLUDES in the parent CMakeLists.
#
add_cmake_project(zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    SOURCE_SUBDIR build/cmake
    CMAKE_ARGS
        -DZSTD_BUILD_PROGRAMS=OFF
        -DZSTD_BUILD_TESTS=OFF
        -DZSTD_LEGACY_SUPPORT=OFF
        -DZSTD_BUILD_SHARED=ON
        -DZSTD_BUILD_STATIC=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
