#
# Volk — vector-optimized kernel library used by sdrpp_core. Pure C public
# API, but its build-time codegen requires Python 3 with the `mako` package.
# Install once on the host with:
#     python -m pip install --user mako
#
# Recursive submodules are required (cpu_features).
#

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_cmake_project(volk
    GIT_REPOSITORY      https://github.com/gnuradio/volk
    GIT_TAG             main
    GIT_SUBMODULES_RECURSE ON
    CMAKE_ARGS
        -DENABLE_TESTING=OFF
        -DENABLE_MODTOOL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DPYTHON_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}
        -DPython3_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}
)

sdrpp_validate_dep(volk
    PACKAGE_NAME   Volk
    TARGET         Volk::volk
    LIB_NAMES      volk
    DLL_NAMES      volk.dll
    HEADER         volk.h
    INCLUDE_SUBDIR volk
    REQUIRES_CONFIG)
