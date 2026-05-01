cmake_minimum_required(VERSION 3.13)
project(radiosonde_decoder C CXX)

# Build sondedump's "radiosonde" static library (the actual decoder core).
# Force its options off so it doesn't pull ncurses/portaudio or generate an
# uninstall target that would clash with the parent project's.
set(UNINSTALL_TARGET_SAVED "${UNINSTALL_TARGET}")
set(UNINSTALL_TARGET OFF CACHE INTERNAL "")
set(ENABLE_TUI       OFF CACHE INTERNAL "")
set(ENABLE_AUDIO     OFF CACHE INTERNAL "")

set(SONDEDUMP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/decode/sondedump")
set(SONDEDUMP_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/sondedump-macos-arm64-mfpu.patch")
set(SONDEDUMP_PATCHED_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/sondedump-patched")
set(SONDEDUMP_PATCHED_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/sondedump-build")

file(REMOVE_RECURSE "${SONDEDUMP_PATCHED_SOURCE_DIR}" "${SONDEDUMP_PATCHED_BINARY_DIR}")
file(MAKE_DIRECTORY "${SONDEDUMP_PATCHED_SOURCE_DIR}")
file(COPY "${SONDEDUMP_SOURCE_DIR}/" DESTINATION "${SONDEDUMP_PATCHED_SOURCE_DIR}" PATTERN ".git" EXCLUDE)

find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${SONDEDUMP_PATCH_FILE}"
        WORKING_DIRECTORY "${SONDEDUMP_PATCHED_SOURCE_DIR}"
        RESULT_VARIABLE SONDEDUMP_PATCH_RESULT
        OUTPUT_VARIABLE SONDEDUMP_PATCH_OUTPUT
        ERROR_VARIABLE SONDEDUMP_PATCH_ERROR
    )
else()
    find_program(PATCH_EXECUTABLE patch)
    if (NOT PATCH_EXECUTABLE)
        message(FATAL_ERROR "Git or patch is required to apply ${SONDEDUMP_PATCH_FILE}")
    endif()
    execute_process(
        COMMAND "${PATCH_EXECUTABLE}" -p1 -i "${SONDEDUMP_PATCH_FILE}"
        WORKING_DIRECTORY "${SONDEDUMP_PATCHED_SOURCE_DIR}"
        RESULT_VARIABLE SONDEDUMP_PATCH_RESULT
        OUTPUT_VARIABLE SONDEDUMP_PATCH_OUTPUT
        ERROR_VARIABLE SONDEDUMP_PATCH_ERROR
    )
endif()
if (NOT SONDEDUMP_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to apply ${SONDEDUMP_PATCH_FILE}:\n${SONDEDUMP_PATCH_OUTPUT}${SONDEDUMP_PATCH_ERROR}")
endif()

add_subdirectory("${SONDEDUMP_PATCHED_SOURCE_DIR}" "${SONDEDUMP_PATCHED_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(UNINSTALL_TARGET "${UNINSTALL_TARGET_SAVED}")

# Build the SDR++ plugin DLL via the standard module convention.
file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(radiosonde_decoder PRIVATE "src/")
target_link_libraries(radiosonde_decoder PRIVATE radiosonde)
