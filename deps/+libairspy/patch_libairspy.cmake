#
# Run with -DSRC=<source-dir> -P this-script.
#
# airspyone_host's Windows header marks every consumer as ADD_EXPORTS. That
# works poorly for static consumers, so teach the source tree an explicit
# AIRSPY_STATIC mode and define the right macro on each library target.
#
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_helpers.cmake)

set(_cmake "${SRC}/libairspy/src/CMakeLists.txt")
file(READ "${_cmake}" _content)
patch_replace_or_fail(_content
    "add_library(airspy SHARED \${c_sources} \${AIRSPY_DLL_SRCS})"
    "add_library(airspy SHARED \${c_sources} \${AIRSPY_DLL_SRCS})\ntarget_compile_definitions(airspy PRIVATE ADD_EXPORTS)")
patch_replace_or_fail(_content
    "add_library(airspy-static STATIC \${c_sources})"
    "add_library(airspy-static STATIC \${c_sources})\ntarget_compile_definitions(airspy-static PRIVATE AIRSPY_STATIC)")
file(WRITE "${_cmake}" "${_content}")
message(STATUS "Patched ${_cmake}")

set(_header "${SRC}/libairspy/src/airspy.h")
file(READ "${_header}" _header_content)
patch_replace_or_fail(_header_content
    "#ifdef _WIN32\n\t #define ADD_EXPORTS\n\t \n\t/* You should define ADD_EXPORTS *only* when building the DLL. */\n\t#ifdef ADD_EXPORTS\n\t\t#define ADDAPI __declspec(dllexport)\n\t#else\n\t\t#define ADDAPI __declspec(dllimport)\n\t#endif"
    "#ifdef _WIN32\n\t/* Define AIRSPY_STATIC when consuming/building the static library. */\n\t#ifdef AIRSPY_STATIC\n\t\t#define ADDAPI\n\t#elif defined(ADD_EXPORTS)\n\t\t#define ADDAPI __declspec(dllexport)\n\t#else\n\t\t#define ADDAPI __declspec(dllimport)\n\t#endif")
file(WRITE "${_header}" "${_header_content}")
message(STATUS "Patched ${_header}")
