#
# zlib — transitive dependency of libxml2 (and likely libbladeRF). Pure-C
# library; safe to share between Release and Debug consumers, hence listed
# in DEP_DEBUG_EXCLUDES in the parent CMakeLists.
#
# CMake's built-in FindZLIB module resolves headers + library against
# CMAKE_PREFIX_PATH, which AddCMakeProject already plumbs into each
# downstream sub-build.
#
add_cmake_project(zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    CMAKE_ARGS
        -DSKIP_INSTALL_FILES=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
