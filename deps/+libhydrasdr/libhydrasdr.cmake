#
# libhydrasdr — HydraSDR USB Lite driver. Needs pthreads-win32 on Windows.
#
add_cmake_project(libhydrasdr
    GIT_REPOSITORY https://github.com/hydrasdr/hydrasdr-host
    GIT_TAG        main
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libhydrasdr.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libhydrasdr_DEPENDS libusb)
if (WIN32)
    list(APPEND DEP_libhydrasdr_DEPENDS pthreads)
endif ()

sdrpp_emit_imported_config(libhydrasdr
    LIB_NAMES   hydrasdr
    DLL_NAMES   hydrasdr.dll
    HEADER      hydrasdr.h
    INCLUDE_SUBDIR libhydrasdr
)
