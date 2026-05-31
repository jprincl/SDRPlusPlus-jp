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
set(_codec2_cross FALSE)
set(_codec2_clang_cl "")
if (MSVC)
    set(_codec2_vs_host_arch "$ENV{VSCMD_ARG_HOST_ARCH}")
    if (NOT _codec2_vs_host_arch)
        if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^[Aa][Rr][Mm]64|^[Aa][Aa][Rr][Cc][Hh]64")
            set(_codec2_vs_host_arch arm64)
        else ()
            set(_codec2_vs_host_arch x64)
        endif ()
    endif ()

    # Resolve VCINSTALLDIR — prefer the dev-shell env var, but fall back to
    # deriving it from the resolved cl.exe path so builds from plain
    # PowerShell (where vcvars hasn't been run, yet cl.exe was found via
    # PATH) still pick up the bundled clang-cl. cl.exe lives at
    #   <VCINSTALLDIR>/Tools/MSVC/<ver>/bin/Hostxxx/<arch>/cl.exe
    # so walking 6 directories up from cl.exe lands on VCINSTALLDIR.
    set(_codec2_vcinstall "$ENV{VCINSTALLDIR}")
    if (NOT _codec2_vcinstall AND CMAKE_C_COMPILER)
        get_filename_component(_codec2_vc_walk "${CMAKE_C_COMPILER}" DIRECTORY)
        foreach (_ RANGE 1 6)
            get_filename_component(_codec2_vc_walk "${_codec2_vc_walk}" DIRECTORY)
        endforeach ()
        if (EXISTS "${_codec2_vc_walk}/Tools/MSVC")
            set(_codec2_vcinstall "${_codec2_vc_walk}")
        endif ()
    endif ()

    # Normalize VCINSTALLDIR — env vars on Windows are typed with backslashes
    # and a trailing slash, which combined with our '/Tools/...' suffix produces
    # paths like 'C:\...\VC\/Tools/...' that CMake's find_program may misparse
    # (the trailing '\' + leading '/' can resolve to '\Tools' = drive-rooted).
    if (_codec2_vcinstall)
        file(TO_CMAKE_PATH "${_codec2_vcinstall}" _codec2_vcinstall)
        string(REGEX REPLACE "/+$" "" _codec2_vcinstall "${_codec2_vcinstall}")
    endif ()

    set(_codec2_clang_hints "")
    if (_codec2_vcinstall)
        list(APPEND _codec2_clang_hints
            "${_codec2_vcinstall}/Tools/Llvm/${_codec2_vs_host_arch}/bin"
            "${_codec2_vcinstall}/Tools/Llvm/x64/bin"
            "${_codec2_vcinstall}/Tools/Llvm/bin")
    endif ()

    # Direct existence check first — find_program's HINTS handling on Windows
    # is occasionally finicky with paths containing spaces. Fall through to
    # find_program (which searches PATH and standard locations) if our direct
    # checks miss.
    set(_codec2_clang_cl "")
    foreach (_hint IN LISTS _codec2_clang_hints)
        foreach (_name clang-cl.exe clang-cl)
            if (EXISTS "${_hint}/${_name}")
                set(_codec2_clang_cl "${_hint}/${_name}")
                break()
            endif ()
        endforeach ()
        if (_codec2_clang_cl)
            break()
        endif ()
    endforeach ()
    if (NOT _codec2_clang_cl)
        find_program(_codec2_clang_cl NAMES clang-cl clang-cl.exe HINTS ${_codec2_clang_hints})
    endif ()

    if (NOT _codec2_clang_cl)
        # Diagnostic: list contents of each hint dir so we can see whether
        # clang-cl is missing entirely, or under an unexpected name.
        set(_codec2_diag "")
        foreach (_hint IN LISTS _codec2_clang_hints)
            if (IS_DIRECTORY "${_hint}")
                file(GLOB _hint_children RELATIVE "${_hint}" "${_hint}/*")
                string(JOIN ", " _hint_listing ${_hint_children})
                string(APPEND _codec2_diag "\n  ${_hint}:\n    ${_hint_listing}")
            else ()
                string(APPEND _codec2_diag "\n  ${_hint}: <not a directory>")
            endif ()
        endforeach ()
        message(FATAL_ERROR
            "codec2 requires clang-cl on Windows because upstream uses C99 VLAs. "
            "Install the Visual Studio C++ Clang tools for Windows component, "
            "and either run from a VS Developer Shell or ensure clang-cl is on PATH."
            "${_codec2_diag}")
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

    # Cross-compile detection: the build-time helper generate_codebook.exe
    # runs on the host and emits codebook*.c. If the target arch can't run
    # on the host (e.g. x64 host -> ARM64 target), we need a native helper.
    # ARM64 / ARM64EC / ARM targets aren't runnable on x64. x86 targets ARE
    # runnable on x64 via WOW64, so we don't flag those as cross.
    if (_codec2_target_arch MATCHES "^(ARM64|ARM64EC|ARM)$"
            AND NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^([Aa][Rr][Mm]64|[Aa][Aa][Rr][Cc][Hh]64)")
        set(_codec2_cross TRUE)
    endif ()
endif ()

# When cross-compiling, build a native (host-arch) generate_codebook before
# the main configure and feed it via -DGENERATE_CODEBOOK. The patched codec2
# CMakeLists short-circuits its own native/cross helper logic when this is
# set. See patch_codec2.cmake for the GENERATE_CODEBOOK branch insertion.
set(_codec2_native_helper_dir "${CMAKE_CURRENT_BINARY_DIR}/builds/codec2-native-helper")
set(_codec2_native_helper_exe "${_codec2_native_helper_dir}/src/generate_codebook.exe")
set(_codec2_extra_cmake_args "")
if (_codec2_cross)
    list(APPEND _codec2_extra_cmake_args
        "-DGENERATE_CODEBOOK:FILEPATH=${_codec2_native_helper_exe}")
    message(STATUS "codec2: cross-compile detected; will pre-build native helper at ${_codec2_native_helper_exe}")
endif ()

add_cmake_project(codec2
    GIT_REPOSITORY https://github.com/drowe67/codec2
    # Pinned to current main; bump in lockstep when a recipe / patch change
    # demands it. Floating refs break deps-cache reproducibility and force a
    # rebuild on every upstream push.
    GIT_TAG        310777b1c6f1af0bc7c72f5b32f80f6fd9136962
    GIT_SHALLOW    OFF
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_codec2.cmake
    CMAKE_ARGS
        ${_codec2_compiler_args}
        ${_codec2_extra_cmake_args}
        -DUNITTEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

if (_codec2_cross)
    # Native helper preflight: configure + build just generate_codebook
    # with clang-cl pointed at the host arch (no --target override). The
    # runner script re-points LIB / LIBPATH from arm64 → x64 internally,
    # since we typically launch from an arm64 dev shell whose LIB env
    # points at arm64 CRT/SDK lib dirs and would otherwise fail to link
    # an x64 binary. Runs after the patch step and before the main configure.
    ExternalProject_Add_Step(dep_codec2 native_helper
        DEPENDEES patch
        DEPENDERS configure
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=<SOURCE_DIR>"
            "-DBUILD_DIR=${_codec2_native_helper_dir}"
            "-DCLANG_CL=${_codec2_clang_cl}"
            -P ${CMAKE_CURRENT_LIST_DIR}/native_helper_runner.cmake
        BYPRODUCTS ${_codec2_native_helper_exe}
        COMMENT "Pre-building native generate_codebook helper for cross-compile"
        USES_TERMINAL ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
    )
endif ()

sdrpp_validate_dep(codec2
    TARGET         codec2
    LIB_NAMES      codec2 libcodec2
    DLL_NAMES      libcodec2.dll codec2.dll
    HEADER         codec2.h
    INCLUDE_SUBDIR codec2
    REQUIRES_CONFIG)
