#
# pthreads4w - POSIX threads compatibility layer for Windows. Required
# transitively by libbladeRF, libhydrasdr and the libairspy* / libhackrf
# forks that call pthread_* APIs.
#
# Built from the PThreads4W 3.0.0 NMake project with our Windows-ARM and
# Makefile fixes. Skipped entirely on POSIX where libc provides pthreads.
#
# Installs a real CMake package config at
#   <prefix>/lib/cmake/pthreads4w/pthreads4w-config.cmake
# defining the imported target pthreads4w::pthreadVC3.
#

if (NOT WIN32)
    return()
endif ()

include(ExternalProject)

find_program(_pthreads_nmake NAMES nmake nmake.exe)
if (NOT _pthreads_nmake)
    message(FATAL_ERROR "pthreads: nmake.exe not found. Run from an MSVC developer shell.")
endif ()

sdrpp_dep_builds_shared(pthreads _pthreads_builds_shared)

file(TO_NATIVE_PATH "${SDRPP_DEPS_INSTALL_PREFIX}" _pthreads_destroot)
set(_pthreads_nmake_args "DESTROOT=${_pthreads_destroot}")

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND _pthreads_nmake_args BUILD_RELEASE=1)
endif ()

if (NOT _pthreads_builds_shared)
    list(APPEND _pthreads_nmake_args BUILD_STATIC=1)
endif ()

# Resolve the target architecture in the form the PThreads4W NMakefile
# expects (AMD64 / x86 / ARM / ARM64). Prefer the explicit Visual Studio
# generator platform; fall back to pointer size for older generators that
# don't set it.
set(_pthreads_arch_probe "${CMAKE_GENERATOR_PLATFORM}")
if (NOT _pthreads_arch_probe)
    set(_pthreads_arch_probe "${CMAKE_VS_PLATFORM_NAME}")
endif ()
if (NOT _pthreads_arch_probe)
    set(_pthreads_arch_probe "$ENV{VSCMD_ARG_TGT_ARCH}")
endif ()
string(TOUPPER "${_pthreads_arch_probe}" _pthreads_arch_probe)
if (_pthreads_arch_probe MATCHES "^(ARM64|AARCH64)$")
    set(_pthreads_target_arch ARM64)
elseif (_pthreads_arch_probe MATCHES "^ARM")
    set(_pthreads_target_arch ARM)
elseif (_pthreads_arch_probe MATCHES "^(WIN32|X86)$")
    set(_pthreads_target_arch x86)
elseif (_pthreads_arch_probe MATCHES "^(X64|AMD64)$")
    set(_pthreads_target_arch AMD64)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_pthreads_target_arch AMD64)
else ()
    set(_pthreads_target_arch x86)
endif ()

set(_pthreads_env
    ${CMAKE_COMMAND} -E env
    "PROCESSOR_ARCHITECTURE=${_pthreads_target_arch}"
    "PLATFORM=${_pthreads_target_arch}"
    "TARGET_CPU=${_pthreads_target_arch}")

set(_pthreads_build_command
    ${_pthreads_env}
    "${_pthreads_nmake}" /nologo /f Makefile ${_pthreads_nmake_args} all)
sdrpp_wrap_traced_command(_pthreads_build_command
    "dep_pthreads:build" "<SOURCE_DIR>" ${_pthreads_build_command})

set(_pthreads_install_command
    ${_pthreads_env}
    "${_pthreads_nmake}" /nologo /f Makefile ${_pthreads_nmake_args} install)
sdrpp_wrap_traced_command(_pthreads_install_command
    "dep_pthreads:install" "<SOURCE_DIR>" ${_pthreads_install_command})

set(_pthreads_patch_env
    ${CMAKE_COMMAND} -E env "GIT_CEILING_DIRECTORIES=<SOURCE_DIR>/..")
set(_pthreads_patch_command
    ${_pthreads_patch_env} ${PATCH_CMD} "${CMAKE_CURRENT_LIST_DIR}/patches/fix-arm-macro.patch"
    COMMAND ${_pthreads_patch_env} ${PATCH_CMD} "${CMAKE_CURRENT_LIST_DIR}/patches/fix-arm64-version_rc.patch"
    COMMAND ${_pthreads_patch_env} ${PATCH_CMD} "${CMAKE_CURRENT_LIST_DIR}/patches/fix-pthread_getname_np.patch"
    COMMAND ${_pthreads_patch_env} ${PATCH_CMD} "${CMAKE_CURRENT_LIST_DIR}/patches/fix-install.patch"
    COMMAND ${_pthreads_patch_env} ${PATCH_CMD} "${CMAKE_CURRENT_LIST_DIR}/patches/whitespace_in_path.patch"
    COMMAND ${_pthreads_patch_env} ${PATCH_CMD} "${CMAKE_CURRENT_LIST_DIR}/patches/use-md.patch")

ExternalProject_Add(dep_pthreads
    URL https://downloads.sourceforge.net/project/pthreads4w/pthreads4w-code-v3.0.0.zip
    URL_HASH SHA512=49e541b66c26ddaf812edb07b61d0553e2a5816ab002edc53a38a897db8ada6d0a096c98a9af73a8f40c94283df53094f76b429b09ac49862465d8697ed20013
    DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/pthreads
    BUILD_IN_SOURCE TRUE
    PATCH_COMMAND ${_pthreads_patch_command}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${_pthreads_build_command}
    INSTALL_COMMAND
        ${_pthreads_install_command}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SDRPP_DEPS_INSTALL_PREFIX}/lib/cmake/pthreads4w
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_LIST_DIR}/pthreads4w-config.cmake
            ${SDRPP_DEPS_INSTALL_PREFIX}/lib/cmake/pthreads4w/pthreads4w-config.cmake
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${SDRPP_DEPS_INSTALL_PREFIX}/lib/pkgconfig
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${SDRPP_DEPS_INSTALL_PREFIX}/share/pkgconfig
    USES_TERMINAL_PATCH ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
    USES_TERMINAL_BUILD ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
    USES_TERMINAL_INSTALL ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
)

sdrpp_validate_dep(pthreads
    PACKAGE_NAME pthreads4w
    TARGET       pthreads4w::pthreadVC3
    LIB_NAMES    pthreadVC3
    DLL_NAMES    pthreadVC3.dll
    HEADER       pthread.h
    REQUIRES_CONFIG)

# libbladeRF's bundled FindLibPThreadsWin32.cmake gates on the presence of a
# COPYING.LIB file (a relic of the pthreads-win32 v2 source tree). pthreads4w
# v3 does not ship it. Write the stub into a build-tree compatibility shim, not
# into the installed dependency prefix.
set(_pthreads_win32_compat_dir "${SDRPP_DEPS_COMPAT_DIR}/pthreads-win32")
file(MAKE_DIRECTORY "${_pthreads_win32_compat_dir}")
file(WRITE "${_pthreads_win32_compat_dir}/COPYING.LIB"
"This stub file exists to satisfy libbladeRF's FindLibPThreadsWin32.cmake,
which uses the presence of COPYING.LIB as a sentinel for pthreads-win32 v2's
source tree. We use pthreads4w v3, which is licensed under the Apache 2.0
License. See <prefix>/lib/cmake/pthreads4w/ for the real package config.
")
