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

set(_libhydrasdr_compile_definitions "")
set(_libhydrasdr_package_dependencies "")
set(_libhydrasdr_link_libraries "")
sdrpp_dep_builds_shared(libhydrasdr _libhydrasdr_builds_shared)
if (NOT _libhydrasdr_builds_shared)
    list(APPEND _libhydrasdr_compile_definitions HYDRASDR_STATIC)
    list(APPEND _libhydrasdr_package_dependencies libusb)
    list(APPEND _libhydrasdr_link_libraries libusb::libusb)
    if (WIN32)
        list(APPEND _libhydrasdr_package_dependencies pthreads4w)
        list(APPEND _libhydrasdr_link_libraries pthreads4w::pthreadVC3)
    endif ()
endif ()

sdrpp_emit_imported_config(libhydrasdr
    LIB_NAMES        hydrasdr
    STATIC_LIB_NAMES hydrasdr_static
    SHARED_LIB_NAMES hydrasdr
    DLL_NAMES        hydrasdr.dll
    HEADER           hydrasdr.h
    INCLUDE_SUBDIR libhydrasdr
    COMPILE_DEFINITIONS ${_libhydrasdr_compile_definitions}
    PACKAGE_DEPENDENCIES ${_libhydrasdr_package_dependencies}
    LINK_LIBRARIES ${_libhydrasdr_link_libraries}
)
