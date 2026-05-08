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
        # FindLibPThreadsWin32.cmake uses. The module calls find_library() with
        # the name pthreadVC2 but we ship pthreads4w (v3). Pre-seeding the two
        # internal cache variables (PTHREAD_LIBRARY / PTHREAD_DLL) that
        # find_library/find_file populate causes CMake to skip its search and
        # use our values directly, so LIBPTHREADSWIN32_LIBRARIES ends up
        # pointing at pthreadVC3.lib and the shared library links correctly.
        -DLIBPTHREADSWIN32_PATH=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
        -DLIBPTHREADSWIN32_FOUND=TRUE
        -DPTHREAD_LIBRARY=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/lib/pthreadVC3.lib
        -DPTHREAD_DLL=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/bin/pthreadVC3.dll
        # Point libbladeRF's FindLibUSB at our install prefix so its DLL-copy
        # custom command can locate libusb-1.0.dll. The suffix is patched to
        # 'bin' (see patch_libbladerf.cmake) to match our destdir layout.
        -DLIBUSB_PATH=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}
)

set(DEP_libbladeRF_DEPENDS libusb pthreads)
