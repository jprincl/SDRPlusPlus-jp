#
# libairspy — Airspy R2/Mini USB driver (airspyone_host).
#
add_cmake_project(libairspy
    GIT_REPOSITORY https://github.com/airspy/airspyone_host
    GIT_TAG        master
    GIT_SHALLOW    ON
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_libairspy_DEPENDS libusb)

# Defensive Config.cmake — airspyone_host upstream may or may not install one,
# depending on the version. Ours wins on our prefix and is self-relative.
sdrpp_emit_imported_config(libairspy
    LIB_NAMES   airspy
    DLL_NAMES   airspy.dll
    HEADER      airspy.h
    INCLUDE_SUBDIR libairspy
)
