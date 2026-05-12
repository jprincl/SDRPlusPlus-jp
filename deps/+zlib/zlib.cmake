#
# zlib — transitive dependency of libxml2 (and likely libbladeRF).
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

# Upstream installs Config.cmake under the package name 'ZLIB' (uppercase),
# but consumers use CMake's built-in FindZLIB anyway — file-existence probes
# are the right check here.
sdrpp_validate_dep(zlib
    TARGET           ZLIB::ZLIB
    LIB_NAMES        zlib z
    STATIC_LIB_NAMES zlibstatic
    SHARED_LIB_NAMES zlib z
    DLL_NAMES        zlib1.dll zlib.dll
    HEADER           zlib.h)
