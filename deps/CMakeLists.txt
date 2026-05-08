#
# Top-level CMake project to download, configure and build third-party
# dependencies for SDR++ iak. Adapted from PrusaSlicer's deps/ subsystem.
#
# Each library lives in a +<Name>/ folder containing a single <Name>.cmake
# recipe. Adding a new dependency = drop in a new folder.
#
# Typical use:
#   cmake --preset vs2022-x64 -S deps -B deps/build-vs2022-x64
#   cmake --build deps/build-vs2022-x64 --config Release
#
# Outputs land in <buildDir>/destdir/usr/local. Point the SDR++ build at it:
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
#         -DCMAKE_PREFIX_PATH=<absolute>/deps/build-vs2022-x64/destdir/usr/local \
#         -DSDR_KIT_ROOT=<absolute>/deps/build-vs2022-x64/destdir/usr/local
#
# On MSVC, when CMAKE_BUILD_TYPE=Release a sibling configure is automatically
# spawned in build/_d/ to produce Debug-mode variants of the libraries that
# need them (those with C++ ABI / STL types in their interface). Pure-C libs
# are reused from the Release build.
#

cmake_minimum_required(VERSION 3.16)  # Ubuntu focal floor
project(sdrpp_deps)

option(BUILD_SHARED_LIBS "Build shared libraries instead of static" ON)
set(${PROJECT_NAME}_PACKAGE_EXCLUDES "" CACHE STRING "Exclude packages matching this regex pattern")

if (MSVC)
    option(DEP_DEBUG "Also build Debug variants for MSVC C++ libraries" ON)
endif ()

if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    cmake_policy(SET CMP0135 NEW)  # URL timestamp = extraction time, so re-extracts on URL change
endif ()

include(${PROJECT_SOURCE_DIR}/cmake/AddCMakeProject.cmake)

# Discover all +<Name> folders containing a <Name>.cmake recipe.
macro(list_projects result curdir)
    file(GLOB children RELATIVE ${curdir} ${curdir}/*)
    set(_dirlist "")
    foreach (child ${children})
        if (IS_DIRECTORY ${curdir}/${child})
            string(REGEX MATCH "^\\+([a-zA-Z0-9_-]+)" _is_pkg ${child})
            if (_is_pkg AND EXISTS ${curdir}/${child}/${CMAKE_MATCH_1}.cmake)
                list(APPEND _dirlist ${CMAKE_MATCH_1})
            endif ()
        endif ()
    endforeach ()
    set(${result} ${_dirlist})
endmacro()

function(dep_message mode msg)
    if (NOT DEP_MESSAGES_WRITTEN)
        message(${mode} "${msg}")
    endif ()
endfunction()

# Architecture sniff (Windows MSVC).
if (MSVC)
    if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
        dep_message(STATUS "Detected 64-bit MSVC compiler")
    elseif ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
        dep_message(STATUS "Detected 32-bit MSVC compiler")
    else ()
        dep_message(FATAL_ERROR "Unable to detect target architecture")
    endif ()
else ()
    set(DEP_CMAKE_OPTS "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
endif ()

# Many SDR libraries ship their own FindLIBUSB.cmake or call pkg_check_modules
# for libusb-1.0, with no fallback to find_package(libusb CONFIG). Pre-feed
# the variables they expect, computed against our deps install prefix, so
# every sub-build can locate the libusb we built ourselves without needing
# pkg-config on the host. The override is harmless to libs that don't use it.
set(_libusb_inc "${SDRPP_DEPS_INSTALL_PREFIX}/include/libusb-1.0")
if (WIN32)
    set(_libusb_lib "${SDRPP_DEPS_INSTALL_PREFIX}/lib/usb-1.0.lib")
elseif (APPLE)
    set(_libusb_lib "${SDRPP_DEPS_INSTALL_PREFIX}/lib/libusb-1.0.dylib")
else ()
    set(_libusb_lib "${SDRPP_DEPS_INSTALL_PREFIX}/lib/libusb-1.0.so")
endif ()
list(APPEND DEP_CMAKE_OPTS
    # singular forms (FindLIBUSB.cmake convention used by libairspy/airspyhf/
    # hackrf/perseus and most SDR forks)
    "-DLIBUSB_INCLUDE_DIR=${_libusb_inc}"
    "-DLIBUSB_LIBRARY=${_libusb_lib}"
    # plural forms (libiio convention via pkg_check_modules result vars)
    "-DLIBUSB_INCLUDE_DIRS=${_libusb_inc}"
    "-DLIBUSB_LIBRARIES=${_libusb_lib}"
    "-DLIBUSB_FOUND=TRUE"
    "-DLIBUSB_VERSION=1.0.27"
)

# Same trick for pthreads-win32 — many SDR libs ship a bundled FindThreads.cmake
# that gates on THREADS_PTHREADS_WIN32_LIBRARY on Windows. We build pthreads4w
# above; pre-feed its install paths so every recipe finds it.
if (WIN32)
    set(_pthreads_inc "${SDRPP_DEPS_INSTALL_PREFIX}/include")
    set(_pthreads_lib "${SDRPP_DEPS_INSTALL_PREFIX}/lib/pthreadVC3.lib")
    list(APPEND DEP_CMAKE_OPTS
        "-DTHREADS_PTHREADS_INCLUDE_DIR=${_pthreads_inc}"
        "-DTHREADS_PTHREADS_WIN32_LIBRARY=${_pthreads_lib}"
    )
endif ()

# FFTW (single precision) — used by libhackrf's hackrf-tools and others.
# Bundled FindFFTW.cmake modules in the SDR forks expect FFTW_LIBRARIES /
# FFTW_INCLUDES / FFTW_INCLUDE_DIRS / FFTW_FOUND. Pre-feed them.
set(_fftw_inc "${SDRPP_DEPS_INSTALL_PREFIX}/include")
if (WIN32)
    set(_fftw_lib "${SDRPP_DEPS_INSTALL_PREFIX}/lib/fftw3f.lib")
elseif (APPLE)
    set(_fftw_lib "${SDRPP_DEPS_INSTALL_PREFIX}/lib/libfftw3f.dylib")
else ()
    set(_fftw_lib "${SDRPP_DEPS_INSTALL_PREFIX}/lib/libfftw3f.so")
endif ()
list(APPEND DEP_CMAKE_OPTS
    "-DFFTW_LIBRARIES=${_fftw_lib}"
    "-DFFTW_INCLUDES=${_fftw_inc}"
    "-DFFTW_INCLUDE_DIRS=${_fftw_inc}"
    "-DFFTW_INCLUDE_DIR=${_fftw_inc}"
    "-DFFTW_FOUND=TRUE"
)

list_projects(FOUND_PACKAGES ${CMAKE_CURRENT_LIST_DIR})
dep_message(STATUS "Found dependency recipes: ${FOUND_PACKAGES}")

# Packages provided by the host platform (skip building, find via find_package).
# Populated as we extend to Linux/macOS; on Windows everything is built from source.
set(${PROJECT_NAME}_PLATFORM_PACKAGES "" CACHE STRING "Packages provided by the platform")
set(SYSTEM_PROVIDED_PACKAGES OpenGL)
list(APPEND SYSTEM_PROVIDED_PACKAGES ${${PROJECT_NAME}_PLATFORM_PACKAGES})
list(REMOVE_DUPLICATES SYSTEM_PROVIDED_PACKAGES)

include(CMakeDependentOption)
option(${PROJECT_NAME}_SELECT_ALL "Build all dependencies" ON)

find_package(Git REQUIRED)
set(PATCH_CMD ${GIT_EXECUTABLE} apply --verbose --ignore-space-change --whitespace=fix)

set(DEPS_TO_BUILD "")
set(_build_list "")
set(_build_list_toplevel "")
set(_checked_list "")

function(check_system_package pkg checked_list)
    if (NOT ${pkg} IN_LIST ${checked_list})
        find_package(${pkg})
        if (NOT ${pkg}_FOUND)
            dep_message(WARNING "No ${pkg} found in system although marked as system-provided.")
        endif ()
        list(APPEND ${checked_list} ${pkg})
        set(${checked_list} ${${checked_list}} PARENT_SCOPE)
    endif ()
endfunction()

set(SUPPORTED_PACKAGES "")
foreach (pkg ${FOUND_PACKAGES})
    cmake_dependent_option(${PROJECT_NAME}_SELECT_${pkg}
        "Select ${pkg} for build" OFF
        "NOT ${PROJECT_NAME}_SELECT_ALL" OFF)
    set(_excluded FALSE)
    if (${PROJECT_NAME}_PACKAGE_EXCLUDES)
        if (${pkg} MATCHES "${${PROJECT_NAME}_PACKAGE_EXCLUDES}")
            set(_excluded TRUE)
        endif ()
    endif ()
    if (NOT _excluded AND (${PROJECT_NAME}_SELECT_ALL OR ${PROJECT_NAME}_SELECT_${pkg}))
        include(+${pkg}/${pkg}.cmake)
        list(APPEND SUPPORTED_PACKAGES ${pkg})

        if (${pkg} IN_LIST SYSTEM_PROVIDED_PACKAGES)
            check_system_package(${pkg} _checked_list)
        elseif (TARGET dep_${pkg})
            get_target_property(_excl_all dep_${pkg} EXCLUDE_FROM_ALL)
            if (NOT _excl_all)
                list(APPEND DEPS_TO_BUILD ${pkg})
            endif ()
        endif ()
    endif ()
endforeach ()

# Dependency graph between packages (DEP_<Name>_DEPENDS = list of other recipe names).
foreach (pkg ${SUPPORTED_PACKAGES})
    if (${pkg} IN_LIST DEPS_TO_BUILD)
        list(APPEND _build_list dep_${pkg})
        list(APPEND _build_list_toplevel dep_${pkg})
    endif ()
    foreach (deppkg ${DEP_${pkg}_DEPENDS})
        if (${deppkg} IN_LIST SYSTEM_PROVIDED_PACKAGES)
            check_system_package(${deppkg} _checked_list)
        elseif (TARGET dep_${deppkg})
            dep_message(STATUS "  ${pkg} depends on ${deppkg}")
            add_dependencies(dep_${pkg} dep_${deppkg})
            list(APPEND _build_list dep_${deppkg})
        endif ()
    endforeach ()
endforeach ()

list(REMOVE_DUPLICATES _build_list)
dep_message(STATUS "Building dep targets (${CMAKE_BUILD_TYPE}): ${_build_list}")
add_custom_target(deps ALL DEPENDS ${_build_list_toplevel})

# MSVC dual-build: spawn a sibling configure to build Debug variants of the
# C++/STL libs. Pure-C libs (DEP_DEBUG_EXCLUDES) are reused from Release.
if (DEP_DEBUG AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Pure-C / binary libraries: their public ABI is CRT-agnostic, so a
    # single Release-mode build is consumable from a Debug-mode application.
    # Only libraries that expose C++ classes / STL containers (rtaudio in our
    # current set) are left OUT of EXCLUDES so a Debug variant gets built.
    set(DEP_DEBUG_EXCLUDES
        libusb zstd zlib fftw3 sdrplay
        libxml2 libiio libad9361
        libairspy libairspyhf libhackrf librtlsdr
        libhydrasdr librfnm libfobos libperseus-sdr libmirisdr
        libbladeRF LimeSuite
        codec2 volk pthreads
        glfw3 portaudio
        # spdlog DELIBERATELY OMITTED — C++ STL types in API, needs Debug variant.
    )
    set(_build_list_filt ${_build_list})
    list(JOIN DEP_DEBUG_EXCLUDES "|" _excl_regexp)
    list(FILTER _build_list_filt EXCLUDE REGEX "${_excl_regexp}")

    set(_build_list_dbg "")
    foreach (t ${_build_list_filt})
        list(APPEND _build_list_dbg ${t}_debug)
    endforeach ()

    # Forward generator platform/toolset/instance to the spawned Debug
    # configure so e.g. -A x64 / -T v143 reach the sibling build (without
    # this, the child uses the generator's default arch/toolset).
    set(_dbg_gen_args "")
    if (CMAKE_GENERATOR_PLATFORM)
        list(APPEND _dbg_gen_args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif ()
    if (CMAKE_GENERATOR_TOOLSET)
        list(APPEND _dbg_gen_args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif ()
    if (CMAKE_GENERATOR_INSTANCE)
        list(APPEND _dbg_gen_args "-DCMAKE_GENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}")
    endif ()

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d)
    execute_process(
        COMMAND ${CMAKE_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}
            -G${CMAKE_GENERATOR}
            ${_dbg_gen_args}
            -DCMAKE_BUILD_TYPE=Debug
            -D${PROJECT_NAME}_DEP_DOWNLOAD_DIR=${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}
            -DSDRPP_DEPS_INSTALL_PREFIX=${SDRPP_DEPS_INSTALL_PREFIX}
            -D${PROJECT_NAME}_PACKAGE_EXCLUDES=${_excl_regexp}
            -D${PROJECT_NAME}_SELECT_ALL=${${PROJECT_NAME}_SELECT_ALL}
            -D${PROJECT_NAME}_DEP_BUILD_VERBOSE=${${PROJECT_NAME}_DEP_BUILD_VERBOSE}
            -DCMAKE_DEBUG_POSTFIX=d
            -DDEP_MESSAGES_WRITTEN=ON
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d
        OUTPUT_QUIET
    )

    dep_message(STATUS "Building dep targets (Debug): ${_build_list_dbg}")

    foreach (pkgtgt ${_build_list_filt})
        add_custom_target(${pkgtgt}_debug
            COMMAND ${CMAKE_COMMAND} --build . --target ${pkgtgt}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d
            USES_TERMINAL
        )
    endforeach ()

    string(JOIN " " _filt_targets "${_build_list_filt}")
    add_custom_target(deps_debug ALL
        COMMAND ${CMAKE_COMMAND} --build . --target ${_filt_targets}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/_d
        USES_TERMINAL
    )
    add_dependencies(deps_debug deps)
endif ()

set(DEP_MESSAGES_WRITTEN ON CACHE BOOL "")

install(CODE "message(STATUS \"SDR++ dependencies built into ${SDRPP_DEPS_INSTALL_PREFIX}\")")
