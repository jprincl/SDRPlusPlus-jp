#
# spdlog — header-only-by-default logging library. Required by librfnm
# (and other libraries that use it for logging). Pure-CMake upstream
# with proper Config.cmake.
#
sdrpp_dep_get_linkage_option_bools(spdlog _spdlog_build_shared _spdlog_build_static)

add_cmake_project(spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    CMAKE_ARGS
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_BUILD_TESTS=OFF
        -DSPDLOG_INSTALL=ON
        -DSPDLOG_BUILD_SHARED=${_spdlog_build_shared}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

sdrpp_validate_dep(spdlog
    TARGET         spdlog::spdlog
    LIB_NAMES      spdlog
    DLL_NAMES      spdlog.dll
    HEADER         spdlog.h
    INCLUDE_SUBDIR spdlog
    REQUIRES_CONFIG)
