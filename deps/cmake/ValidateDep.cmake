#
# sdrpp_validate_dep(<name> ...) — register a post-install validation step
# on the dep_<name> ExternalProject.
#
# Verifies that the install actually produced a usable layout:
#   - expected library binary is found by name under the install prefix
#   - on Windows shared builds, expected DLL is found under bin/
#   - expected header is found under include/[<subdir>]
#   - <name>Config.cmake exists in lib/cmake/<name>/ (if REQUIRES_CONFIG)
#   - the Config.cmake imported target points at the selected static/shared
#     artifacts (if REQUIRES_CONFIG)
#   - writes a manifest at <buildDir>/validation/<name>.installed
#
# A FATAL_ERROR with the searched paths is raised on first miss, so the
# failure is attributed to the dep that broke instead of surfacing as a
# cryptic linker error in a consumer module much later.
#
# Args:
#   <name>             matches dep_<name> ExternalProject target
#   HEADER <file>      header filename (e.g. hackrf.h)
#   INCLUDE_SUBDIR <d> optional subdir of include/ where the header lives
#   LIB_NAMES <names>  candidate library basenames; defaults to <name>
#                      with leading "lib" stripped, plus full <name>
#   STATIC_LIB_NAMES <names>
#                      candidate library basenames used when the dependency
#                      policy resolves to static linkage
#   SHARED_LIB_NAMES <names>
#                      candidate library basenames used when the dependency
#                      policy resolves to shared linkage
#   DLL_NAMES <names>  candidate DLL filenames (Windows only); defaults
#                      to LIB_NAMES with ".dll" suffix
#   TARGET <name>      Config.cmake imported target name (informational,
#                      written to the manifest)
#   STATIC_TARGET <name>
#                      imported target name used when policy resolves static
#   SHARED_TARGET <name>
#                      imported target name used when policy resolves shared
#   PACKAGE_NAME <name>
#                      Config-mode find_package name; defaults to <name>
#   CONFIG_SUBDIR <d>  optional lib/cmake/<d> subdir to search when
#                      REQUIRES_CONFIG is set
#   REQUIRES_CONFIG    require Config.cmake to be present and verify the
#                      imported target's artifact properties
#

if (COMMAND sdrpp_validate_dep)
    return()
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/DepPolicy.cmake)

if (NOT DEFINED SDRPP_TRACE_DEP_ARTIFACTS)
    set(SDRPP_TRACE_DEP_ARTIFACTS "OFF")
endif ()

set(SDRPP_DEPS_VALIDATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/validation"
    CACHE PATH "Where dependency validation manifests will be written")
set(SDRPP_DEPS_PROBES_DIR "${CMAKE_CURRENT_BINARY_DIR}/probes"
    CACHE PATH "Where dependency validation probe projects will be written")

set(_SDRPP_VALIDATE_DEP_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ValidateDepStep.cmake")

function(sdrpp_validate_dep name)
    cmake_parse_arguments(P "REQUIRES_CONFIG"
        "HEADER;INCLUDE_SUBDIR;TARGET;STATIC_TARGET;SHARED_TARGET;PACKAGE_NAME;CONFIG_SUBDIR"
        "LIB_NAMES;STATIC_LIB_NAMES;SHARED_LIB_NAMES;DLL_NAMES" ${ARGN})

    set(_shared 0)
    sdrpp_dep_builds_shared(${name} _dep_builds_shared)
    if (_dep_builds_shared)
        set(_shared 1)
    endif ()

    set(_target ${P_TARGET})
    if (_dep_builds_shared AND P_SHARED_TARGET)
        set(_target ${P_SHARED_TARGET})
    elseif (NOT _dep_builds_shared AND P_STATIC_TARGET)
        set(_target ${P_STATIC_TARGET})
    endif ()
    if (NOT _target)
        set(_target ${name}::${name})
    endif ()

    set(_package_name ${P_PACKAGE_NAME})
    if (NOT _package_name)
        set(_package_name ${name})
    endif ()

    if (_dep_builds_shared AND P_SHARED_LIB_NAMES)
        set(P_LIB_NAMES ${P_SHARED_LIB_NAMES})
    elseif (NOT _dep_builds_shared AND P_STATIC_LIB_NAMES)
        set(P_LIB_NAMES ${P_STATIC_LIB_NAMES})
    endif ()

    if (NOT P_LIB_NAMES)
        string(REGEX REPLACE "^lib" "" _short ${name})
        set(P_LIB_NAMES ${_short} ${name})
    endif ()
    list(REMOVE_DUPLICATES P_LIB_NAMES)

    if (NOT P_DLL_NAMES)
        set(P_DLL_NAMES "")
        foreach (l ${P_LIB_NAMES})
            list(APPEND P_DLL_NAMES "${l}.dll")
        endforeach ()
    endif ()

    # In Debug builds on MSVC, many upstream CMakeLists hardcode
    # CMAKE_DEBUG_POSTFIX=d (zlib, spdlog, glfw, ...), so the installed libs
    # land with a 'd' suffix. Add postfixed variants to the candidate lists —
    # find_library/find_file return the first match, so libs that don't apply
    # the postfix still resolve via the non-postfixed name.
    if (MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_postfixed_libs "")
        foreach (n ${P_LIB_NAMES})
            list(APPEND _postfixed_libs "${n}d")
        endforeach ()
        list(PREPEND P_LIB_NAMES ${_postfixed_libs})
        list(REMOVE_DUPLICATES P_LIB_NAMES)

        set(_postfixed_dlls "")
        foreach (dll ${P_DLL_NAMES})
            get_filename_component(_stem "${dll}" NAME_WE)
            get_filename_component(_ext "${dll}" EXT)
            list(APPEND _postfixed_dlls "${_stem}d${_ext}")
        endforeach ()
        list(PREPEND P_DLL_NAMES ${_postfixed_dlls})
        list(REMOVE_DUPLICATES P_DLL_NAMES)
    endif ()

    # ExternalProject_Add_Step's COMMAND is itself a CMake list, so a
    # semicolon-list arg would split into multiple args. Pack with '|'
    # and let the worker re-split.
    string(REPLACE ";" "|" _lib_names_arg "${P_LIB_NAMES}")
    string(REPLACE ";" "|" _dll_names_arg "${P_DLL_NAMES}")
    string(REPLACE ";" "," _trace_dep_artifacts_arg "${SDRPP_TRACE_DEP_ARTIFACTS}")

    set(_require_config 0)
    if (P_REQUIRES_CONFIG)
        set(_require_config 1)
    endif ()

    set(_win 0)
    if (WIN32)
        set(_win 1)
    endif ()

    ExternalProject_Add_Step(dep_${name} validate
        DEPENDEES install
        COMMAND ${CMAKE_COMMAND}
            -DPREFIX:PATH=${SDRPP_DEPS_INSTALL_PREFIX}
            -DNAME:STRING=${name}
            -DPACKAGE_NAME:STRING=${_package_name}
            -DTARGET_NAME:STRING=${_target}
            -DLIB_NAMES:STRING=${_lib_names_arg}
            -DDLL_NAMES:STRING=${_dll_names_arg}
            -DHEADER:STRING=${P_HEADER}
            -DINCLUDE_SUBDIR:STRING=${P_INCLUDE_SUBDIR}
            -DCONFIG_SUBDIR:STRING=${P_CONFIG_SUBDIR}
            -DREQUIRES_CONFIG:BOOL=${_require_config}
            -DSHARED:BOOL=${_shared}
            -DWIN:BOOL=${_win}
            -DVALIDATION_DIR:PATH=${SDRPP_DEPS_VALIDATION_DIR}
            -DPROBES_DIR:PATH=${SDRPP_DEPS_PROBES_DIR}
            -DSDRPP_TRACE_DEP_ARTIFACTS:STRING=${_trace_dep_artifacts_arg}
            -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}
            -DANDROID_ABI:STRING=${ANDROID_ABI}
            -DANDROID_PLATFORM:STRING=${ANDROID_PLATFORM}
            -DANDROID_STL:STRING=${ANDROID_STL}
            -DANDROID_ARM_MODE:STRING=${ANDROID_ARM_MODE}
            -DANDROID_ARM_NEON:STRING=${ANDROID_ARM_NEON}
            -P ${_SDRPP_VALIDATE_DEP_SCRIPT}
        COMMENT "Validating ${name} install at ${SDRPP_DEPS_INSTALL_PREFIX}"
        USES_TERMINAL ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
    )
endfunction()
