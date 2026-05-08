#
# libairspyhf — Airspy HF+ USB driver.
#
add_cmake_project(libairspyhf
    GIT_REPOSITORY https://github.com/airspy/airspyhf
    GIT_TAG        master
    GIT_SHALLOW    ON
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libairspyhf_DEPENDS libusb)

sdrpp_emit_imported_config(libairspyhf
    LIB_NAMES   airspyhf
    DLL_NAMES   airspyhf.dll
    HEADER      airspyhf.h
    INCLUDE_SUBDIR libairspyhf
)
