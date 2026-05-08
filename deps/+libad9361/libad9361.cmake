#
# libad9361-iio — completes PlutoSDR support. Upstream's `install` target
# is broken on most platforms (the Android kit notes this), so we drive
# the build manually and copy the artifacts ourselves.
#

include(ExternalProject)

set(_prefix ${SDRPP_DEPS_INSTALL_PREFIX})
set(_src    ${CMAKE_CURRENT_BINARY_DIR}/sources/libad9361)
set(_bin    ${CMAKE_CURRENT_BINARY_DIR}/builds/libad9361)

ExternalProject_Add(dep_libad9361
    URL                 https://github.com/analogdevicesinc/libad9361-iio/archive/refs/tags/v0.2.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    DOWNLOAD_DIR        ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/libad9361
    SOURCE_DIR          ${_src}
    BINARY_DIR          ${_bin}
    CMAKE_ARGS
        -DCMAKE_PREFIX_PATH:STRING=${_prefix}
        -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}
        -DCMAKE_TOOLCHAIN_FILE:STRING=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # libad9361 v0.2's cmake_minimum_required is < 3.5; CMake 4.x drops
        # support for that without this opt-in.
        -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
    BUILD_COMMAND       ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE}
    INSTALL_COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_prefix}/include
        COMMAND
            ${CMAKE_COMMAND} -E copy ${_src}/ad9361.h ${_prefix}/include/ad9361.h
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_prefix}/bin
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_prefix}/lib
        # Copy whatever the build produced — handled at build time via a glob.
        COMMAND
            ${CMAKE_COMMAND} -DSRC=${_bin} -DDST_BIN=${_prefix}/bin -DDST_LIB=${_prefix}/lib
                             -P ${CMAKE_CURRENT_LIST_DIR}/install_libad9361.cmake
)

set(DEP_libad9361_DEPENDS libiio)

sdrpp_emit_imported_config(libad9361
    LIB_NAMES   ad9361 libad9361
    DLL_NAMES   ad9361.dll libad9361.dll
    HEADER      ad9361.h
)
