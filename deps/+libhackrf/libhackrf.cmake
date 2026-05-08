#
# libhackrf — built from the AlexandreRouma fork (matches the Android kit
# and the existing Windows CI). The CMake project lives in host/.
#
# DISABLE_USB_ENUMERATION=ON is needed on Android to bypass libusb device
# enumeration that requires Linux udev. On desktop we leave it OFF.
#
add_cmake_project(libhackrf
    GIT_REPOSITORY https://github.com/AlexandreRouma/hackrf
    GIT_TAG        master
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  host
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libhackrf_DEPENDS libusb fftw3)
