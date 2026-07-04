#
# libhydrasdr — HydraSDR USB Lite driver. Needs pthreads-win32 on Windows.
#
# Upstream ignores BUILD_SHARED_LIBS and builds/installs BOTH library types by
# default (ENABLE_SHARED_LIB / ENABLE_STATIC_LIB are independent options, both
# ON). Build only the policy-resolved variant, otherwise the unused shared lib
# lands in the destdir prefix and leaks into packaging (the .deb ships every
# lib/*.so* from the deps prefix).
sdrpp_dep_get_linkage_option_bools(libhydrasdr _libhydrasdr_enable_shared _libhydrasdr_enable_static)

add_cmake_project(libhydrasdr
    GIT_REPOSITORY https://github.com/hydrasdr/hydrasdr-host
    # main @ 2026-05-31; bump when intentional.
    GIT_TAG        6e4014fe8db88b31a7c0f22faf042b63b4295d89
    GIT_SHALLOW    OFF
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libhydrasdr.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DENABLE_SHARED_LIB=${_libhydrasdr_enable_shared}
        -DENABLE_STATIC_LIB=${_libhydrasdr_enable_static}
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
    # Upstream's static target is named `hydrasdr_static` and installs with
    # that name on Windows (so the import lib `hydrasdr.lib` and the static
    # archive don't collide). On UNIX the same target overrides OUTPUT_NAME
    # back to `hydrasdr`, producing `libhydrasdr.a` alongside `libhydrasdr.so*`.
    # List both basenames so find_library matches either install layout.
    STATIC_LIB_NAMES hydrasdr_static hydrasdr
    SHARED_LIB_NAMES hydrasdr
    DLL_NAMES        hydrasdr.dll
    HEADER           hydrasdr.h
    INCLUDE_SUBDIR libhydrasdr
    COMPILE_DEFINITIONS ${_libhydrasdr_compile_definitions}
    PACKAGE_DEPENDENCIES ${_libhydrasdr_package_dependencies}
    LINK_LIBRARIES ${_libhydrasdr_link_libraries}
)
