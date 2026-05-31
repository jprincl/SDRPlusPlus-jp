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
    # master @ 2026-05-31; bump when intentional.
    GIT_TAG        b1275e9c240b64cd7a8fd9bac70d77a8dd957616
    GIT_SHALLOW    OFF
    SOURCE_SUBDIR  host
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        ${_libhackrf_android_args}
)

set(DEP_libhackrf_DEPENDS libusb fftw3)
if (WIN32)
    list(APPEND DEP_libhackrf_DEPENDS pthreads)
endif ()

sdrpp_emit_imported_config(libhackrf
    LIB_NAMES   hackrf
    DLL_NAMES   hackrf.dll
    HEADER      hackrf.h
    INCLUDE_SUBDIR libhackrf
)
