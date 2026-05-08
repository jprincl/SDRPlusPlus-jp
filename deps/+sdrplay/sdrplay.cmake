#
# SDRplay API — closed-source binary blob. We download the SDR++ project's
# pre-extracted bundle (same source as the existing CI), then copy headers
# and the matching architecture's import lib + DLL into the shared prefix.
#
# Layout inside the zip is `SDRplay/inc/` + `SDRplay/x64/`. Adjust the
# arch_dir mapping if SDRplay ever ships ARM64 variants.
#

include(ExternalProject)

if (CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_arch_dir x64)
elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_GENERATOR_PLATFORM STREQUAL "")
    set(_arch_dir x86)
elseif (CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
    # SDRplay's Windows API does not currently ship ARM64 binaries.
    message(WARNING "SDRplay: no ARM64 binary available, skipping")
    return()
endif ()

set(_prefix ${${PROJECT_NAME}_DEP_INSTALL_PREFIX})

ExternalProject_Add(dep_sdrplay
    URL                 https://www.sdrpp.org/SDRplay.zip
    # URL_HASH SHA256=<TODO: pin after first verified build>
    DOWNLOAD_DIR        ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/sdrplay
    SOURCE_DIR          ${CMAKE_CURRENT_BINARY_DIR}/sources/sdrplay
    BINARY_DIR          ${CMAKE_CURRENT_BINARY_DIR}/builds/sdrplay
    CONFIGURE_COMMAND   ""
    BUILD_COMMAND       ""
    INSTALL_COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_prefix}/include/SDRplay
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/API/inc
                                               ${_prefix}/include/SDRplay
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_prefix}/lib
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_prefix}/bin
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
                <SOURCE_DIR>/API/${_arch_dir}/sdrplay_api.lib
                ${_prefix}/lib/sdrplay_api.lib
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
                <SOURCE_DIR>/API/${_arch_dir}/sdrplay_api.dll
                ${_prefix}/bin/sdrplay_api.dll
)

sdrpp_emit_imported_config(sdrplay
    TARGET      sdrplay::sdrplay_api
    LIB_NAMES   sdrplay_api
    DLL_NAMES   sdrplay_api.dll
    HEADER      sdrplay_api.h
    INCLUDE_SUBDIR SDRplay
)
