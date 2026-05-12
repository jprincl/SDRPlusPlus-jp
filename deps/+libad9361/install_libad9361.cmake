#
# Copies whatever ad9361 artifacts were produced by the build (.dll/.lib/.pdb
# on MSVC, .so on UNIX) into the install prefix. Driven from the libad9361
# recipe's INSTALL_COMMAND because upstream's own install target is broken.
#

set(_artifact_dirs "${SRC}")
foreach (_config IN ITEMS "${CONFIG}" "${CMAKE_BUILD_TYPE}" RelWithDebInfo Release Debug MinSizeRel)
    if (NOT "${_config}" STREQUAL "")
        list(APPEND _artifact_dirs "${SRC}/${_config}")
    endif ()
endforeach ()
list(REMOVE_DUPLICATES _artifact_dirs)

set(_dll_files "")
set(_lib_files "")
set(_pdb_files "")
foreach (_dir IN LISTS _artifact_dirs)
    if (NOT EXISTS "${_dir}")
        continue()
    endif ()
    file(GLOB _dir_dll_files "${_dir}/*.dll")
    file(GLOB _dir_lib_files "${_dir}/*.lib" "${_dir}/*.so" "${_dir}/*.so.*")
    file(GLOB _dir_pdb_files "${_dir}/*.pdb")
    list(APPEND _dll_files ${_dir_dll_files})
    list(APPEND _lib_files ${_dir_lib_files})
    list(APPEND _pdb_files ${_dir_pdb_files})
endforeach ()

foreach (f ${_dll_files} ${_pdb_files})
    file(COPY ${f} DESTINATION ${DST_BIN})
endforeach ()
foreach (f ${_lib_files})
    file(COPY ${f} DESTINATION ${DST_LIB})
endforeach ()
