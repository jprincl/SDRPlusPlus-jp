#
# Volk — vector-optimized kernel library used by sdrpp_core. Pure C public
# API, but its build-time codegen requires Python 3 with the `mako` package.
# Install once on the host with:
#     python -m pip install --user mako
#
# Recursive submodules are required (cpu_features).
#

# Locate the host Python 3 interpreter (cross-compile safe).
# Override with -DHOST_PYTHON_EXECUTABLE=<path> if auto-detection fails.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FindHostPython.cmake)

add_cmake_project(volk
    GIT_REPOSITORY      https://github.com/gnuradio/volk
    GIT_TAG             main
    GIT_SUBMODULES_RECURSE ON
    CMAKE_ARGS
        -DENABLE_TESTING=OFF
        -DENABLE_MODTOOL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Pass the host Python to both VolkPython.cmake (PYTHON_EXECUTABLE) and
        # directly to FindPython3 as a cache variable so the Android sysroot
        # search and any stale INTERNAL cache entry are both bypassed.
        -DPYTHON_EXECUTABLE:FILEPATH=${HOST_PYTHON_EXECUTABLE}
        -DPython3_EXECUTABLE:FILEPATH=${HOST_PYTHON_EXECUTABLE}
)

sdrpp_validate_dep(volk
    TARGET         Volk::volk
    LIB_NAMES      volk
    DLL_NAMES      volk.dll
    HEADER         volk.h
    INCLUDE_SUBDIR volk
    REQUIRES_CONFIG)
