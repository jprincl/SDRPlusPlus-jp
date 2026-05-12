#
# Run with -DSRC=<source-dir> -P this-script.
#
# libhydrasdr (hydrasdr/hydrasdr-host) embeds a hydrasdr-tools sub-project
# whose internal FetchContent pthreads4w uses the static MSVC CRT while the
# parent project uses the DLL CRT, causing unresolved __imp__beginthreadex etc.
# at link time. SDR++ only needs the shared library, so skip the tools
# subdirectory entirely.
#
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_helpers.cmake)

set(_f "${SRC}/CMakeLists.txt")
file(READ "${_f}" _content)
patch_replace_or_fail(_content
    "add_subdirectory(hydrasdr-tools)"
    "# add_subdirectory(hydrasdr-tools)  # disabled by SDR++ deps build (tools not needed)")
file(WRITE "${_f}" "${_content}")
message(STATUS "Patched ${_f}")

set(_h "${SRC}/libhydrasdr/src/hydrasdr.h")
file(READ "${_h}" _header)
patch_replace_or_fail(_header
    "	#ifdef ADD_EXPORTS\n		#define ADDAPI __declspec(dllexport)\n	#else\n		#define ADDAPI __declspec(dllimport)\n	#endif"
    "	#ifdef HYDRASDR_STATIC\n		#define ADDAPI\n	#elif defined(ADD_EXPORTS)\n		#define ADDAPI __declspec(dllexport)\n	#else\n		#define ADDAPI __declspec(dllimport)\n	#endif")
file(WRITE "${_h}" "${_header}")
message(STATUS "Patched ${_h}")
