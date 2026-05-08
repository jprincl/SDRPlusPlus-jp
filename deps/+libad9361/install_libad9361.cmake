#
# Copies whatever ad9361 artifacts were produced by the build (.dll/.lib/.pdb
# on MSVC, .so on UNIX) into the install prefix. Driven from the libad9361
# recipe's INSTALL_COMMAND because upstream's own install target is broken.
#
file(GLOB _dll_files "${SRC}/*.dll" "${SRC}/${CMAKE_BUILD_TYPE}/*.dll")
file(GLOB _lib_files "${SRC}/*.lib" "${SRC}/${CMAKE_BUILD_TYPE}/*.lib" "${SRC}/*.so" "${SRC}/*.so.*")
file(GLOB _pdb_files "${SRC}/*.pdb" "${SRC}/${CMAKE_BUILD_TYPE}/*.pdb")

foreach (f ${_dll_files} ${_pdb_files})
    file(COPY ${f} DESTINATION ${DST_BIN})
endforeach ()
foreach (f ${_lib_files})
    file(COPY ${f} DESTINATION ${DST_LIB})
endforeach ()
