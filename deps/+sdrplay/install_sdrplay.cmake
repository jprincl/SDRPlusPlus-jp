#
# Run with:
#   cmake -DSCRATCH=<dir> -DPREFIX=<install-prefix>
#         -DSO_VERSIONED=libsdrplay_api.so.X.Y
#         -DSO_SONAME=libsdrplay_api.so.X
#         [-DARCH=<arch-subdir>]
#         -P install_sdrplay.cmake
#
# Drives both the Linux (Makeself .run extracted by 7z) and macOS
# (.pkg expanded by `pkgutil --expand-full`) install flows. Differences
# we paper over:
#
#   - Linux .run holds per-arch subdirectories (x86_64/, aarch64/,
#     armv7l/, i686/). Caller passes ARCH so we can prefer the matching
#     subtree; without ARCH (macOS universal .pkg) we just glob.
#   - macOS .pkg sometimes drops the .so without the exact minor-version
#     suffix (libsdrplay_api.so.<major> or unversioned). The fallback
#     glob accepts any libsdrplay_api.so.*; we rename to SO_VERSIONED so
#     the SONAME / linker-name symlinks below resolve to a real file.
#
# SDRplay uses the .so extension on both Linux and macOS (peculiar for
# macOS but consistent across their tooling).
#

foreach (_var SCRATCH PREFIX SO_VERSIONED SO_SONAME)
    if (NOT DEFINED ${_var})
        message(FATAL_ERROR "install_sdrplay.cmake: ${_var} not set")
    endif ()
endforeach ()

if (NOT IS_DIRECTORY "${SCRATCH}")
    message(FATAL_ERROR "install_sdrplay.cmake: SCRATCH dir not found: ${SCRATCH}")
endif ()

# Locate the versioned .so. Search order: arch-specific subdir (if ARCH was
# passed), then SO_VERSIONED anywhere under scratch, then any
# libsdrplay_api.so.* as a last resort. Each step only fires if the previous
# turned up nothing.
set(_so_candidates "")
if (DEFINED ARCH AND NOT "${ARCH}" STREQUAL "")
    file(GLOB_RECURSE _so_candidates
        LIST_DIRECTORIES FALSE
        "${SCRATCH}/*/${ARCH}/${SO_VERSIONED}"
        "${SCRATCH}/${ARCH}/${SO_VERSIONED}")
endif ()
if (NOT _so_candidates)
    file(GLOB_RECURSE _so_candidates
        LIST_DIRECTORIES FALSE
        "${SCRATCH}/**/${SO_VERSIONED}")
endif ()
if (NOT _so_candidates)
    file(GLOB_RECURSE _so_candidates
        LIST_DIRECTORIES FALSE
        "${SCRATCH}/**/libsdrplay_api.so.*")
endif ()
if (NOT _so_candidates)
    file(GLOB_RECURSE _all_contents LIST_DIRECTORIES FALSE "${SCRATCH}/*")
    string(REPLACE ";" "\n  " _listing "${_all_contents}")
    message(FATAL_ERROR
        "install_sdrplay.cmake: ${SO_VERSIONED} not found under ${SCRATCH}. "
        "Contents:\n  ${_listing}")
endif ()

# Prefer the candidate whose basename matches SO_VERSIONED exactly so the
# fallback glob (which also catches symlinks like libsdrplay_api.so.3) does
# not preempt the real versioned file when both are present.
set(_so_path "")
foreach (_candidate IN LISTS _so_candidates)
    get_filename_component(_name "${_candidate}" NAME)
    if ("${_name}" STREQUAL "${SO_VERSIONED}")
        set(_so_path "${_candidate}")
        break ()
    endif ()
endforeach ()
if ("${_so_path}" STREQUAL "")
    list(GET _so_candidates 0 _so_path)
endif ()

# Locate the headers via sdrplay_api.h.
file(GLOB_RECURSE _header_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/**/sdrplay_api.h")
list(LENGTH _header_candidates _header_count)
if (_header_count EQUAL 0)
    message(FATAL_ERROR
        "install_sdrplay.cmake: sdrplay_api.h not found anywhere under ${SCRATCH}")
endif ()
list(GET _header_candidates 0 _header_path)
get_filename_component(_inc_src_dir "${_header_path}" DIRECTORY)

message(STATUS "install_sdrplay.cmake: lib  = ${_so_path}")
message(STATUS "install_sdrplay.cmake: inc  = ${_inc_src_dir}")
message(STATUS "install_sdrplay.cmake: dest = ${PREFIX}")

file(MAKE_DIRECTORY "${PREFIX}/lib")
file(MAKE_DIRECTORY "${PREFIX}/include/SDRplay")

# FOLLOW_SYMLINK_CHAIN dereferences any symlink, so we get the real .so file
# at PREFIX/lib. If its source basename differs from SO_VERSIONED (macOS
# fallback case), rename it so the symlinks below have a real target.
file(COPY "${_so_path}"
    DESTINATION "${PREFIX}/lib"
    FOLLOW_SYMLINK_CHAIN)
get_filename_component(_so_basename "${_so_path}" NAME)
if (NOT "${_so_basename}" STREQUAL "${SO_VERSIONED}")
    file(RENAME "${PREFIX}/lib/${_so_basename}" "${PREFIX}/lib/${SO_VERSIONED}")
endif ()

# Recreate the SONAME symlink (libsdrplay_api.so.<major>) — consumer plugins
# get this as their NEEDED entry from SDRplay's embedded SONAME — and the
# linker-name symlink (libsdrplay_api.so) so find_library(sdrplay_api)
# resolves at link time.
foreach (_link "${PREFIX}/lib/libsdrplay_api.so" "${PREFIX}/lib/${SO_SONAME}")
    file(REMOVE "${_link}")
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
        file(CREATE_LINK "${SO_VERSIONED}" "${_link}" SYMBOLIC)
    else ()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${SO_VERSIONED}" "${_link}"
            RESULT_VARIABLE _ln_rc)
        if (NOT _ln_rc EQUAL 0)
            message(FATAL_ERROR "install_sdrplay.cmake: failed to create symlink ${_link} -> ${SO_VERSIONED}")
        endif ()
    endif ()
endforeach ()

file(GLOB _hdrs "${_inc_src_dir}/*.h")
foreach (_h IN LISTS _hdrs)
    file(COPY "${_h}" DESTINATION "${PREFIX}/include/SDRplay")
endforeach ()
