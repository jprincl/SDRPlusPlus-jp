#
# SDRplay API — closed-source binary blob. We download the SDR++ project's
# pre-extracted bundle (same source as the existing CI), then copy headers
# and the matching architecture's import lib + DLL into the shared prefix.
#
# Layout inside the zip is `SDRplay/inc/` + `SDRplay/x64/`. Adjust the
# arch_dir mapping if SDRplay ever ships ARM64 variants.
#

include(ExternalProject)

sdrpp_dep_builds_shared(sdrplay _sdrplay_builds_shared)
if (NOT _sdrplay_builds_shared)
    message(FATAL_ERROR "sdrplay is distributed as a shared binary only; remove it from SDRPP_DEP_FORCE_STATIC.")
endif ()

# SDRplay's Windows API ships only x86/x64 user-mode binaries and matching
# x86/x64 kernel drivers. ARM64 Windows cannot load those drivers, so the
# device never enumerates. Skip on any ARM64 target — the check runs before
# the x64 branch so CMAKE_SIZEOF_VOID_P=8 on ARM64 doesn't mis-route us into
# installing an unusable x64 DLL.
if (CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
    message(WARNING "SDRplay: no ARM64 Windows kernel driver available, skipping")
    return()
elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_arch_dir x64)
elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_GENERATOR_PLATFORM STREQUAL "")
    set(_arch_dir x86)
endif ()

set(_prefix ${SDRPP_DEPS_INSTALL_PREFIX})

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
    USES_TERMINAL_INSTALL ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
)

sdrpp_emit_imported_config(sdrplay
    TARGET      sdrplay::sdrplay_api
    LIB_NAMES   sdrplay_api
    DLL_NAMES   sdrplay_api.dll
    HEADER      sdrplay_api.h
    INCLUDE_SUBDIR SDRplay
)
