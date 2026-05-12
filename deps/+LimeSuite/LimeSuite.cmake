#
# LimeSuite — LimeSDR driver and utilities. Build is heavy; disable the GUI
# and tests to keep it lean.
#
add_cmake_project(LimeSuite
    GIT_REPOSITORY https://github.com/myriadrf/LimeSuite
    GIT_TAG        v23.11.0
    GIT_SHALLOW    ON
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_limesuite.cmake
    INSTALL_COMMAND
        ${CMAKE_COMMAND} --build . --target install --config ${CMAKE_BUILD_TYPE}
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${SDRPP_DEPS_INSTALL_PREFIX}
            -P ${CMAKE_CURRENT_LIST_DIR}/fix_limesuite_config.cmake
    CMAKE_ARGS
        -DENABLE_GUI=OFF
        -DENABLE_OCTAVE=OFF
        -DENABLE_NOVENA=OFF
        -DENABLE_EXAMPLES=OFF
        -DENABLE_UTILITIES=OFF
        -DENABLE_QUICKTEST=OFF
        -DENABLE_LIME_UTIL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

set(DEP_LimeSuite_DEPENDS libusb)

sdrpp_validate_dep(LimeSuite
    TARGET         LimeSuite
    LIB_NAMES      LimeSuite
    DLL_NAMES      LimeSuite.dll
    HEADER         LimeSuite.h
    INCLUDE_SUBDIR lime
    REQUIRES_CONFIG)
