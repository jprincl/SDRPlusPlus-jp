#
# pthreads-win32 — POSIX threads compatibility layer for Windows. Required
# transitively by libhydrasdr / rfone_host (they use pthread_* APIs).
#
# Built from GerHobbelt's CMake-enabled mirror of the original pthreads4w
# project. Linux/macOS already have native pthreads via the C library, so
# this recipe is a no-op there.
#

if (NOT WIN32)
    # Define a stub target so DEP_*_DEPENDS lookups from libhydrasdr / rfone_host
    # resolve harmlessly on non-Windows hosts.
    add_custom_target(dep_pthreads)
    set_target_properties(dep_pthreads PROPERTIES EXCLUDE_FROM_ALL TRUE)
    return()
endif ()

add_cmake_project(pthreads
    GIT_REPOSITORY https://github.com/GerHobbelt/pthread-win32
    GIT_TAG        master
    GIT_SHALLOW    ON
    CMAKE_ARGS
        -DBUILD_TESTING=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

# libbladeRF's FindLibPThreadsWin32.cmake gates on the presence of a
# COPYING.LIB file (a relic of the pthreads-win32 v2 source tree). pthreads4w
# (the v3 fork we use) doesn't ship it. Drop a stub so the find succeeds.
file(WRITE ${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/COPYING.LIB
"This stub file exists to satisfy libbladeRF's FindLibPThreadsWin32.cmake,
which uses the presence of COPYING.LIB as a sentinel for pthreads-win32 v2's
source tree. We use pthreads4w (the GerHobbelt v3 fork), which is licensed
under the Apache 2.0 License — see lib/cmake/pthreads4w/ for the actual
project artifacts.
")
