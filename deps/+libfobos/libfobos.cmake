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

sdrpp_emit_imported_config(libfobos
    LIB_NAMES   fobos
    DLL_NAMES   fobos.dll
    HEADER      fobos.h
)
