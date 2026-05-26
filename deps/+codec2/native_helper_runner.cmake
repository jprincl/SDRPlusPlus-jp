#
# Pre-build the native `generate_codebook` helper for codec2 cross-compile.
# Run via `cmake -P` from codec2.cmake's native_helper ExternalProject step.
#
# Inputs (via -D):
#   SOURCE_DIR:  codec2 source tree
#   BUILD_DIR:   build dir for the native helper
#   CLANG_CL:    absolute path to clang-cl.exe (host-arch one)
#
# Why this is a separate script: when invoked from an ARM64 dev shell, the
# inherited LIB / LIBPATH env vars point at the arm64 MSVC + Windows SDK lib
# directories. clang-cl with no --target emits host-arch (x64) objects, but
# lld-link then resolves CRT imports against the arm64 libs, producing
# "machine type arm64 conflicts with x64" errors. Rewriting LIB / LIBPATH
# from arm64 → x64 in this script's environment fixes that without polluting
# the cross deps build's own (arm64-correct) env. INCLUDE is arch-agnostic
# and needs no change.
#

foreach (_arg SOURCE_DIR BUILD_DIR CLANG_CL)
    if (NOT DEFINED ${_arg})
        message(FATAL_ERROR "native_helper_runner: ${_arg} not provided")
    endif ()
endforeach ()

function(_codec2_repoint_env_to_x64 var)
    set(_value "$ENV{${var}}")
    if (NOT _value)
        return()
    endif ()
    foreach (_variant arm64 ARM64 Arm64)
        string(REPLACE "\\${_variant}" "\\x64" _value "${_value}")
        string(REPLACE "/${_variant}" "/x64" _value "${_value}")
    endforeach ()
    set(ENV{${var}} "${_value}")
endfunction()

_codec2_repoint_env_to_x64(LIB)
_codec2_repoint_env_to_x64(LIBPATH)

message(STATUS "codec2 native helper: configuring in ${BUILD_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -S "${SOURCE_DIR}"
        -B "${BUILD_DIR}"
        -G Ninja
        "-DCMAKE_C_COMPILER:FILEPATH=${CLANG_CL}"
        "-DCMAKE_CXX_COMPILER:FILEPATH=${CLANG_CL}"
        -DCMAKE_BUILD_TYPE:STRING=Release
        "-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreadedDLL"
        -DUNITTEST:BOOL=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
    RESULT_VARIABLE _configure_result)
if (NOT _configure_result EQUAL 0)
    message(FATAL_ERROR "codec2 native helper configure failed (exit ${_configure_result})")
endif ()

message(STATUS "codec2 native helper: building generate_codebook target")
execute_process(
    COMMAND ${CMAKE_COMMAND}
        --build "${BUILD_DIR}"
        --target generate_codebook
        --config Release
    RESULT_VARIABLE _build_result)
if (NOT _build_result EQUAL 0)
    message(FATAL_ERROR "codec2 native helper build failed (exit ${_build_result})")
endif ()
