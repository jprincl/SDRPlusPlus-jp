#
# SDRplay API — closed-source binary blob. Distribution differs per OS:
#   * Windows: the SDR++ project's pre-extracted bundle on sdrpp.org ships
#     headers + per-arch .lib/.dll.
#   * Linux: SDRplay's official Makeself .run installer from sdrplay.com,
#     extracted via 7z (avoids running the EULA-prompting shell wrapper).
#   * macOS: not yet automated — SDRplay distributes a .pkg installer; until
#     that's wired up, this stays a manual step.
#
# All three paths land the artifacts under the shared deps prefix so the
# Config.cmake emitted at the bottom of this file finds them uniformly.
#

include(ExternalProject)

sdrpp_dep_builds_shared(sdrplay _sdrplay_builds_shared)
if (NOT _sdrplay_builds_shared)
    message(FATAL_ERROR "sdrplay is distributed as a shared binary only; remove it from SDRPP_DEP_FORCE_STATIC.")
endif ()

set(_prefix ${SDRPP_DEPS_INSTALL_PREFIX})

if (WIN32)
    # SDRplay's Windows API ships only x86/x64 user-mode binaries and matching
    # x86/x64 kernel drivers. ARM64 Windows cannot load those drivers, so the
    # device never enumerates. Skip on any ARM64 target — the check runs before
    # the x64 branch so CMAKE_SIZEOF_VOID_P=8 on ARM64 doesn't mis-route us
    # into installing an unusable x64 DLL.
    if (CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64")
        message(WARNING "SDRplay: no ARM64 Windows kernel driver available, skipping")
        return()
    elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch_dir x64)
    elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_GENERATOR_PLATFORM STREQUAL "")
        set(_arch_dir x86)
    endif ()

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
elseif (UNIX AND NOT APPLE)
    # SDRplay's Linux installer is a Makeself .run — a shell wrapper around a
    # tar.gz payload. Running it would prompt for an EULA, so extract with 7z
    # instead (same two-pass approach the pre-rework do_build.sh used: pass 1
    # peels the shell wrapper, pass 2 unpacks the inner tar).
    set(_sdrplay_run_version 3.15.2)
    set(_sdrplay_run_basename SDRplay_RSP_API-Linux-${_sdrplay_run_version})
    set(_sdrplay_run_url https://www.sdrplay.com/software/${_sdrplay_run_basename}.run)

    # Inner archive's per-arch directory names mirror `uname -m` on the targets
    # SDRplay supports. CMAKE_SYSTEM_PROCESSOR can come through as either
    # `aarch64`/`arm64` and `x86_64`/`amd64` depending on the toolchain, so
    # normalize both spellings.
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(_sdrplay_arch_dir x86_64)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(_sdrplay_arch_dir aarch64)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^armv7")
        set(_sdrplay_arch_dir armv7l)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i686|i386)$")
        set(_sdrplay_arch_dir i686)
    else ()
        message(FATAL_ERROR "SDRplay: unsupported Linux host architecture '${CMAKE_SYSTEM_PROCESSOR}'")
    endif ()

    find_program(_sdrplay_7z NAMES 7z 7zz 7za)
    if (NOT _sdrplay_7z)
        message(FATAL_ERROR
            "SDRplay (Linux): 7z not found on PATH. "
            "Install p7zip-full (Debian/Ubuntu) or equivalent to extract the SDRplay installer.")
    endif ()

    # The .so inside the installer is versioned major.minor (e.g. .so.3.15);
    # derive it from the installer version so a future SDRplay bump only needs
    # to change _sdrplay_run_version above. The SONAME embedded in the .so is
    # major-only (e.g. libsdrplay_api.so.3) — plugins linked against the API get
    # that as their NEEDED entry, so we must publish a matching symlink for
    # linuxdeploy/ldd to resolve at AppImage bundle time.
    string(REGEX MATCH "^([0-9]+\\.[0-9]+)" _sdrplay_so_mm "${_sdrplay_run_version}")
    string(REGEX MATCH "^([0-9]+)" _sdrplay_so_major "${_sdrplay_run_version}")
    set(_sdrplay_so_versioned libsdrplay_api.so.${_sdrplay_so_mm})
    set(_sdrplay_so_soname libsdrplay_api.so.${_sdrplay_so_major})

    set(_sdrplay_scratch ${CMAKE_CURRENT_BINARY_DIR}/sources/sdrplay/extracted)

    ExternalProject_Add(dep_sdrplay
        URL                 ${_sdrplay_run_url}
        DOWNLOAD_DIR        ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/sdrplay
        DOWNLOAD_NO_EXTRACT TRUE
        SOURCE_DIR          ${CMAKE_CURRENT_BINARY_DIR}/sources/sdrplay
        BINARY_DIR          ${CMAKE_CURRENT_BINARY_DIR}/builds/sdrplay
        CONFIGURE_COMMAND   ""
        BUILD_COMMAND       ""
        INSTALL_COMMAND
                ${CMAKE_COMMAND} -E rm -rf ${_sdrplay_scratch}
            COMMAND
                ${CMAKE_COMMAND} -E make_directory ${_sdrplay_scratch}
            COMMAND
                ${_sdrplay_7z} x -y -o${_sdrplay_scratch}
                    ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/sdrplay/${_sdrplay_run_basename}.run
            COMMAND
                ${_sdrplay_7z} x -y -o${_sdrplay_scratch}
                    ${_sdrplay_scratch}/${_sdrplay_run_basename}
            COMMAND
                ${CMAKE_COMMAND}
                    -DSCRATCH=${_sdrplay_scratch}
                    -DARCH=${_sdrplay_arch_dir}
                    -DPREFIX=${_prefix}
                    -DSO_VERSIONED=${_sdrplay_so_versioned}
                    -DSO_SONAME=${_sdrplay_so_soname}
                    -P ${CMAKE_CURRENT_LIST_DIR}/install_linux.cmake
        USES_TERMINAL_INSTALL ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
    )
else ()
    # macOS: SDRplay ships SDRplayAPI-macos-installer-universal-*.pkg, which
    # would need xar + cpio extraction (or `pkgutil --expand-full`). Until
    # that's automated, fail loudly so the build doesn't silently produce a
    # broken sdrplay_source plugin.
    message(FATAL_ERROR
        "SDRplay: macOS auto-install is not implemented in deps/+sdrplay yet. "
        "Either install the SDRplay API .pkg manually (sudo installer -pkg ... -target /) "
        "and add a system-source path here, or disable OPT_BUILD_SDRPLAY_SOURCE for the macOS build.")
endif ()

sdrpp_emit_imported_config(sdrplay
    TARGET      sdrplay::sdrplay_api
    LIB_NAMES   sdrplay_api
    DLL_NAMES   sdrplay_api.dll
    HEADER      sdrplay_api.h
    INCLUDE_SUBDIR SDRplay
)
