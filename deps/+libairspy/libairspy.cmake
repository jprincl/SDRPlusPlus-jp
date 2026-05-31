#
# libairspy — Airspy R2/Mini USB driver (airspyone_host).
#
add_cmake_project(libairspy
    GIT_REPOSITORY https://github.com/airspy/airspyone_host
    # master @ 2026-05-31; bump when intentional.
    GIT_TAG        c6721000f19601512e9ba6b0340e5d9ced22a900
    GIT_SHALLOW    OFF
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libairspy.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libairspy_DEPENDS libusb)
if (WIN32)
    list(APPEND DEP_libairspy_DEPENDS pthreads)
endif ()

set(_libairspy_compile_definitions "")
set(_libairspy_package_dependencies "")
set(_libairspy_link_libraries "")
sdrpp_dep_builds_shared(libairspy _libairspy_builds_shared)
if (NOT _libairspy_builds_shared)
    list(APPEND _libairspy_compile_definitions AIRSPY_STATIC)
    list(APPEND _libairspy_package_dependencies libusb)
    list(APPEND _libairspy_link_libraries libusb::libusb)
    if (WIN32)
        list(APPEND _libairspy_package_dependencies pthreads4w)
        list(APPEND _libairspy_link_libraries pthreads4w::pthreadVC3)
    endif ()
endif ()

# Defensive Config.cmake — airspyone_host upstream may or may not install one,
# depending on the version. Ours wins on our prefix and is self-relative.
sdrpp_emit_imported_config(libairspy
    LIB_NAMES        airspy
    # Upstream's static target is named `airspy_static` and installs with that
    # name on Windows (so the import lib `airspy.lib` and the static archive
    # don't collide). On UNIX the same target overrides OUTPUT_NAME back to
    # `airspy`, producing `libairspy.a` alongside `libairspy.so*`. List both
    # basenames so find_library matches either install layout.
    STATIC_LIB_NAMES airspy_static airspy
    SHARED_LIB_NAMES airspy
    DLL_NAMES        airspy.dll
    HEADER           airspy.h
    INCLUDE_SUBDIR libairspy
    COMPILE_DEFINITIONS ${_libairspy_compile_definitions}
    PACKAGE_DEPENDENCIES ${_libairspy_package_dependencies}
    LINK_LIBRARIES ${_libairspy_link_libraries}
)
