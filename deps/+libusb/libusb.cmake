#
# libusb — built via the libusb-cmake wrapper project, which pulls in mainline
# libusb as a git submodule and provides a proper CMakeLists.txt. This lets
# us treat libusb like any other dep (Ninja-driven, no msbuild specialcase,
# no vcxproj patching).
#
# Output layout matches mainline libusb's convention:
#   <prefix>/bin/libusb-1.0.dll        (Windows shared lib)
#   <prefix>/lib/libusb-1.0.lib        (Windows import lib)
#   <prefix>/include/libusb-1.0/libusb.h
#
# Pure-C library — kept in DEP_DEBUG_EXCLUDES so the MSVC dual-build does not
# rebuild it for Debug.
#
# Recursive submodules are required (libusb-cmake bundles libusb itself as a
# submodule under the project root).
#

add_cmake_project(libusb
    GIT_REPOSITORY          https://github.com/libusb/libusb-cmake
    GIT_TAG                 main
    GIT_SHALLOW             ON
    # GIT_SUBMODULES_RECURSE was added in CMake 3.17; do the recursive submodule
    # init via a PATCH_COMMAND so this works on Ubuntu focal's CMake 3.16 too.
    PATCH_COMMAND           ${GIT_EXECUTABLE} submodule update --init --recursive
    CMAKE_ARGS
        -DLIBUSB_BUILD_EXAMPLES=OFF
        -DLIBUSB_BUILD_TESTING=OFF
        -DLIBUSB_INSTALL_TARGETS=ON
        -DLIBUSB_ENABLE_UDEV=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

# Emit a Config.cmake with the consumer-facing target name libusb::libusb so
# `sdrpp_link_dep(<x> libusb PC_NAME libusb-1.0)` resolves uniformly even
# though libusb-cmake's own export uses different naming conventions.
sdrpp_emit_imported_config(libusb
    LIB_NAMES   libusb-1.0 usb-1.0
    DLL_NAMES   libusb-1.0.dll
    HEADER      libusb.h
    INCLUDE_SUBDIR libusb-1.0
)
