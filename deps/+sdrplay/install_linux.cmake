#
# Run with:
#   cmake -DSCRATCH=<dir> -DARCH=<x86_64|aarch64|...> -DPREFIX=<install-prefix>
#         -DSO_VERSIONED=libsdrplay_api.so.X.Y -P install_linux.cmake
#
# The SDRplay Linux installer's inner tar has varied across versions in whether
# it's rooted at the top of the tar or wrapped in a SDRplay_RSP_API-Linux-X.Y.Z
# directory. Rather than hard-code one layout, locate the .so and the headers
# by globbing under <scratch> and copy them into the deps prefix.
#

foreach (_var SCRATCH ARCH PREFIX SO_VERSIONED SO_SONAME)
    if (NOT DEFINED ${_var})
        message(FATAL_ERROR "install_linux.cmake: ${_var} not set")
    endif ()
endforeach ()

if (NOT IS_DIRECTORY "${SCRATCH}")
    message(FATAL_ERROR "install_linux.cmake: SCRATCH dir not found: ${SCRATCH}")
endif ()

# Locate the architecture-specific .so. Search by exact versioned name so we
# don't pick up symlinks the installer may also ship.
file(GLOB_RECURSE _so_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/*/${ARCH}/${SO_VERSIONED}")
list(LENGTH _so_candidates _so_count)
if (_so_count EQUAL 0)
    # Fall back to scanning anywhere under scratch — the layout could omit the
    # arch subdir or wrap things differently in future releases.
    file(GLOB_RECURSE _so_candidates
        LIST_DIRECTORIES FALSE
        "${SCRATCH}/${ARCH}/${SO_VERSIONED}"
        "${SCRATCH}/**/${SO_VERSIONED}")
    list(LENGTH _so_candidates _so_count)
endif ()
if (_so_count EQUAL 0)
    file(GLOB_RECURSE _all_contents LIST_DIRECTORIES FALSE "${SCRATCH}/*")
    string(REPLACE ";" "\n  " _listing "${_all_contents}")
    message(FATAL_ERROR
        "install_linux.cmake: ${SO_VERSIONED} not found under ${SCRATCH}/${ARCH}/. "
        "Contents:\n  ${_listing}")
endif ()
list(GET _so_candidates 0 _so_path)

# Locate the headers directory. The header set is identified by sdrplay_api.h.
file(GLOB_RECURSE _header_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/*/sdrplay_api.h")
list(LENGTH _header_candidates _header_count)
if (_header_count EQUAL 0)
    message(FATAL_ERROR
        "install_linux.cmake: sdrplay_api.h not found anywhere under ${SCRATCH}")
endif ()
list(GET _header_candidates 0 _header_path)
get_filename_component(_inc_src_dir "${_header_path}" DIRECTORY)

message(STATUS "install_linux.cmake: lib  = ${_so_path}")
message(STATUS "install_linux.cmake: inc  = ${_inc_src_dir}")
message(STATUS "install_linux.cmake: dest = ${PREFIX}")

file(MAKE_DIRECTORY "${PREFIX}/lib")
file(MAKE_DIRECTORY "${PREFIX}/include/SDRplay")

file(COPY "${_so_path}"
    DESTINATION "${PREFIX}/lib"
    FOLLOW_SYMLINK_CHAIN)

# Recreate the linker-name symlink so find_library(sdrplay_api) resolves, and
# the SONAME-name symlink (e.g. libsdrplay_api.so.3) so the dynamic linker /
# linuxdeploy can resolve the NEEDED entry baked into consumers at link time.
foreach (_link "${PREFIX}/lib/libsdrplay_api.so" "${PREFIX}/lib/${SO_SONAME}")
    file(REMOVE "${_link}")
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
        file(CREATE_LINK "${SO_VERSIONED}" "${_link}" SYMBOLIC)
    else ()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${SO_VERSIONED}" "${_link}"
            RESULT_VARIABLE _ln_rc)
        if (NOT _ln_rc EQUAL 0)
            message(FATAL_ERROR "install_linux.cmake: failed to create symlink ${_link} -> ${SO_VERSIONED}")
        endif ()
    endif ()
endforeach ()

file(GLOB _hdrs "${_inc_src_dir}/*.h")
foreach (_h IN LISTS _hdrs)
    file(COPY "${_h}" DESTINATION "${PREFIX}/include/SDRplay")
endforeach ()
