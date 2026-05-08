#
# Run with -DSRC=<source-dir> -P this-script.
#
# codec2's cmake/GetDependencies.cmake.in uses the legacy GetPrerequisites.cmake
# module which was removed in CMake 4.0. The MinGW cmake in MSYS2 is already at
# 4.x, so the install step fails with "include could not find requested file:
# GetPrerequisites.cmake".
#
# Replace the template with a modern equivalent that uses the built-in
# file(GET_RUNTIME_DEPENDENCIES) command (available since CMake 3.16).
#
set(_f "${SRC}/cmake/GetDependencies.cmake.in")
file(WRITE "${_f}" [=[
# As this script is run in a new cmake instance, it does not have access to
# the existing cache variables. Pass them in via the configure_file command.
set(CMAKE_BINARY_DIR @CMAKE_BINARY_DIR@)
set(CMAKE_SOURCE_DIR @CMAKE_SOURCE_DIR@)
set(UNIX @UNIX@)
set(WIN32 @WIN32@)
set(CMAKE_CROSSCOMPILING @CMAKE_CROSSCOMPILING@)
set(CMAKE_FIND_LIBRARY_SUFFIXES @CMAKE_FIND_LIBRARY_SUFFIXES@)
set(CMAKE_FIND_LIBRARY_PREFIXES @CMAKE_FIND_LIBRARY_PREFIXES@)
set(CMAKE_SYSTEM_LIBRARY_PATH @CMAKE_SYSTEM_LIBRARY_PATH@)
set(CMAKE_FIND_ROOT_PATH @CMAKE_FIND_ROOT_PATH@)
set(CODEC2_DLL ${CMAKE_BINARY_DIR}/src/libcodec2.dll)

# GetPrerequisites.cmake was removed in CMake 4.0. Use the modern equivalent.
if(EXISTS "${CODEC2_DLL}")
    file(GET_RUNTIME_DEPENDENCIES
        LIBRARIES "${CODEC2_DLL}"
        RESOLVED_DEPENDENCIES_VAR _resolved
        UNRESOLVED_DEPENDENCIES_VAR _unresolved
        DIRECTORIES @CMAKE_SYSTEM_LIBRARY_PATH@
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32/.*\\.dll$"
    )
    foreach(_dep IN LISTS _resolved)
        message(STATUS "Installing runtime dep: ${_dep}")
        file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
             TYPE EXECUTABLE FILES "${_dep}")
    endforeach()
endif()
]=])
message(STATUS "Patched ${_f}")
