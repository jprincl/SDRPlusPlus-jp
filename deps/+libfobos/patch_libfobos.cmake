#
# Run with -DSRC=<source-dir> -P this-script.
#
# Patches libfobos's CMakeLists.txt:
#   1. Removes the hardcoded CMAKE_INSTALL_PREFIX so our destdir prefix is used.
#   2. Applies the generic PkgConfig stub (idempotent).
#

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_helpers.cmake)

set(_f "${SRC}/CMakeLists.txt")
if (NOT EXISTS "${_f}")
    message(FATAL_ERROR "patch_libfobos: ${_f} not found")
endif ()

file(READ "${_f}" _content)

# --- Patch 1: remove hardcoded install prefix ---
# Upstream sets: set(CMAKE_INSTALL_PREFIX "C:/Program Files/RigExpert/Fobos/")
# which would override the deps destdir.  Drop it.
patch_replace_or_fail(_content
    "set(CMAKE_INSTALL_PREFIX \"C:/Program Files/RigExpert/Fobos/\")"
    "# CMAKE_INSTALL_PREFIX hardcode removed by SDR++ deps build")


# --- Patch 2: remove bundled libusb-1.0.dll install rule ---
# libfobos ships install(FILES $<TARGET_FILE_DIR:fobos>/libusb-1.0.dll ...)
# which tries to copy a DLL from the build dir that doesn't exist because we
# link against our own libusb from the destdir.  libusb is already installed
# by dep_libusb, so just drop this rule.
string(REGEX REPLACE
    "install[ \t]*\\([ \t]*FILES[ \t]+\\$<TARGET_FILE_DIR:fobos>/libusb-1\\.0\\.dll[^\n]*\n?"
    "# libusb-1.0.dll install removed by SDR++ deps build (libusb built separately)\n"
    _content "${_content}")

file(WRITE "${_f}" "${_content}")

# --- Patch 3: PkgConfig stub (shared, idempotent) ---
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_pkgconfig.cmake)
