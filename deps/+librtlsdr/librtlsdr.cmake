#
# librtlsdr — built from the AlexandreRouma fork (matches the Android kit).
# Note: the Windows CI uses the prebuilt osmocom Windows binary instead;
# this recipe builds from source so we get matching debug symbols.
#
add_cmake_project(librtlsdr
    GIT_REPOSITORY https://github.com/AlexandreRouma/rtl-sdr
    GIT_TAG        master
    GIT_SHALLOW    ON
    CMAKE_ARGS
        -DBUILD_UTILITIES=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_librtlsdr_DEPENDS libusb)
