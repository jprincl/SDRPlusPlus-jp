#
# libhackrf — built from the AlexandreRouma fork (matches the Android kit
# and the existing Windows CI). The CMake project lives in host/.
#
# DISABLE_USB_ENUMERATION=ON is needed on Android to bypass libusb device
# enumeration that requires Linux udev. On desktop we leave it OFF.
#
set(_libhackrf_android_args "")
if (ANDROID)
    list(APPEND _libhackrf_android_args -DDISABLE_USB_ENUMERATION=ON)
endif ()

add_cmake_project(libhackrf
    GIT_REPOSITORY https://github.com/AlexandreRouma/hackrf
    GIT_TAG        master
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  host
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        ${_libhackrf_android_args}
)

set(DEP_libhackrf_DEPENDS libusb fftw3)

sdrpp_emit_imported_config(libhackrf
    LIB_NAMES   hackrf
    DLL_NAMES   hackrf.dll
    HEADER      hackrf.h
    INCLUDE_SUBDIR libhackrf
)
