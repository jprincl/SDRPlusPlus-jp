#
# add_cmake_project(<name> ...) — wrapper around ExternalProject_Add that
# forwards toolchain/compiler/build-type, sets up parallel builds, and
# directs install into the shared destdir prefix.
#
# Same signature as ExternalProject_Add except INSTALL_DIR / BUILD_COMMAND /
# INSTALL_COMMAND are managed for you. Use ExternalProject_Add directly when
# you need full control (non-CMake build systems, binary extracts, etc.).
#

include(ExternalProject)
include(ProcessorCount)

set(_sdrpp_deps_install_prefix_default "${CMAKE_CURRENT_BINARY_DIR}/destdir/usr/local")
if (DEFINED sdrpp_deps_DEP_INSTALL_PREFIX AND NOT DEFINED SDRPP_DEPS_INSTALL_PREFIX)
    set(_sdrpp_deps_install_prefix_default "${sdrpp_deps_DEP_INSTALL_PREFIX}")
endif ()
set(SDRPP_DEPS_INSTALL_PREFIX "${_sdrpp_deps_install_prefix_default}"
    CACHE PATH "Where dependencies will be installed")
set(${PROJECT_NAME}_DEP_DOWNLOAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloads"
    CACHE PATH "Where downloaded source archives are cached")
option(${PROJECT_NAME}_DEP_BUILD_VERBOSE "Verbose output from each dependency build" OFF)

get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if (NOT CMAKE_BUILD_TYPE AND NOT _is_multi)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Release")
endif ()

function(add_cmake_project projectname)
    cmake_parse_arguments(P_ARGS "" "INSTALL_DIR;BUILD_COMMAND;INSTALL_COMMAND" "CMAKE_ARGS" ${ARGN})

    set(_pcount ${DEP_${projectname}_MAX_THREADS})
    if (NOT _pcount)
        set(_pcount ${DEP_MAX_THREADS})
    endif ()
    if (NOT _pcount)
        ProcessorCount(_pcount)
    endif ()
    if (_pcount EQUAL 0)
        set(_pcount 1)
    endif ()

    set(_build_j "-j${_pcount}")
    if (CMAKE_GENERATOR MATCHES "Visual Studio")
        set(_build_j "-m:${_pcount}")
    endif ()

    string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type_upper)
    set(_configs_line -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE})
    if (_is_multi)
        set(_configs_line "")
    endif ()

    set(_verbose_switch "")
    if (${PROJECT_NAME}_DEP_BUILD_VERBOSE)
        if (CMAKE_GENERATOR MATCHES "Ninja")
            set(_verbose_switch "--verbose")
        elseif (CMAKE_GENERATOR MATCHES "Visual Studio")
            set(_verbose_switch "-v:d")
        endif ()
    endif ()

    # ExternalProject_Add inherits CMAKE_GENERATOR/_PLATFORM/_TOOLSET from the
    # parent by default, but make it explicit so cross-arch (e.g. Win32 → x64,
    # x64 → ARM64) and non-default toolsets (e.g. v143, ClangCL) propagate
    # reliably to every sub-build.
    set(_gen_args "")
    if (CMAKE_GENERATOR_PLATFORM)
        list(APPEND _gen_args CMAKE_GENERATOR_PLATFORM ${CMAKE_GENERATOR_PLATFORM})
    endif ()
    if (CMAKE_GENERATOR_TOOLSET)
        list(APPEND _gen_args CMAKE_GENERATOR_TOOLSET ${CMAKE_GENERATOR_TOOLSET})
    endif ()
    if (CMAKE_GENERATOR_INSTANCE)
        list(APPEND _gen_args CMAKE_GENERATOR_INSTANCE ${CMAKE_GENERATOR_INSTANCE})
    endif ()

    ExternalProject_Add(
        dep_${projectname}
        INSTALL_DIR     ${SDRPP_DEPS_INSTALL_PREFIX}
        DOWNLOAD_DIR    ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/${projectname}
        BINARY_DIR      ${CMAKE_CURRENT_BINARY_DIR}/builds/${projectname}
        ${_gen_args}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:STRING=${SDRPP_DEPS_INSTALL_PREFIX}
            -DCMAKE_PREFIX_PATH:STRING=${SDRPP_DEPS_INSTALL_PREFIX}
            -DCMAKE_DEBUG_POSTFIX:STRING=${CMAKE_DEBUG_POSTFIX}
            -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}
            -DCMAKE_TOOLCHAIN_FILE:STRING=${CMAKE_TOOLCHAIN_FILE}
            -DBUILD_SHARED_LIBS:BOOL=${BUILD_SHARED_LIBS}
            # CMake 4.x dropped support for projects requesting cmake_minimum_required
            # < 3.5. Many of our deps (FFTW 3.3.10, libxml2, codec2, ...) still ask
            # for older minimums. CMAKE_POLICY_VERSION_MINIMUM tells CMake to act as
            # if the minimum were 3.5 even if the project asks for less.
            -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
            "${_configs_line}"
            ${DEP_CMAKE_OPTS}
            ${P_ARGS_CMAKE_ARGS}
        ${P_ARGS_UNPARSED_ARGUMENTS}
        BUILD_COMMAND   ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE} -- ${_build_j} ${_verbose_switch}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config ${CMAKE_BUILD_TYPE}
    )
endfunction()

#
# sdrpp_emit_imported_config(<name>
#     [TARGET <imported-target>]
#     [LIB_NAMES <names...>]
#     [DLL_NAMES <names...>]
#     [HEADER <relative/path.h>]
#     [INCLUDE_SUBDIR <subdir>])
#
# Writes a small Config.cmake into the deps install prefix that resolves an
# imported library at consume time via find_library / find_file / find_path
# against the prefix. Use for libraries that don't ship Config.cmake upstream
# (most Alex Rouma forks, hydrasdr, libad9361, sdrplay, libusb,
# etc.). Does NOT replace upstream Config.cmake — if upstream installs one,
# don't call this helper.
#
# Defaults:
#   TARGET         ${name}::${name}
#   LIB_NAMES      ${name} with leading "lib" stripped, plus full ${name}
#   DLL_NAMES      same as LIB_NAMES with .dll suffix added
#   HEADER         (none — relies on INTERFACE_INCLUDE_DIRECTORIES = root/include)
#   INCLUDE_SUBDIR (none — uses root/include directly)
#
function(sdrpp_emit_imported_config name)
    cmake_parse_arguments(P "" "TARGET;HEADER;INCLUDE_SUBDIR" "LIB_NAMES;DLL_NAMES" ${ARGN})

    set(_target ${P_TARGET})
    if (NOT _target)
        set(_target ${name}::${name})
    endif ()

    if (P_LIB_NAMES)
        set(_lib_names ${P_LIB_NAMES})
    else ()
        string(REGEX REPLACE "^lib" "" _short ${name})
        set(_lib_names ${_short} ${name})
    endif ()
    list(REMOVE_DUPLICATES _lib_names)

    if (P_DLL_NAMES)
        set(_dll_names ${P_DLL_NAMES})
    else ()
        set(_dll_names "")
        foreach (l ${_lib_names})
            list(APPEND _dll_names "${l}.dll")
        endforeach ()
    endif ()

    set(_inc_dir "\${_root}/include")
    if (P_INCLUDE_SUBDIR)
        set(_inc_dir "\${_root}/include/${P_INCLUDE_SUBDIR}")
    endif ()

    set(_lib_names_str "${_lib_names}")
    string(REPLACE ";" " " _lib_names_str "${_lib_names_str}")
    set(_dll_names_str "${_dll_names}")
    string(REPLACE ";" " " _dll_names_str "${_dll_names_str}")

    set(_dest "${SDRPP_DEPS_INSTALL_PREFIX}/lib/cmake/${name}/${name}Config.cmake")

    # NO_CACHE was added in CMake 3.21. Consumers may be on older CMake
    # (Ubuntu focal ships 3.16), so emit a runtime gate that uses NO_CACHE
    # when available and falls back to the regular cache lookup otherwise.
    set(_content "# Generated by SDR++ deps build for ${name}\n")
    string(APPEND _content "get_filename_component(_root \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n")
    string(APPEND _content "if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)\n")
    string(APPEND _content "    set(_sdrpp_no_cache NO_CACHE)\n")
    string(APPEND _content "else ()\n")
    string(APPEND _content "    set(_sdrpp_no_cache \"\")\n")
    string(APPEND _content "endif ()\n")
    string(APPEND _content "if (NOT TARGET ${_target})\n")
    string(APPEND _content "    add_library(${_target} SHARED IMPORTED)\n")
    string(APPEND _content "    find_library(_${name}_imp NAMES ${_lib_names_str} HINTS \"\${_root}/lib\" NO_DEFAULT_PATH \${_sdrpp_no_cache})\n")
    if (P_HEADER)
        string(APPEND _content "    find_path(_${name}_inc \"${P_HEADER}\" HINTS \"${_inc_dir}\" \"\${_root}/include\" NO_DEFAULT_PATH \${_sdrpp_no_cache})\n")
    endif ()
    string(APPEND _content "    if (WIN32)\n")
    string(APPEND _content "        find_file(_${name}_loc NAMES ${_dll_names_str} HINTS \"\${_root}/bin\" NO_DEFAULT_PATH \${_sdrpp_no_cache})\n")
    string(APPEND _content "        set_target_properties(${_target} PROPERTIES IMPORTED_LOCATION \"\${_${name}_loc}\" IMPORTED_IMPLIB \"\${_${name}_imp}\")\n")
    string(APPEND _content "    else ()\n")
    string(APPEND _content "        set_target_properties(${_target} PROPERTIES IMPORTED_LOCATION \"\${_${name}_imp}\")\n")
    string(APPEND _content "    endif ()\n")
    if (P_HEADER)
        string(APPEND _content "    set_target_properties(${_target} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"\${_${name}_inc}\")\n")
    else ()
        string(APPEND _content "    set_target_properties(${_target} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"${_inc_dir}\")\n")
    endif ()
    string(APPEND _content "endif ()\n")

    file(WRITE "${_dest}" "${_content}")
endfunction()
