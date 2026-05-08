#
# rfone_host — HydraSDR RFOne driver. Same upstream as libhydrasdr but a
# distinct repo. Also needs pthreads-win32 on Windows.
#
add_cmake_project(rfone_host
    GIT_REPOSITORY https://github.com/hydrasdr/rfone_host
    GIT_TAG        main
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_rfone_host.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_rfone_host_DEPENDS libusb)
if (WIN32)
    list(APPEND DEP_rfone_host_DEPENDS pthreads)
endif ()

# Note: the actual library file name produced by rfone_host upstream needs
# verification on first build — best guesses listed in priority order.
sdrpp_emit_imported_config(rfone_host
    LIB_NAMES   hydrasdr_rfone hydrasdr_rf1 hydrasdr
    DLL_NAMES   hydrasdr_rfone.dll hydrasdr_rf1.dll hydrasdr.dll
    HEADER      hydrasdr.h
)
