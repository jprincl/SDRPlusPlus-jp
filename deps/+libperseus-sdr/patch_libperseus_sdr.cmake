#
# Run with -DSRC=<source-dir> -P this-script.
#
# Patches libperseus-sdr's CMakeLists.txt:
#   1. Drops the explicit SHARED keyword from add_library(perseus-sdr ...) so
#      the library type follows BUILD_SHARED_LIBS. Without this, the dep
#      always builds shared even when the policy resolves to static (per
#      DepClassification.cmake, libperseus-sdr is static on every profile),
#      producing perseus-sdr.dll + an import lib named perseus-sdr.lib that
#      the validator correctly flags as a static/shared mismatch (same issue
#      libfobos had before its own patch landed).
#   2. Appends install rules — upstream ships none, so cmake --target install
#      would otherwise fail with "unknown target 'install'".
#
# Patches libperseus-sdr's src/perseus-sdr.h:
#   3. Fixes the dbgprintf / errorset variadic macros: upstream writes
#      `format, __VA_ARGS__`, which leaves a trailing comma when callers
#      pass no variadic args (e.g. errorset(PERSEUS_NOMEM, "can't alloc")).
#      Modern GCC rejects this as "expected expression before ')'". Switch
#      to the GCC/Clang/MSVC-supported `, ##__VA_ARGS__` form which swallows
#      the comma when __VA_ARGS__ is empty.
#
# Patches libperseus-sdr's CMakeLists.txt (continued):
#   4. Gates the pkg_check_modules(libusb-1.0) call on LIBUSB not already
#      being resolved by the caller. The parent autobuild passes
#      -DLIBUSB_INCLUDE_DIRS / -DLIBUSB_LIBRARIES / -DLIBUSB_FOUND when libusb
#      is built as a bundled dep (no system .pc file), but upstream's
#      unconditional pkg_check_modules ignores those and fails the configure.
#
# Patches libperseus-sdr's src/perseus-sdr.c:
#   5. Adds <unistd.h> on non-Windows so sleep() is declared. GCC 14
#      (Debian trixie / Ubuntu noble) promotes -Wimplicit-function-declaration
#      to an error, so the missing include is no longer tolerated.
#   6. Gates the Windows-only Sleep() call behind _WIN32 and uses usleep()
#      on POSIX. Same GCC 14 implicit-declaration error otherwise.
#

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_helpers.cmake)

set(_f "${SRC}/CMakeLists.txt")
file(READ "${_f}" _content)

# --- Patch 1: make library type follow BUILD_SHARED_LIBS ---
patch_replace_or_fail(_content
    "add_library(perseus-sdr SHARED \${SRC})"
    "add_library(perseus-sdr \${SRC})")

# --- Patch 4: skip pkg_check_modules when LIBUSB is pre-resolved by parent ---
patch_replace_or_fail(_content
    "    find_package(PkgConfig)

    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)"
    "    if (NOT LIBUSB_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    endif ()")

# --- Patch 2: append install rules (upstream has none) ---
# Idempotency via marker — patch_replace_or_fail can't help here because this
# is an append, not a substitution.
if (NOT _content MATCHES "SDR\\+\\+ deps build: install rules")
    string(APPEND _content "
# >>> SDR++ deps build: install rules (none existed upstream)
include(GNUInstallDirs)
install(TARGETS perseus-sdr
    RUNTIME DESTINATION \${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION \${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION \${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    \${CMAKE_CURRENT_SOURCE_DIR}/src/perseus-sdr.h
    DESTINATION \${CMAKE_INSTALL_INCLUDEDIR}/perseus-sdr
)
# <<< SDR++ deps build
")
endif ()

file(WRITE "${_f}" "${_content}")

# --- Patch 3: fix variadic-macro trailing-comma in perseus-sdr.h ---
set(_h "${SRC}/src/perseus-sdr.h")
file(READ "${_h}" _hcontent)

patch_replace_or_fail(_hcontent
    "fprintf(stderr, format, __VA_ARGS__)"
    "fprintf(stderr, format, ##__VA_ARGS__)")

patch_replace_or_fail(_hcontent
    "snprintf(perseus_error_str, sizeof(perseus_error_str) - 1, format, __VA_ARGS__)"
    "snprintf(perseus_error_str, sizeof(perseus_error_str) - 1, format, ##__VA_ARGS__)")

file(WRITE "${_h}" "${_hcontent}")

# --- Patches 5 & 6: GCC 14 implicit-declaration fixes in perseus-sdr.c ---
set(_c "${SRC}/src/perseus-sdr.c")
file(READ "${_c}" _ccontent)

patch_replace_or_fail(_ccontent
    "#if !defined(_WIN32)
#include <dlfcn.h>
#else
#include <windows.h>
#endif"
    "#if !defined(_WIN32)
#include <dlfcn.h>
#include <unistd.h>
#else
#include <windows.h>
#endif")

patch_replace_or_fail(_ccontent
    "            Sleep(1);"
    "#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif")

file(WRITE "${_c}" "${_ccontent}")
