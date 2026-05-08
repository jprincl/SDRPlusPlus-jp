#
# librfnm — RFNM SDR driver (Alex Rouma's fork).
#
add_cmake_project(librfnm
    GIT_REPOSITORY https://github.com/AlexandreRouma/librfnm
    GIT_TAG        main
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_librfnm.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_librfnm_DEPENDS libusb spdlog)

sdrpp_emit_imported_config(librfnm
    LIB_NAMES   rfnm librfnm
    DLL_NAMES   rfnm.dll librfnm.dll
    HEADER      librfnm.h
    INCLUDE_SUBDIR librfnm
)
