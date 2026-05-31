#
# libmirisdr — Mirics MSi2500/MSi3101 USB driver (ericek111 fork of libmirisdr-5).
# Pure-C library; needs libusb-1.0 and pthreads-win32 on Windows.
#
add_cmake_project(libmirisdr
    GIT_REPOSITORY https://github.com/ericek111/libmirisdr-5
    # master @ 2026-05-31; bump when intentional.
    GIT_TAG        b4e6ffaf81fac1b74646537cdfee29580817ef74
    GIT_SHALLOW    OFF
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libmirisdr.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libmirisdr_DEPENDS libusb)
if (WIN32)
    list(APPEND DEP_libmirisdr_DEPENDS pthreads)
endif ()

sdrpp_emit_imported_config(libmirisdr
    LIB_NAMES   mirisdr
    DLL_NAMES   mirisdr.dll
    HEADER      mirisdr.h
)
