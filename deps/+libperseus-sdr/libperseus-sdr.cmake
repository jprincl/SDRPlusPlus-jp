#
# libperseus-sdr — Microtelecom Perseus driver (Alex Rouma's fork).
#
add_cmake_project(libperseus-sdr
    GIT_REPOSITORY https://github.com/AlexandreRouma/libperseus-sdr
    GIT_TAG        master
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libperseus_sdr.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libperseus-sdr_DEPENDS libusb)

sdrpp_emit_imported_config(libperseus-sdr
    LIB_NAMES   perseus-sdr
    DLL_NAMES   perseus-sdr.dll
    HEADER      perseus-sdr.h
    INCLUDE_SUBDIR perseus-sdr
)
