#
# FFTW3 — single-precision float build (matches Android kit / SDR++ usage).
# Pure-CMake upstream, simplest possible recipe.
#
# Hash sourced from https://www.fftw.org/release-notes.html — verify on first
# successful build and pin if it diverges.
#
add_cmake_project(fftw3
    URL https://www.fftw.org/fftw-3.3.10.tar.gz
    URL_HASH SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
    CMAKE_ARGS
        -DENABLE_FLOAT=ON
        -DBUILD_TESTS=OFF
        -DDISABLE_FORTRAN=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
