#
# libxml2 — transitive dependency of libiio. Recipe matches the version
# pinned in the Android kit (2.9.14).
#
add_cmake_project(libxml2
    URL https://download.gnome.org/sources/libxml2/2.9/libxml2-2.9.14.tar.xz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    CMAKE_ARGS
        -DLIBXML2_WITH_LZMA=OFF
        -DLIBXML2_WITH_PYTHON=OFF
        -DLIBXML2_WITH_ICONV=OFF
        -DLIBXML2_WITH_TESTS=OFF
        -DLIBXML2_WITH_PROGRAMS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

# Android's NDK supplies libz from the sysroot.
if (NOT ANDROID)
    set(DEP_libxml2_DEPENDS zlib)
endif ()

sdrpp_validate_dep(libxml2
    TARGET         LibXml2::LibXml2
    LIB_NAMES      libxml2s xml2 libxml2
    HEADER         parser.h
    INCLUDE_SUBDIR libxml2/libxml
    CONFIG_SUBDIR  libxml2-2.9.14
    REQUIRES_CONFIG)
