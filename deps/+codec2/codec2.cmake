#
# codec2 — speech codec used by OPT_BUILD_M17_DECODER.
#
# codec2 uses C99 VLAs (variable-length arrays) pervasively throughout its
# library sources (~25 VLAs across 10+ files).  MSVC never implemented C99
# VLAs and there is no compiler flag to enable them.  The upstream explicitly
# rejects MSVC support (see drowe67/codec2 PR #11 and issue #66).
#
# On Windows, codec2 is built with the MinGW-w64 GCC from MSYS2.
# The MinGW cmake and ninja binaries are invoked directly (no shell wrapper)
# so there are no quoting or PATH issues.
#
# -DCMAKE_GNUtoMS=ON instructs the MinGW cmake to also emit a MSVC-compatible
# import library (codec2.lib) alongside the DLL.  This requires lib.exe from
# the MSVC toolset to be on PATH at build time.  When building via
# "cmake --build" with the Visual Studio generator that is guaranteed because
# MSBuild sets up the VC environment for its custom-build steps.
#
# -DCMAKE_C_FLAGS=-static-libgcc statically links libgcc into the DLL so
# that the installed DLL has no runtime dependency on the MinGW libgcc DLL.
#
# Prerequisites: install MSYS2 and the MinGW-w64 toolchain as described in
# doc/install-mingw.md.
#

if (MSVC)
    set(_msys2_bin   "C:/msys64/mingw64/bin")
    set(_msys2_gcc   "${_msys2_bin}/gcc.exe")
    set(_msys2_cmake "${_msys2_bin}/cmake.exe")
    set(_msys2_ninja "${_msys2_bin}/ninja.exe")

    if (NOT EXISTS "${_msys2_gcc}")
        message(FATAL_ERROR
            "codec2: MinGW-w64 GCC not found at ${_msys2_gcc}.\n"
            "Install MSYS2 and the build tools by following doc/install-mingw.md, "
            "then re-run the deps configure.")
    endif ()

    set(_prefix "${${PROJECT_NAME}_DEP_INSTALL_PREFIX}")
    set(_invoke "${CMAKE_CURRENT_LIST_DIR}/codec2_mingw.cmake")

    ExternalProject_Add(dep_codec2
        GIT_REPOSITORY  https://github.com/drowe67/codec2
        # TODO: pin to a specific tag once we confirm what's current upstream.
        GIT_TAG         main
        GIT_SHALLOW     ON
        DOWNLOAD_DIR    ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/codec2
        BINARY_DIR      ${CMAKE_CURRENT_BINARY_DIR}/builds/codec2
        INSTALL_DIR     ${_prefix}
        PATCH_COMMAND   ${CMAKE_COMMAND}
                            -DSRC=<SOURCE_DIR>
                            -P ${CMAKE_CURRENT_LIST_DIR}/patch_codec2.cmake
        CONFIGURE_COMMAND
            ${CMAKE_COMMAND}
                -DMINGW_BIN=${_msys2_bin}
                -DMINGW_CMAKE=${_msys2_cmake}
                -DMINGW_NINJA=${_msys2_ninja}
                -DSOURCE_DIR=<SOURCE_DIR>
                -DBINARY_DIR=<BINARY_DIR>
                -DINSTALL_PREFIX=${_prefix}
                -DMODE=configure
                -P ${_invoke}
        BUILD_COMMAND
            ${CMAKE_COMMAND}
                -DMINGW_BIN=${_msys2_bin}
                -DMINGW_CMAKE=${_msys2_cmake}
                -DBINARY_DIR=<BINARY_DIR>
                -DMODE=build
                -P ${_invoke}
        INSTALL_COMMAND
            ${CMAKE_COMMAND}
                -DMINGW_BIN=${_msys2_bin}
                -DMINGW_CMAKE=${_msys2_cmake}
                -DBINARY_DIR=<BINARY_DIR>
                -DMODE=install
                -P ${_invoke}
    )
    return()
endif ()

add_cmake_project(codec2
    GIT_REPOSITORY https://github.com/drowe67/codec2
    # TODO: pin to a specific tag once we confirm what's current upstream.
    GIT_TAG        main
    GIT_SHALLOW    ON
    CMAKE_ARGS
        -DUNITTEST=OFF
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
