#
# Included from the root CMakeLists.txt when OPT_BUILD_DEPS=ON.
# Configures and builds deps/ as a sub-project before continuing the main
# configure, then prepends the deps install dir to CMAKE_PREFIX_PATH.
#

if (NOT SDRPP_DEPS_PRESET)
    set(SDRPP_DEPS_PRESET "default")
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/sdrpp_trace_helpers.cmake)
sdrpp_init_cmake_trace_defaults("${CMAKE_BINARY_DIR}")
set(SDRPP_TRACE_DEP_ARTIFACTS "OFF" CACHE STRING "Print validated dependency artifacts to the console (ON/all, or comma/semicolon-separated dependency names)")

if (ANDROID AND SDRPP_DEPS_PRESET STREQUAL "default" AND ANDROID_ABI)
    set(SDRPP_DEPS_PRESET "android-${ANDROID_ABI}")
endif ()

set(_output_quiet "")
if (SDRPP_DEPS_OUTPUT_QUIET)
    set(_output_quiet OUTPUT_QUIET)
endif ()

message(STATUS "Building dependencies with preset '${SDRPP_DEPS_PRESET}'")

set(_gen_arg "")
if (CMAKE_GENERATOR)
    set(_gen_arg "-G${CMAKE_GENERATOR}")
endif ()

set(_build_args "")
if (CMAKE_C_COMPILER)
    list(APPEND _build_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif ()
if (CMAKE_CXX_COMPILER)
    list(APPEND _build_args "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif ()
if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND _build_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif ()
if (DEFINED _sdrpp_dep_root_packages_effective AND _sdrpp_dep_root_packages_effective)
    list(JOIN _sdrpp_dep_root_packages_effective "," _dep_root_packages_arg)
    list(APPEND _build_args "-DSDRPP_DEP_ROOT_PACKAGES=${_dep_root_packages_arg}")
endif ()

# Policy overrides set on the main project's cmake invocation only take effect
# on the deps sub-build if forwarded explicitly — the sub-build configures via
# `cmake --preset`, so it reads its own cache, not the parent's. Without this,
# the parent resolves a dep as bundled (and expects a Config.cmake in the deps
# prefix) while the sub-build resolves it as system and never builds it.
foreach (_var IN ITEMS
        SDRPP_DEP_PROFILE
        SDRPP_DEP_FORCE_BUNDLED
        SDRPP_DEP_FORCE_SYSTEM
        SDRPP_DEP_FORCE_SHARED
        SDRPP_DEP_FORCE_STATIC
        SDRPP_DEP_POLICY_STRICT)
    if (DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
        list(APPEND _build_args "-D${_var}=${${_var}}")
    endif ()
endforeach ()
set(_trace_dep_artifacts_arg "${SDRPP_TRACE_DEP_ARTIFACTS}")
string(REPLACE ";" "," _trace_dep_artifacts_arg "${_trace_dep_artifacts_arg}")
list(APPEND _build_args
    "-DSDRPP_TRACE_CMAKE_INVOCATIONS=${SDRPP_TRACE_CMAKE_INVOCATIONS}"
    "-DSDRPP_SERIALIZE_CMAKE_INVOCATIONS=${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}"
    "-DSDRPP_CMAKE_TRACE_LOG=${SDRPP_CMAKE_TRACE_LOG}"
    "-DSDRPP_TRACE_DEP_ARTIFACTS=${_trace_dep_artifacts_arg}")

# Single-config build model: each build tree produces exactly one configuration.
# Pick that configuration from the parent's CMAKE_BUILD_TYPE (single-config
# generators) or its active CMAKE_CONFIGURATION_TYPES (multi-config), with an
# explicit SDRPP_DEPS_BUILD_CONFIG override for the rare case the deps and the
# app want to differ.
if (SDRPP_DEPS_BUILD_CONFIG)
    set(_deps_build_config "${SDRPP_DEPS_BUILD_CONFIG}")
elseif (CMAKE_BUILD_TYPE)
    set(_deps_build_config "${CMAKE_BUILD_TYPE}")
elseif (CMAKE_CONFIGURATION_TYPES)
    if (MSVC)
        set(_deps_build_config "RelWithDebInfo")
    else ()
        list(GET CMAKE_CONFIGURATION_TYPES 0 _deps_build_config)
    endif ()
else ()
    set(_deps_build_config "Release")
endif ()

list(APPEND _build_args "-DCMAKE_BUILD_TYPE=${_deps_build_config}")

set(_deps_src_dir "${CMAKE_CURRENT_LIST_DIR}")
set(_build_dir "${_deps_src_dir}/build-${SDRPP_DEPS_PRESET}-${_deps_build_config}")
if (SDRPP_DEPS_BUILD_DIR)
    set(_build_dir "${SDRPP_DEPS_BUILD_DIR}")
endif ()

message(STATUS "  deps source: ${_deps_src_dir}")
message(STATUS "  deps build:  ${_build_dir}")
message(STATUS "  deps config: ${_deps_build_config}")
if (DEFINED _dep_root_packages_arg AND NOT "${_dep_root_packages_arg}" STREQUAL "")
    message(STATUS "  deps roots:  ${_dep_root_packages_arg}")
endif ()

set(_deps_build_cmd ${CMAKE_COMMAND} --build .)
# Multi-config generators (VS) still need --config on the build line; for
# single-config the configure-time CMAKE_BUILD_TYPE is authoritative and
# --config is harmlessly ignored.
list(APPEND _deps_build_cmd --config "${_deps_build_config}")
if (SDRPP_SERIALIZE_CMAKE_INVOCATIONS)
    list(APPEND _deps_build_cmd --parallel 1)
endif ()

set(_deps_configure_cmd ${CMAKE_COMMAND} --preset ${SDRPP_DEPS_PRESET})
if (_gen_arg)
    list(APPEND _deps_configure_cmd "${_gen_arg}")
endif ()
list(APPEND _deps_configure_cmd -B "${_build_dir}" ${_build_args})
sdrpp_wrap_traced_command(_deps_configure_cmd "root:deps-configure" "${_deps_src_dir}" NO_SERIALIZE ${_deps_configure_cmd})
sdrpp_wrap_traced_command(_deps_build_cmd "root:deps-build" "${_build_dir}" NO_SERIALIZE ${_deps_build_cmd})

# Let stdout/stderr stream live to the parent process. Capturing stderr into
# a variable (the prior approach) buffers the entire child build and only
# dumps it at the end inside the FATAL_ERROR body, where CI log viewers
# truncate the head — i.e. exactly where the real error lives. Streaming
# keeps the failure visible in context as it happens.
execute_process(
    COMMAND ${_deps_configure_cmd}
    WORKING_DIRECTORY ${_deps_src_dir}
    ${_output_quiet}
    RESULT_VARIABLE _deps_configure_result
)
if (NOT _deps_configure_result EQUAL 0)
    message(FATAL_ERROR "Dependency configure failed (see output above)")
endif ()

execute_process(
    COMMAND ${_deps_build_cmd}
    WORKING_DIRECTORY ${_build_dir}
    ${_output_quiet}
    RESULT_VARIABLE _deps_build_result
)
if (NOT _deps_build_result EQUAL 0)
    message(FATAL_ERROR "Dependency build failed (see output above)")
endif ()

set(_deps_install_prefix "${_build_dir}/destdir/usr/local")
list(APPEND CMAKE_PREFIX_PATH "${_deps_install_prefix}")
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "" FORCE)

message(STATUS "Dependencies installed at ${_deps_install_prefix}")
