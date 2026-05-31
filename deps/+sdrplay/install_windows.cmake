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
# prefix, mirroring what install_sdrplay.cmake does for Linux/macOS. The
# file searches use file(GLOB_RECURSE) so a future SDRplay release with a
# slightly different inner layout still works without recipe surgery.
#

foreach (_var SCRATCH ARCH PREFIX)
    if (NOT DEFINED ${_var})
        message(FATAL_ERROR "install_windows.cmake: ${_var} not set")
    endif ()
endforeach ()

if (NOT IS_DIRECTORY "${SCRATCH}")
    message(FATAL_ERROR "install_windows.cmake: SCRATCH dir not found: ${SCRATCH}")
endif ()

# Headers — identified by sdrplay_api.h.
file(GLOB_RECURSE _header_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/**/sdrplay_api.h")
list(LENGTH _header_candidates _header_count)
if (_header_count EQUAL 0)
    message(FATAL_ERROR
        "install_windows.cmake: sdrplay_api.h not found anywhere under ${SCRATCH}")
endif ()
list(GET _header_candidates 0 _header_path)
get_filename_component(_inc_src_dir "${_header_path}" DIRECTORY)

# Architecture-specific .lib.
file(GLOB_RECURSE _lib_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/**/${ARCH}/sdrplay_api.lib")
list(LENGTH _lib_candidates _lib_count)
if (_lib_count EQUAL 0)
    message(FATAL_ERROR
        "install_windows.cmake: sdrplay_api.lib for arch '${ARCH}' not found under ${SCRATCH}")
endif ()
list(GET _lib_candidates 0 _lib_path)

# Architecture-specific .dll.
file(GLOB_RECURSE _dll_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/**/${ARCH}/sdrplay_api.dll")
list(LENGTH _dll_candidates _dll_count)
if (_dll_count EQUAL 0)
    message(FATAL_ERROR
        "install_windows.cmake: sdrplay_api.dll for arch '${ARCH}' not found under ${SCRATCH}")
endif ()
list(GET _dll_candidates 0 _dll_path)

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
