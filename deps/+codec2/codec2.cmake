#
# codec2 — speech codec used by OPT_BUILD_M17_DECODER.
#
# codec2 uses C99 VLAs (variable-length arrays) pervasively (~25 VLAs across
# 10+ files). MSVC's `cl.exe` has never implemented C99 VLAs and upstream
# explicitly rejects MSVC support (drowe67/codec2 PR #11, issue #66).
#
# On Windows we route this one dep through VS-bundled `clang-cl`. clang-cl
# accepts the source unchanged and emits native MSVC-ABI binaries — same
# calling convention, same CRT linkage, same import-lib format as `cl.exe`.
# No GNUtoMS step, no MSYS2 MinGW, and the resulting codec2.dll is native
# for whatever architecture the parent build targets (x64, ARM64, ...).
# Prerequisite: the "C++ Clang tools for Windows" workload component in the
# VS 2026 installer, and a VS Developer prompt so clang-cl is on PATH (the
# ninja-msvc-* presets already require this environment).
#
# On every other platform the parent toolchain handles VLAs natively, so
# we inherit the parent compiler.
#

set(_codec2_compiler_args "")
if (MSVC)
    set(_codec2_clang_hints "")
    if (DEFINED ENV{VCINSTALLDIR})
        set(_codec2_vs_host_arch "$ENV{VSCMD_ARG_HOST_ARCH}")
        if (NOT _codec2_vs_host_arch)
            if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]64|^[Aa][Aa][Rr][Cc][Hh]64")
                set(_codec2_vs_host_arch arm64)
            else ()
                set(_codec2_vs_host_arch x64)
            endif ()
        endif ()
        list(APPEND _codec2_clang_hints "$ENV{VCINSTALLDIR}/Tools/Llvm/${_codec2_vs_host_arch}/bin")
    endif ()

    find_program(_codec2_clang_cl NAMES clang-cl clang-cl.exe HINTS ${_codec2_clang_hints})
    if (NOT _codec2_clang_cl)
        message(FATAL_ERROR
            "codec2 requires clang-cl on Windows because upstream uses C99 VLAs. "
            "Install the Visual Studio C++ Clang tools for Windows component.")
    endif ()

    set(_codec2_target_arch "")
    foreach(_codec2_arch_probe
            "${CMAKE_GENERATOR_PLATFORM}"
            "${CMAKE_VS_PLATFORM_NAME}"
            "${CMAKE_C_COMPILER_ARCHITECTURE_ID}"
            "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}"
            "$ENV{VSCMD_ARG_TGT_ARCH}"
            "${CMAKE_SYSTEM_PROCESSOR}")
        if (_codec2_arch_probe MATCHES "^[Aa][Rr][Mm]64[Ee][Cc]")
            set(_codec2_target_arch ARM64EC)
            break()
        elseif (_codec2_arch_probe MATCHES "^([Aa][Rr][Mm]64|[Aa][Aa][Rr][Cc][Hh]64)")
            set(_codec2_target_arch ARM64)
            break()
        elseif (_codec2_arch_probe MATCHES "^[Aa][Rr][Mm]")
            set(_codec2_target_arch ARM)
            break()
        elseif (_codec2_arch_probe MATCHES "^([Ww][Ii][Nn]32|[Xx]86)")
            set(_codec2_target_arch x86)
            break()
        elseif (_codec2_arch_probe MATCHES "^([Xx]64|[Aa][Mm][Dd]64)")
            set(_codec2_target_arch x64)
            break()
        endif ()
    endforeach()

    if (NOT _codec2_target_arch AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_codec2_target_arch x64)
    elseif (NOT _codec2_target_arch)
        set(_codec2_target_arch x86)
    endif ()

    if (_codec2_target_arch STREQUAL "ARM64EC")
        set(_codec2_clang_target arm64ec-pc-windows-msvc)
    elseif (_codec2_target_arch STREQUAL "ARM64")
        set(_codec2_clang_target aarch64-pc-windows-msvc)
    elseif (_codec2_target_arch STREQUAL "ARM")
        set(_codec2_clang_target armv7-pc-windows-msvc)
    elseif (_codec2_target_arch STREQUAL "x64")
        set(_codec2_clang_target x86_64-pc-windows-msvc)
    else ()
        set(_codec2_clang_target i686-pc-windows-msvc)
    endif ()

    set(_codec2_compiler_args
        "-DCMAKE_C_COMPILER:FILEPATH=${_codec2_clang_cl}"
        "-DCMAKE_CXX_COMPILER:FILEPATH=${_codec2_clang_cl}"
        -DCMAKE_C_COMPILER_TARGET:STRING=${_codec2_clang_target}
        -DCMAKE_CXX_COMPILER_TARGET:STRING=${_codec2_clang_target})
    message(STATUS "codec2: using ${_codec2_clang_cl} for ${_codec2_clang_target}")
endif ()

add_cmake_project(codec2
    GIT_REPOSITORY https://github.com/drowe67/codec2
    # TODO: pin to a specific tag once we confirm what's current upstream.
    GIT_TAG        main
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_codec2.cmake
    CMAKE_ARGS
        ${_codec2_compiler_args}
        -DUNITTEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

sdrpp_validate_dep(codec2
    TARGET         codec2
    LIB_NAMES      codec2 libcodec2
    DLL_NAMES      libcodec2.dll codec2.dll
    HEADER         codec2.h
    INCLUDE_SUBDIR codec2
    REQUIRES_CONFIG)
