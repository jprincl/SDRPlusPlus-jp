#
# Run with -DSRC=<source-dir> -P this-script.
#
# codec2's cmake/GetDependencies.cmake.in uses the legacy GetPrerequisites.cmake
# module which was removed in CMake 4.0. Once the host CMake is on 4.x, the
# install step fails with "include could not find requested file:
# GetPrerequisites.cmake". The bottom of this script writes a 3.16+ replacement
# template using file(GET_RUNTIME_DEPENDENCIES) instead.
#
# Codec2 cross-builds a native host generate_codebook helper. For Android
# builds on Windows, that nested native build uses MSVC; keep GCC flags and
# Unix libm behind MSVC checks while leaving Android Clang unchanged. The .exe
# helper path is host-Windows-specific.
#
# M_PI: several codec2 sources (sine.c, fsk.c, cohpsk.c, ...) include <math.h>
# *before* "defines.h". On MSVC / clang-cl, <math.h> does not expose M_PI unless
# _USE_MATH_DEFINES is set, but enabling _USE_MATH_DEFINES also drags in the
# MSVC <complex.h> type machinery, which collides with codec2's own COMP type.
# So we inject M_PI directly as a numeric macro into codec2's MSVC branch and
# leave _USE_MATH_DEFINES alone.
#
# clang-cl/UCRT complex compatibility: codec2 uses C99 native complex syntax
# (`complex float`) in a few OFDM/filter/debug-probe files. The Windows UCRT
# headers expose a legacy `_complex` struct macro from <math.h>, and its
# <complex.h> functions use `_Fcomplex` structs rather than Clang's native
# `_Complex float`. Force-include a tiny header for clang-cl builds that maps
# codec2's C99 complex use back to Clang native complex types and builtins.
if (CMAKE_HOST_WIN32)
    set(_complex_compat "${SRC}/src/sdrpp_clangcl_complex.h")
    file(WRITE "${_complex_compat}" [=[
#ifndef SDRPP_CODEC2_CLANGCL_COMPLEX_H
#define SDRPP_CODEC2_CLANGCL_COMPLEX_H

#if defined(_MSC_VER) && defined(__clang__) && !defined(__cplusplus)

#ifndef _COMPLEX_DEFINED
#define _COMPLEX_DEFINED
#endif

#include <complex.h>
#include <math.h>

#ifdef complex
#undef complex
#endif
#define complex _Complex

#ifdef _Complex_I
#undef _Complex_I
#endif
#ifdef I
#undef I
#endif
#define _Complex_I (__builtin_complex(0.0f, 1.0f))
#define I _Complex_I

#ifdef crealf
#undef crealf
#endif
#define crealf(z) (__real__(z))

#ifdef cimagf
#undef cimagf
#endif
#define cimagf(z) (__imag__(z))

#ifdef conjf
#undef conjf
#endif
#define conjf(z) (__builtin_conjf(z))

#ifdef cabsf
#undef cabsf
#endif
#define cabsf(z) (__builtin_cabsf(z))

#ifdef cargf
#undef cargf
#endif
#define cargf(z) (atan2f(__imag__(z), __real__(z)))

#endif

#endif
]=])
    message(STATUS "Patched ${_complex_compat}")

    set(_root "${SRC}/CMakeLists.txt")
    file(READ "${_root}" _root_contents)

    set(_wall_line [=[set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-strict-overflow")]=])
    set(_wall_wrapped [=[if(NOT MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-strict-overflow")
endif()]=])
    string(FIND "${_root_contents}" "${_wall_wrapped}" _wall_wrapped_pos)
    if (_wall_wrapped_pos EQUAL -1)
        string(FIND "${_root_contents}" "${_wall_line}" _wall_line_pos)
        if (NOT _wall_line_pos EQUAL -1)
            string(REPLACE "${_wall_line}" "${_wall_wrapped}"
                _root_contents "${_root_contents}")
        endif ()
    endif ()

    set(_msvc_flags_block [=[if(MSVC)
    add_definitions(-DM_PI=3.14159265358979323846)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_compile_options("/FI${CMAKE_CURRENT_SOURCE_DIR}/src/sdrpp_clangcl_complex.h")
    endif()
    set(CMAKE_C_FLAGS_DEBUG "/Zi /O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_C_FLAGS_DEBUG "-g -O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "-O3")
endif()]=])
    string(FIND "${_root_contents}" "sdrpp_clangcl_complex.h" _complex_force_include_pos)
    if (_complex_force_include_pos EQUAL -1)
        set(_original_flags_block [=[set(CMAKE_C_FLAGS_DEBUG "-g -O2 -DDUMP")
set(CMAKE_C_FLAGS_RELEASE "-O3")]=])
        set(_patched_flags_block [=[if(MSVC)
    add_definitions(-DM_PI=3.14159265358979323846)
    set(CMAKE_C_FLAGS_DEBUG "/Zi /O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_C_FLAGS_DEBUG "-g -O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "-O3")
endif()]=])
        set(_patched_flags_block_no_mpi [=[if(MSVC)
    set(CMAKE_C_FLAGS_DEBUG "/Zi /O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_C_FLAGS_DEBUG "-g -O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "-O3")
endif()]=])

        string(FIND "${_root_contents}" "${_original_flags_block}" _flags_pos)
        if (NOT _flags_pos EQUAL -1)
            string(REPLACE "${_original_flags_block}" "${_msvc_flags_block}"
                _root_contents "${_root_contents}")
        else ()
            string(FIND "${_root_contents}" "${_patched_flags_block}" _flags_pos)
            if (NOT _flags_pos EQUAL -1)
                string(REPLACE "${_patched_flags_block}" "${_msvc_flags_block}"
                    _root_contents "${_root_contents}")
            else ()
                string(FIND "${_root_contents}" "${_patched_flags_block_no_mpi}" _flags_pos)
                if (NOT _flags_pos EQUAL -1)
                    string(REPLACE "${_patched_flags_block_no_mpi}" "${_msvc_flags_block}"
                        _root_contents "${_root_contents}")
                else ()
                    message(WARNING "Could not patch codec2 MSVC flag block in ${_root}")
                endif ()
            endif ()
        endif ()
    endif ()

    set(_stale_flags_tail [=[endif()
    set(CMAKE_C_FLAGS_DEBUG "-g -O2 -DDUMP")
    set(CMAKE_C_FLAGS_RELEASE "-O3")
endif()]=])
    string(REPLACE "${_stale_flags_tail}" "endif()"
        _root_contents "${_root_contents}")

    set(_demo_subdir [=[add_subdirectory(demo)]=])
    set(_demo_subdir_wrapped [=[if(NOT MSVC)
    add_subdirectory(demo)
endif()]=])
    string(FIND "${_root_contents}" "${_demo_subdir_wrapped}" _demo_subdir_wrapped_pos)
    if (_demo_subdir_wrapped_pos EQUAL -1)
        string(REPLACE "${_demo_subdir}" "${_demo_subdir_wrapped}"
            _root_contents "${_root_contents}")
    endif ()

    file(WRITE "${_root}" "${_root_contents}")
    message(STATUS "Patched ${_root}")

    set(_src_cmake "${SRC}/src/CMakeLists.txt")
    file(READ "${_src_cmake}" _src_cmake_contents)

    set(_m_alias_block [=[if(MSVC AND NOT TARGET m)
    add_library(m INTERFACE)
endif()]=])
    string(FIND "${_src_cmake_contents}" "${_m_alias_block}" _m_alias_pos)
    if (_m_alias_pos EQUAL -1)
        string(REPLACE
            [=[set(D ${CMAKE_CURRENT_SOURCE_DIR}/codebook)

# lsp quantisers]=]
            [=[set(D ${CMAKE_CURRENT_SOURCE_DIR}/codebook)

if(MSVC AND NOT TARGET m)
    add_library(m INTERFACE)
endif()

# lsp quantisers]=]
            _src_cmake_contents "${_src_cmake_contents}")
    endif ()

    set(_generate_codebook_m_link [=[    target_link_libraries(generate_codebook m)]=])
    set(_generate_codebook_m_link_wrapped [=[    if(NOT MSVC)
        target_link_libraries(generate_codebook m)
    endif()]=])
    string(FIND "${_src_cmake_contents}" "${_generate_codebook_m_link_wrapped}" _m_link_wrapped_pos)
    if (_m_link_wrapped_pos EQUAL -1)
        string(REPLACE "${_generate_codebook_m_link}" "${_generate_codebook_m_link_wrapped}"
            _src_cmake_contents "${_src_cmake_contents}")
    endif ()

    set(_codec2_exports_block [=[if(MSVC)
    set_target_properties(codec2 PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()]=])
    string(FIND "${_src_cmake_contents}" "WINDOWS_EXPORT_ALL_SYMBOLS" _codec2_exports_pos)
    if (_codec2_exports_pos EQUAL -1)
        string(REPLACE
            [=[add_library(codec2 ${CODEC2_SRCS})]=]
            [=[add_library(codec2 ${CODEC2_SRCS})
if(MSVC)
    set_target_properties(codec2 PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()]=]
            _src_cmake_contents "${_src_cmake_contents}")
    endif ()

    string(FIND "${_src_cmake_contents}" "generate_codebook.exe" _generate_codebook_exe_pos)
    if (_generate_codebook_exe_pos EQUAL -1)
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
    endif ()

    set(_clang_builtins_block [=[if(MSVC AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE _clang_resource_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    set(_clang_rt_arch_candidates "")
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64EC" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64EC")
        list(APPEND _clang_rt_arch_candidates arm64ec aarch64)
    elseif(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|AARCH64|aarch64")
        list(APPEND _clang_rt_arch_candidates aarch64 arm64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        list(APPEND _clang_rt_arch_candidates x86_64)
    else()
        list(APPEND _clang_rt_arch_candidates i386)
    endif()
    set(_clang_rt_builtins "")
    foreach(_clang_rt_arch IN LISTS _clang_rt_arch_candidates)
        set(_clang_rt_candidate "${_clang_resource_dir}/lib/windows/clang_rt.builtins-${_clang_rt_arch}.lib")
        if(EXISTS "${_clang_rt_candidate}")
            set(_clang_rt_builtins "${_clang_rt_candidate}")
            break()
        endif()
    endforeach()
    if(NOT _clang_rt_builtins)
        message(FATAL_ERROR "Could not find clang_rt.builtins for codec2 clang-cl build below ${_clang_resource_dir}/lib/windows")
    endif()
    target_link_libraries(codec2 PRIVATE "${_clang_rt_builtins}")
endif()]=])
    string(FIND "${_src_cmake_contents}" "clang_rt.builtins" _clang_builtins_pos)
    if (_clang_builtins_pos EQUAL -1)
        set(_unix_m_link_block [=[if(UNIX)
    target_link_libraries(codec2 PUBLIC m)
endif(UNIX)]=])
        set(_unix_m_link_block_with_builtins "${_unix_m_link_block}\n${_clang_builtins_block}")
        string(REPLACE "${_unix_m_link_block}" "${_unix_m_link_block_with_builtins}"
            _src_cmake_contents "${_src_cmake_contents}")
    endif ()

    set(_tool_block_start [=[add_executable(c2enc c2enc.c)]=])
    set(_tool_block_stop [=[install(TARGETS codec2 EXPORT codec2-config]=])
    string(FIND "${_src_cmake_contents}" "if(NOT MSVC)\nadd_executable(c2enc c2enc.c)" _tools_wrapped_pos)
    if (_tools_wrapped_pos EQUAL -1)
        string(REPLACE "${_tool_block_start}" "if(NOT MSVC)\n${_tool_block_start}"
            _src_cmake_contents "${_src_cmake_contents}")
        string(REPLACE "${_tool_block_stop}" "endif()\n\n${_tool_block_stop}"
            _src_cmake_contents "${_src_cmake_contents}")
    endif ()

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
