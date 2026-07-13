#
# librtlsdr — built from the AlexandreRouma fork (matches the Android kit).
#
add_cmake_project(librtlsdr
    GIT_REPOSITORY https://github.com/AlexandreRouma/rtl-sdr
    # master @ 2026-05-31; bump when intentional.
    GIT_TAG        ddd6811b71359c299cf31b4e3e75fbb1ed5c964d
    GIT_SHALLOW    OFF
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_librtlsdr.cmake
    INSTALL_COMMAND
        ${CMAKE_COMMAND} --build . --target install --config ${CMAKE_BUILD_TYPE}
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${SDRPP_DEPS_INSTALL_PREFIX}
            -P ${CMAKE_CURRENT_LIST_DIR}/fix_librtlsdr_config.cmake
    CMAKE_ARGS
        -DBUILD_UTILITIES=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_librtlsdr_DEPENDS libusb)

sdrpp_validate_dep(librtlsdr
    PACKAGE_NAME     rtlsdr
    TARGET           rtlsdr::rtlsdr
    STATIC_TARGET    rtlsdr::rtlsdr_static
    SHARED_TARGET    rtlsdr::rtlsdr
    LIB_NAMES        rtlsdr
    # Upstream's static target keeps the `_static` suffix on Windows but
    # overrides OUTPUT_NAME back to `rtlsdr` on UNIX, producing `librtlsdr.a`
    # alongside `librtlsdr.so*`. List both basenames so find_library matches
    # either install layout.
    STATIC_LIB_NAMES rtlsdr_static rtlsdr
    SHARED_LIB_NAMES rtlsdr
    DLL_NAMES        rtlsdr.dll
    HEADER           rtl-sdr.h
    CONFIG_SUBDIR    rtlsdr
    REQUIRES_CONFIG)
