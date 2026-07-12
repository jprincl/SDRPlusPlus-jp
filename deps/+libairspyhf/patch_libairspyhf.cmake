#
# Run with -DSRC=<source-dir> -P this-script.
#
# AirspyHF already has STATIC_AIRSPYHFPLUS in its public header. Define it on
# the static library target and stop marking non-DLL consumers as exporters.
#
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_helpers.cmake)

set(_cmake "${SRC}/libairspyhf/src/CMakeLists.txt")
file(READ "${_cmake}" _content)
patch_replace_or_fail(_content
    "add_library(airspyhf SHARED \${c_sources} \${AIRSPYHF_DLL_SRCS})"
    "add_library(airspyhf SHARED \${c_sources} \${AIRSPYHF_DLL_SRCS})\ntarget_compile_definitions(airspyhf PRIVATE ADD_EXPORTS)")
patch_replace_or_fail(_content
    "add_library(airspyhf-static STATIC \${c_sources})"
    "add_library(airspyhf-static STATIC \${c_sources})\ntarget_compile_definitions(airspyhf-static PRIVATE STATIC_AIRSPYHFPLUS)")
file(WRITE "${_cmake}" "${_content}")
message(STATUS "Patched ${_cmake}")

set(_header "${SRC}/libairspyhf/src/airspyhf.h")
file(READ "${_header}" _header_content)
patch_replace_or_fail(_header_content
    "#if defined(_WIN32) && !defined(STATIC_AIRSPYHFPLUS)\n\t #define ADD_EXPORTS\n\t \n\t/* You should define ADD_EXPORTS *only* when building the DLL. */\n\t#ifdef ADD_EXPORTS\n\t\t#define ADDAPI __declspec(dllexport)\n\t#else\n\t\t#define ADDAPI __declspec(dllimport)\n\t#endif"
    "#if defined(_WIN32) && !defined(STATIC_AIRSPYHFPLUS)\n\t/* Define ADD_EXPORTS only when building the DLL. */\n\t#ifdef ADD_EXPORTS\n\t\t#define ADDAPI __declspec(dllexport)\n\t#else\n\t\t#define ADDAPI __declspec(dllimport)\n\t#endif")
file(WRITE "${_header}" "${_header_content}")
message(STATUS "Patched ${_header}")

#
# Streaming-teardown crash fix, kept as a proper git patch because it is
# intended for an upstream pull request (github.com/airspy/airspyhf).
#
# Upstream airspyhf_close() frees the bulk transfers even when the cancelled
# transfers were never reaped by libusb — guaranteed on Android when the USB
# fd is closed on device detach — leaving freed nodes on libusb's
# flying-transfers list and crashing inside libusb_close(). See the patch
# header for the full analysis.
#
set(_patch "${CMAKE_CURRENT_LIST_DIR}/0001-fix-use-after-free-in-airspyhf_close.patch")
find_program(_git git REQUIRED)
execute_process(
    COMMAND "${_git}" apply --check "${_patch}"
    WORKING_DIRECTORY "${SRC}"
    RESULT_VARIABLE _check_rc
    ERROR_VARIABLE _check_err
)
if (_check_rc EQUAL 0)
    execute_process(
        COMMAND "${_git}" apply "${_patch}"
        WORKING_DIRECTORY "${SRC}"
        RESULT_VARIABLE _apply_rc
        ERROR_VARIABLE _apply_err
    )
    if (NOT _apply_rc EQUAL 0)
        message(FATAL_ERROR "git apply ${_patch} failed:\n${_apply_err}")
    endif ()
    message(STATUS "Applied ${_patch}")
else ()
    # Re-runs on an already-patched tree must stay a no-op.
    execute_process(
        COMMAND "${_git}" apply --reverse --check "${_patch}"
        WORKING_DIRECTORY "${SRC}"
        RESULT_VARIABLE _reverse_rc
        ERROR_QUIET
    )
    if (_reverse_rc EQUAL 0)
        message(STATUS "Already applied: ${_patch}")
    else ()
        message(FATAL_ERROR
            "${_patch} neither applies nor is already applied — upstream shape changed:\n${_check_err}")
    endif ()
endif ()
