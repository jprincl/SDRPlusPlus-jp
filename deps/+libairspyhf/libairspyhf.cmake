#
# libairspyhf — Airspy HF+ USB driver.
#
add_cmake_project(libairspyhf
    GIT_REPOSITORY https://github.com/airspy/airspyhf
    GIT_TAG        master
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libairspyhf.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libairspyhf_DEPENDS libusb)
if (WIN32)
    list(APPEND DEP_libairspyhf_DEPENDS pthreads)
endif ()

set(_libairspyhf_compile_definitions "")
set(_libairspyhf_package_dependencies "")
set(_libairspyhf_link_libraries "")
sdrpp_dep_builds_shared(libairspyhf _libairspyhf_builds_shared)
if (NOT _libairspyhf_builds_shared)
    list(APPEND _libairspyhf_compile_definitions STATIC_AIRSPYHFPLUS)
    list(APPEND _libairspyhf_package_dependencies libusb)
    list(APPEND _libairspyhf_link_libraries libusb::libusb)
    if (WIN32)
        list(APPEND _libairspyhf_package_dependencies pthreads4w)
        list(APPEND _libairspyhf_link_libraries pthreads4w::pthreadVC3)
    endif ()
endif ()

sdrpp_emit_imported_config(libairspyhf
    LIB_NAMES        airspyhf
    # Upstream's static target keeps the `_static` suffix on Windows but
    # overrides OUTPUT_NAME back to `airspyhf` on UNIX, producing
    # `libairspyhf.a` alongside `libairspyhf.so*`. List both basenames so
    # find_library matches either install layout.
    STATIC_LIB_NAMES airspyhf_static airspyhf
    SHARED_LIB_NAMES airspyhf
    DLL_NAMES        airspyhf.dll
    HEADER           airspyhf.h
    INCLUDE_SUBDIR libairspyhf
    COMPILE_DEFINITIONS ${_libairspyhf_compile_definitions}
    PACKAGE_DEPENDENCIES ${_libairspyhf_package_dependencies}
    LINK_LIBRARIES ${_libairspyhf_link_libraries}
)
