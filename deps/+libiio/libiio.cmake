#
# libiio — IIO (Industrial I/O) bindings, dependency of PlutoSDR support.
# Version matches the Android kit pin (v0.24).
#
# libiio v0.24's CMakeLists uses pkg_check_modules(libusb-1.0) with no
# CMake-native fallback. The libusb override variables (LIBUSB_LIBRARIES /
# LIBUSB_INCLUDE_DIRS / LIBUSB_FOUND) are injected globally for every recipe
# via DEP_CMAKE_OPTS in deps/CMakeLists.txt — see the libusb override block
# there.
set(_libiio_usb_backend ON)
if (ANDROID)
    set(_libiio_usb_backend OFF)
endif ()

add_cmake_project(libiio
    URL https://github.com/analogdevicesinc/libiio/archive/refs/tags/v0.24.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libiio.cmake
    CMAKE_ARGS
        -DWITH_TESTS=OFF
        -DWITH_DOC=OFF
        -DWITH_USB_BACKEND=${_libiio_usb_backend}
        -DWITH_NETWORK_BACKEND=ON
        -DHAVE_DNS_SD=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libiio_DEPENDS libxml2 libusb)

set(_libiio_compile_definitions "")
set(_libiio_package_dependencies "")
set(_libiio_link_libraries "")
sdrpp_dep_builds_shared(libiio _libiio_builds_shared)
if (NOT _libiio_builds_shared)
    list(APPEND _libiio_compile_definitions LIBIIO_STATIC)
    list(APPEND _libiio_package_dependencies libusb libxml2)
    list(APPEND _libiio_link_libraries libusb::libusb LibXml2::LibXml2)
    if (WIN32)
        list(APPEND _libiio_link_libraries wsock32 iphlpapi ws2_32)
    endif ()
endif ()

sdrpp_emit_imported_config(libiio
    LIB_NAMES   iio libiio
    DLL_NAMES   iio.dll libiio.dll
    HEADER      iio.h
    COMPILE_DEFINITIONS ${_libiio_compile_definitions}
    PACKAGE_DEPENDENCIES ${_libiio_package_dependencies}
    LINK_LIBRARIES ${_libiio_link_libraries}
)
