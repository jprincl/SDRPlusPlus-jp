#
# spdlog — header-only-by-default logging library. Required by librfnm
# (and other libraries that use it for logging). Pure-CMake upstream
# with proper Config.cmake.
#
add_cmake_project(spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    CMAKE_ARGS
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_BUILD_TESTS=OFF
        -DSPDLOG_INSTALL=ON
        -DSPDLOG_BUILD_SHARED=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
