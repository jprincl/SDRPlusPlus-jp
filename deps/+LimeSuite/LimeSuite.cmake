#
# LimeSuite — LimeSDR driver and utilities. Build is heavy; disable the GUI
# and tests to keep it lean.
#
set(_limesuite_cmake_args
    -DENABLE_GUI=OFF
    -DENABLE_OCTAVE=OFF
    -DENABLE_NOVENA=OFF
    -DENABLE_EXAMPLES=OFF
    -DENABLE_UTILITIES=OFF
    -DENABLE_QUICKTEST=OFF
    -DENABLE_LIME_UTIL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

# FTDI's FTD3XX SuperSpeed SDK ships x86/x64 binaries only — no ARM64 build of
# FTD3XX.lib exists, and LimeSuite's CMakeLists has no ARM64 branch in its
# FTD3XX_LIB_DIR selection (it falls into the x64 path on any 64-bit target,
# producing a machine-type mismatch at link time on ARM64). Disable the FT601
# backend on ARM64 — LimeSDR-USB support is lost there, but LimeSDR-Mini and
# LimeSDR-PCIE still work via the libusb / Xillybus backends.
set(_limesuite_is_arm64 FALSE)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^([Aa][Rr][Mm]64|[Aa][Aa][Rr][Cc][Hh]64)"
        OR CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64"
        OR "$ENV{VSCMD_ARG_TGT_ARCH}" MATCHES "^[Aa][Rr][Mm]64")
    set(_limesuite_is_arm64 TRUE)
endif ()
if (WIN32 AND _limesuite_is_arm64)
    list(APPEND _limesuite_cmake_args -DENABLE_FTDI=OFF)
endif ()

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
    CMAKE_ARGS ${_limesuite_cmake_args}
)

set(DEP_LimeSuite_DEPENDS libusb)

sdrpp_validate_dep(LimeSuite
    TARGET         LimeSuite
    LIB_NAMES      LimeSuite
    DLL_NAMES      LimeSuite.dll
    HEADER         LimeSuite.h
    INCLUDE_SUBDIR lime
    REQUIRES_CONFIG)
