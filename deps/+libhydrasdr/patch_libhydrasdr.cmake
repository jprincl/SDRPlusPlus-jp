#
# Run with -DSRC=<source-dir> -P this-script.
#
# libhydrasdr (hydrasdr/hydrasdr-host) embeds a hydrasdr-tools sub-project
# whose internal FetchContent pthreads4w is built with /MT (static CRT)
# while the parent project uses /MD, causing unresolved __imp__beginthreadex
# etc. at link time. SDR++ only needs the shared library, so skip the tools
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
