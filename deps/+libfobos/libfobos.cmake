#
# libfobos — Fobos SDR driver (Alex Rouma's fork).
#
add_cmake_project(libfobos
    GIT_REPOSITORY https://github.com/AlexandreRouma/libfobos
    GIT_TAG        main
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libfobos.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libfobos_DEPENDS libusb)

set(_libfobos_package_dependencies "")
set(_libfobos_link_libraries "")
sdrpp_dep_builds_shared(libfobos _libfobos_builds_shared)
if (NOT _libfobos_builds_shared)
    list(APPEND _libfobos_package_dependencies libusb)
    list(APPEND _libfobos_link_libraries libusb::libusb)
endif ()

sdrpp_emit_imported_config(libfobos
    LIB_NAMES   fobos
    DLL_NAMES   fobos.dll
    HEADER      fobos.h
    PACKAGE_DEPENDENCIES ${_libfobos_package_dependencies}
    LINK_LIBRARIES ${_libfobos_link_libraries}
)
