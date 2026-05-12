#
# libbladeRF — Nuand bladeRF driver. CMake project lives in host/.
#
add_cmake_project(libbladeRF
    GIT_REPOSITORY https://github.com/Nuand/bladeRF
    GIT_TAG        libbladeRF_v2.5.0
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  host
    PATCH_COMMAND  ${CMAKE_COMMAND}
                       -DSRC=<SOURCE_DIR>
                       -P ${CMAKE_CURRENT_LIST_DIR}/patch_libbladerf.cmake
    CMAKE_ARGS
        -DENABLE_FX3_BUILD=OFF
        -DENABLE_HOST_BUILD=ON
        -DTREAT_WARNINGS_AS_ERRORS=OFF
        -DBUILD_DOCUMENTATION=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Pre-set the cache variables that libbladeRF's bundled
        # FindLibPThreadsWin32.cmake uses. The module expects an old
        # pthreads-win32 v2 root containing COPYING.LIB, so point that root at
        # a build-tree compatibility shim while feeding the real pthreads4w
        # header, library, and DLL from the installed dependency prefix.
        -DLIBPTHREADSWIN32_PATH=${SDRPP_DEPS_COMPAT_DIR}/pthreads-win32
        -DLIBPTHREADSWIN32_FOUND=TRUE
        -DLIBPTHREADSWIN32_HEADER_FILE=${SDRPP_DEPS_INSTALL_PREFIX}/include/pthread.h
        -DPTHREAD_LIBRARY=${SDRPP_DEPS_INSTALL_PREFIX}/lib/pthreadVC3.lib
        -DPTHREAD_DLL=${SDRPP_DEPS_INSTALL_PREFIX}/bin/pthreadVC3.dll
        # Point libbladeRF's FindLibUSB at our install prefix so its DLL-copy
        # custom command can locate libusb-1.0.dll. The suffix is patched to
        # 'bin' (see patch_libbladerf.cmake) to match our destdir layout.
        -DLIBUSB_PATH=${SDRPP_DEPS_INSTALL_PREFIX}
)

set(DEP_libbladeRF_DEPENDS libusb pthreads)

# libbladeRF's host/CMakeLists.txt installs the runtime DLL into lib/ instead
# of bin/ on Windows. Mirror it into bin/ post-install so the imported-config
# DLL search and standard app-bundling collection both find it where they
# expect.
if (WIN32)
    ExternalProject_Add_Step(dep_libbladeRF mirror_dll_to_bin
        DEPENDEES install
        COMMAND   ${CMAKE_COMMAND} -E make_directory ${SDRPP_DEPS_INSTALL_PREFIX}/bin
        COMMAND   ${CMAKE_COMMAND} -E copy_if_different
                      ${SDRPP_DEPS_INSTALL_PREFIX}/lib/bladeRF.dll
                      ${SDRPP_DEPS_INSTALL_PREFIX}/bin/bladeRF.dll
        COMMENT   "Mirroring bladeRF.dll from lib/ to bin/"
        USES_TERMINAL ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS}
    )
endif ()

sdrpp_emit_imported_config(libbladeRF
    LIB_NAMES   bladeRF libbladeRF
    DLL_NAMES   bladeRF.dll
    HEADER      libbladeRF.h
)
