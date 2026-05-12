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
