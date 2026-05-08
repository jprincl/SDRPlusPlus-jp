#
# Run with -DSRC=<source-dir> -P this-script.
#
# libperseus-sdr has no install() rules at all, so cmake --target install
# produces "unknown target 'install'".  Add the necessary rules.
#
set(_f "${SRC}/CMakeLists.txt")
file(READ "${_f}" _content)

# Idempotency guard
if (_content MATCHES "SDR\\+\\+ deps build: install rules")
    message(STATUS "patch_libperseus_sdr: already patched, skipping")
    return()
endif ()

set(_install_rules "
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

string(APPEND _content "${_install_rules}")
file(WRITE "${_f}" "${_content}")
message(STATUS "patch_libperseus_sdr: patched ${_f}")
