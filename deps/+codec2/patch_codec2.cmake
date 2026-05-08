#
# Run with -DSRC=<source-dir> -P this-script.
#
# codec2's cmake/GetDependencies.cmake.in uses the legacy GetPrerequisites.cmake
# module which was removed in CMake 4.0. The MinGW cmake in MSYS2 is already at
# 4.x, so the install step fails with "include could not find requested file:
# GetPrerequisites.cmake".
#
# Codec2 cross-builds a native host generate_codebook helper. For Android
# builds on Windows, that nested native build uses MSVC; keep GCC flags and
# Unix libm behind MSVC checks while leaving Android Clang unchanged. The .exe
# helper path is host-Windows-specific.
if (CMAKE_HOST_WIN32)
    set(_root "${SRC}/CMakeLists.txt")
    file(READ "${_root}" _root_contents)
    string(REPLACE
        [=[set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-strict-overflow")]=]
        [=[if(NOT MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-strict-overflow")
endif()]=]
        _root_contents "${_root_contents}")
    string(REPLACE
        [=[set(CMAKE_C_FLAGS_DEBUG "-g -O2 -DDUMP")
set(CMAKE_C_FLAGS_RELEASE "-O3")]=]
        [=[if(MSVC)
    set(CMAKE_C_FLAGS_DEBUG "/Zi /O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_C_FLAGS_DEBUG "-g -O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "-O3")
endif()]=]
        _root_contents "${_root_contents}")
    file(WRITE "${_root}" "${_root_contents}")
    message(STATUS "Patched ${_root}")

    set(_src_cmake "${SRC}/src/CMakeLists.txt")
    file(READ "${_src_cmake}" _src_cmake_contents)
    string(REPLACE
        [=[    target_link_libraries(generate_codebook m)]=]
        [=[    if(NOT MSVC)
        target_link_libraries(generate_codebook m)
    endif()]=]
        _src_cmake_contents "${_src_cmake_contents}")
    string(REPLACE
        [=[    ExternalProject_Add(codec2_native
       SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..
       BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/codec2_native
       BUILD_COMMAND ${CMAKE_COMMAND} --build . --target generate_codebook
       INSTALL_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/codec2_native/src/generate_codebook ${CMAKE_CURRENT_BINARY_DIR}
       BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/generate_codebook
    )
    add_executable(generate_codebook IMPORTED)
    set_target_properties(generate_codebook PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/generate_codebook)]=]
        [=[    ExternalProject_Add(codec2_native
       SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..
       BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/codec2_native
       BUILD_COMMAND ${CMAKE_COMMAND} --build . --target generate_codebook
       INSTALL_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/codec2_native/src/generate_codebook.exe ${CMAKE_CURRENT_BINARY_DIR}/generate_codebook.exe
       BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/generate_codebook.exe
    )
    add_executable(generate_codebook IMPORTED)
    set_target_properties(generate_codebook PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/generate_codebook.exe)]=]
        _src_cmake_contents "${_src_cmake_contents}")
    file(WRITE "${_src_cmake}" "${_src_cmake_contents}")
    message(STATUS "Patched ${_src_cmake}")
endif ()

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
