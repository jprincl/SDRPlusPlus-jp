#
# Run with -DSCRATCH=<dir> -DARCH=<x86|x64> -DPREFIX=<install-prefix>
#         -P install_windows.cmake
#
# Operates on the directory tree produced by `innoextract --silent
# --exclude-temp --output-dir <scratch> SDRplay_RSP_API-Windows-*.exe`.
# Inno Setup's default layout puts everything under an `app/` root:
#
#   SCRATCH/app/API/inc/*.h
#   SCRATCH/app/API/x86/sdrplay_api.{dll,lib}
#   SCRATCH/app/API/x64/sdrplay_api.{dll,lib}
#   SCRATCH/app/API/arm64/...     (present in the installer, unused — no
#                                  ARM64 Windows kernel driver exists)
#   SCRATCH/app/Drivers/...       (kernel driver; ignored — end-user concern)
#   SCRATCH/app/sdrplay_apiService.exe  (system service; ignored)
#
# We copy headers and the architecture-specific lib/dll into the deps
# prefix, mirroring what install_sdrplay.cmake does for Linux/macOS.
#
# The direct paths come first because they're zero-cost and unambiguous;
# the file(GLOB_RECURSE) fallback only kicks in if a future SDRplay
# release rearranges the inner layout. Note that CMake's glob `*` matches
# `[^/]*` (single path segment, no slashes), so a recursive search for a
# file at known relative depth needs either an exact depth pattern or
# matching against the filename alone — we use the latter.
#

foreach (_var SCRATCH ARCH PREFIX)
    if (NOT DEFINED ${_var})
        message(FATAL_ERROR "install_windows.cmake: ${_var} not set")
    endif ()
endforeach ()

if (NOT IS_DIRECTORY "${SCRATCH}")
    message(FATAL_ERROR "install_windows.cmake: SCRATCH dir not found: ${SCRATCH}")
endif ()

# Helper: search for a file by basename anywhere under SCRATCH (recursing
# through subdirectories), then if ARG_PARENT_NAME is given, prefer the
# candidate whose immediate parent dir matches that name. Used to find the
# arch-specific .dll / .lib when the simple direct path lookup fails.
function(_sdrplay_find_in_scratch out_var basename)
    cmake_parse_arguments(_p "" "PARENT_NAME" "" ${ARGN})
    set(_match "")
    file(GLOB_RECURSE _all
        LIST_DIRECTORIES FALSE
        "${SCRATCH}/${basename}")
    if (_p_PARENT_NAME)
        foreach (_c IN LISTS _all)
            get_filename_component(_parent "${_c}" DIRECTORY)
            get_filename_component(_pname "${_parent}" NAME)
            if ("${_pname}" STREQUAL "${_p_PARENT_NAME}")
                set(_match "${_c}")
                break ()
            endif ()
        endforeach ()
    elseif (_all)
        list(GET _all 0 _match)
    endif ()
    set(${out_var} "${_match}" PARENT_SCOPE)
endfunction()

# Architecture-specific .lib. Direct path first (innoextract output layout),
# fallback to recursive filename search filtered by arch-named parent dir.
set(_lib_path "${SCRATCH}/app/API/${ARCH}/sdrplay_api.lib")
if (NOT EXISTS "${_lib_path}")
    _sdrplay_find_in_scratch(_lib_path "sdrplay_api.lib" PARENT_NAME "${ARCH}")
endif ()
if (NOT EXISTS "${_lib_path}")
    message(FATAL_ERROR
        "install_windows.cmake: sdrplay_api.lib for arch '${ARCH}' not found under ${SCRATCH}")
endif ()

# Architecture-specific .dll.
set(_dll_path "${SCRATCH}/app/API/${ARCH}/sdrplay_api.dll")
if (NOT EXISTS "${_dll_path}")
    _sdrplay_find_in_scratch(_dll_path "sdrplay_api.dll" PARENT_NAME "${ARCH}")
endif ()
if (NOT EXISTS "${_dll_path}")
    message(FATAL_ERROR
        "install_windows.cmake: sdrplay_api.dll for arch '${ARCH}' not found under ${SCRATCH}")
endif ()

# Headers — identified by sdrplay_api.h.
set(_header_path "${SCRATCH}/app/API/inc/sdrplay_api.h")
if (NOT EXISTS "${_header_path}")
    _sdrplay_find_in_scratch(_header_path "sdrplay_api.h")
endif ()
if (NOT EXISTS "${_header_path}")
    message(FATAL_ERROR
        "install_windows.cmake: sdrplay_api.h not found anywhere under ${SCRATCH}")
endif ()
get_filename_component(_inc_src_dir "${_header_path}" DIRECTORY)

message(STATUS "install_windows.cmake: lib  = ${_lib_path}")
message(STATUS "install_windows.cmake: dll  = ${_dll_path}")
message(STATUS "install_windows.cmake: inc  = ${_inc_src_dir}")
message(STATUS "install_windows.cmake: dest = ${PREFIX}")

file(MAKE_DIRECTORY "${PREFIX}/lib")
file(MAKE_DIRECTORY "${PREFIX}/bin")
file(MAKE_DIRECTORY "${PREFIX}/include/SDRplay")

file(COPY "${_lib_path}" DESTINATION "${PREFIX}/lib")
file(COPY "${_dll_path}" DESTINATION "${PREFIX}/bin")

file(GLOB _hdrs "${_inc_src_dir}/*.h")
foreach (_h IN LISTS _hdrs)
    file(COPY "${_h}" DESTINATION "${PREFIX}/include/SDRplay")
endforeach ()
