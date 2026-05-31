#
# libperseus-sdr — Microtelecom Perseus driver, via bubnikv's fork of
# Alex Rouma's CMake repackaging. The bubnikv fork carries the GCC 14
# fixes Rouma's autotools->CMake refactor regressed against Microtelecom
# upstream (missing <unistd.h>, unguarded Sleep(1), variadic-macro
# trailing comma in dbgprintf/errorset). See README in that fork for
# the full fork chain and per-file diffs.
#
add_cmake_project(libperseus-sdr
    GIT_REPOSITORY https://github.com/bubnikv/libperseus-sdr
    # master @ 2026-05-31 (= GCC 14 fixes + README commit); bump when intentional.
    GIT_TAG        ffb471cea64d4f2b8403e67bbaecda3117c8a690
    GIT_SHALLOW    OFF
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libperseus_sdr.cmake
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libperseus-sdr_DEPENDS libusb)

set(_libperseus_sdr_package_dependencies "")
set(_libperseus_sdr_link_libraries "")
sdrpp_dep_builds_shared(libperseus-sdr _libperseus_sdr_builds_shared)
if (NOT _libperseus_sdr_builds_shared)
    list(APPEND _libperseus_sdr_package_dependencies libusb)
    list(APPEND _libperseus_sdr_link_libraries libusb::libusb)
endif ()

sdrpp_emit_imported_config(libperseus-sdr
    LIB_NAMES   perseus-sdr
    DLL_NAMES   perseus-sdr.dll
    HEADER      perseus-sdr.h
    INCLUDE_SUBDIR perseus-sdr
    PACKAGE_DEPENDENCIES ${_libperseus_sdr_package_dependencies}
    LINK_LIBRARIES ${_libperseus_sdr_link_libraries}
)
