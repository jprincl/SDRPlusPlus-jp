#
# Helper script invoked by ExternalProject_Add for the codec2 MSVC+MinGW path.
# Called as:
#   cmake -DMINGW_BIN=... -DMINGW_CMAKE=... -DMINGW_NINJA=...
#         -DSOURCE_DIR=... -DBINARY_DIR=... -DINSTALL_PREFIX=...
#         -DMODE=configure|build|install
#         -P codec2_mingw.cmake
#
# Prepends the MinGW bin directory to PATH before spawning the child process.
# This is necessary because gcc.exe spawns cc1.exe whose DLL dependencies live
# in the MinGW bin directory, which is not on the system PATH.
#
# cmake -E env cannot be used for this because PATH contains semicolons, which
# are CMake's list separator; they cannot survive the ExternalProject → Ninja
# command pipeline intact regardless of escaping.  A -P script with
# set(ENV{PATH} ...) does not have this problem: the value is set directly in
# the CMake process memory and never passes through a shell.
#
cmake_minimum_required(VERSION 3.16)

set(ENV{PATH} "${MINGW_BIN};$ENV{PATH}")

if (MODE STREQUAL "configure")
    execute_process(
        COMMAND "${MINGW_CMAKE}"
            -S "${SOURCE_DIR}"
            -B "${BINARY_DIR}"
            -G Ninja
            -DCMAKE_MAKE_PROGRAM=${MINGW_NINJA}
            -DCMAKE_C_COMPILER=${MINGW_BIN}/gcc.exe
            -DCMAKE_GNUtoMS=ON
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_C_FLAGS=-static-libgcc
            -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            -DUNITTEST=OFF
            -DBUILD_SHARED_LIBS=ON
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        RESULT_VARIABLE _r
    )
elseif (MODE STREQUAL "build")
    execute_process(
        COMMAND "${MINGW_CMAKE}" --build "${BINARY_DIR}"
        RESULT_VARIABLE _r
    )
elseif (MODE STREQUAL "install")
    execute_process(
        COMMAND "${MINGW_CMAKE}" --build "${BINARY_DIR}" --target install
        RESULT_VARIABLE _r
    )
else ()
    message(FATAL_ERROR "codec2_mingw.cmake: unknown MODE '${MODE}'")
endif ()

if (_r)
    message(FATAL_ERROR "codec2 ${MODE} step failed with exit code ${_r}")
endif ()
