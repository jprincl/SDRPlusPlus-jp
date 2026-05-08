#
# GLFW — windowing/input library used by the SDR++ GLFW backend on
# Windows/Linux/macOS. Android uses a native EGL+ANativeWindow backend and
# does not need this dependency.
#
add_cmake_project(glfw3
    URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
    URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
    CMAKE_ARGS
        -DGLFW_BUILD_DOCS=OFF
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
        -DGLFW_INSTALL=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
