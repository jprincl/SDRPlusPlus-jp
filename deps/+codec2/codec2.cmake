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
    set(_codec2_compiler_args
        -DCMAKE_C_COMPILER:STRING=clang-cl
        -DCMAKE_CXX_COMPILER:STRING=clang-cl)
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
