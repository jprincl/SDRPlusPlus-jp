#
# Run with:
#   cmake -DSCRATCH=<dir> -DPREFIX=<install-prefix>
#         -DSO_VERSIONED=libsdrplay_api.so.X.Y
#         -DSO_SONAME=libsdrplay_api.so.X -P install_macos.cmake
#
# Operates on the directory tree produced by `pkgutil --expand-full` on
# SDRplayAPI-macos-installer-universal-*.pkg. The exact layout inside the
# expanded tree varies across SDRplay releases (and across whether the .pkg
# is product-level or component-level), so we locate the .so and the headers
# by globbing under <scratch> rather than hard-coding a path.
#
# SDRplay's macOS dylib uses the `.so` extension (not `.dylib`) — peculiar
# but consistent with how they package the API.
#

foreach (_var SCRATCH PREFIX SO_VERSIONED SO_SONAME)
    if (NOT DEFINED ${_var})
        message(FATAL_ERROR "install_macos.cmake: ${_var} not set")
    endif ()
endforeach ()

if (NOT IS_DIRECTORY "${SCRATCH}")
    message(FATAL_ERROR "install_macos.cmake: SCRATCH dir not found: ${SCRATCH}")
endif ()

# Locate the versioned dylib (libsdrplay_api.so.X.Y, e.g. libsdrplay_api.so.3.15).
# Search by exact name so symlinks the .pkg payload ships don't get picked first.
file(GLOB_RECURSE _so_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/**/${SO_VERSIONED}")
list(LENGTH _so_candidates _so_count)
if (_so_count EQUAL 0)
    # Fallback: SDRplay sometimes drops a single .so without the minor-version
    # suffix (libsdrplay_api.so or libsdrplay_api.so.<major>). Accept any
    # libsdrplay_api.so.* that's a real file.
    file(GLOB_RECURSE _so_candidates
        LIST_DIRECTORIES FALSE
        "${SCRATCH}/**/libsdrplay_api.so.*")
    list(LENGTH _so_candidates _so_count)
endif ()
if (_so_count EQUAL 0)
    file(GLOB_RECURSE _all_contents LIST_DIRECTORIES FALSE "${SCRATCH}/*")
    string(REPLACE ";" "\n  " _listing "${_all_contents}")
    message(FATAL_ERROR
        "install_macos.cmake: ${SO_VERSIONED} not found under ${SCRATCH}. "
        "Contents:\n  ${_listing}")
endif ()

# Prefer the candidate that matches SO_VERSIONED exactly; otherwise take the
# first one (and rename to SO_VERSIONED in the destination).
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

# Locate the headers directory via sdrplay_api.h.
file(GLOB_RECURSE _header_candidates
    LIST_DIRECTORIES FALSE
    "${SCRATCH}/**/sdrplay_api.h")
list(LENGTH _header_candidates _header_count)
if (_header_count EQUAL 0)
    message(FATAL_ERROR
        "install_macos.cmake: sdrplay_api.h not found anywhere under ${SCRATCH}")
endif ()
list(GET _header_candidates 0 _header_path)
get_filename_component(_inc_src_dir "${_header_path}" DIRECTORY)

message(STATUS "install_macos.cmake: lib  = ${_so_path}")
message(STATUS "install_macos.cmake: inc  = ${_inc_src_dir}")
message(STATUS "install_macos.cmake: dest = ${PREFIX}")

file(MAKE_DIRECTORY "${PREFIX}/lib")
file(MAKE_DIRECTORY "${PREFIX}/include/SDRplay")

# file(COPY) preserves symlinks, but we want a real file at the canonical
# SO_VERSIONED name under PREFIX/lib (so the SONAME / linker-name symlinks
# below resolve). Pick the real file via FOLLOW_SYMLINK_CHAIN, then rename
# if its filename differs from SO_VERSIONED.
file(COPY "${_so_path}"
    DESTINATION "${PREFIX}/lib"
    FOLLOW_SYMLINK_CHAIN)
get_filename_component(_so_basename "${_so_path}" NAME)
if (NOT "${_so_basename}" STREQUAL "${SO_VERSIONED}")
    file(RENAME "${PREFIX}/lib/${_so_basename}" "${PREFIX}/lib/${SO_VERSIONED}")
endif ()

# Recreate the linker-name and SONAME symlinks. The plugin gets NEEDED entry
# from SDRplay's SONAME (libsdrplay_api.so.<major>) at link time, and the
# unversioned libsdrplay_api.so makes find_library(sdrplay_api) resolve.
foreach (_link "${PREFIX}/lib/libsdrplay_api.so" "${PREFIX}/lib/${SO_SONAME}")
    file(REMOVE "${_link}")
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
        file(CREATE_LINK "${SO_VERSIONED}" "${_link}" SYMBOLIC)
    else ()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${SO_VERSIONED}" "${_link}"
            RESULT_VARIABLE _ln_rc)
        if (NOT _ln_rc EQUAL 0)
            message(FATAL_ERROR "install_macos.cmake: failed to create symlink ${_link} -> ${SO_VERSIONED}")
        endif ()
    endif ()
endforeach ()

file(GLOB _hdrs "${_inc_src_dir}/*.h")
foreach (_h IN LISTS _hdrs)
    file(COPY "${_h}" DESTINATION "${PREFIX}/include/SDRplay")
endforeach ()
