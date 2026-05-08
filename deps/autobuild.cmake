#
# Included from the root CMakeLists.txt when OPT_BUILD_DEPS=ON.
# Configures and builds deps/ as a sub-project before continuing the main
# configure, then prepends the deps install dir to CMAKE_PREFIX_PATH.
#

if (NOT SDRPP_DEPS_PRESET)
    set(SDRPP_DEPS_PRESET "default")
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

set(_deps_src_dir "${CMAKE_CURRENT_LIST_DIR}")
set(_build_dir "${_deps_src_dir}/build-${SDRPP_DEPS_PRESET}")
if (SDRPP_DEPS_BUILD_DIR)
    set(_build_dir "${SDRPP_DEPS_BUILD_DIR}")
endif ()

message(STATUS "  deps source: ${_deps_src_dir}")
message(STATUS "  deps build:  ${_build_dir}")

execute_process(
    COMMAND ${CMAKE_COMMAND} --preset ${SDRPP_DEPS_PRESET} "${_gen_arg}" -B ${_build_dir} ${_build_args}
    WORKING_DIRECTORY ${_deps_src_dir}
    ${_output_quiet}
    ERROR_VARIABLE _deps_configure_output
    RESULT_VARIABLE _deps_configure_result
)
if (NOT _deps_configure_result EQUAL 0)
    message(FATAL_ERROR "Dependency configure failed:\n${_deps_configure_output}")
endif ()

execute_process(
    COMMAND ${CMAKE_COMMAND} --build .
    WORKING_DIRECTORY ${_build_dir}
    ${_output_quiet}
    ERROR_VARIABLE _deps_build_output
    RESULT_VARIABLE _deps_build_result
)
if (NOT _deps_build_result EQUAL 0)
    message(FATAL_ERROR "Dependency build failed:\n${_deps_build_output}")
endif ()

set(_deps_install_prefix "${_build_dir}/destdir/usr/local")
list(APPEND CMAKE_PREFIX_PATH "${_deps_install_prefix}")
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "" FORCE)

# Surface as SDR_KIT_ROOT for modules that look there directly (Android-style path resolution).
set(SDR_KIT_ROOT "${_deps_install_prefix}" CACHE PATH "" FORCE)

message(STATUS "Dependencies installed at ${_deps_install_prefix}")
