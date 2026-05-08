#
# PortAudio — used by OPT_BUILD_NEW_PORTAUDIO_SINK / OPT_BUILD_PORTAUDIO_SINK.
# Pure C library with CMake support. Android uses Oboe / AAudio sinks instead.
#
if (ANDROID)
    return()
endif ()

add_cmake_project(portaudio
    URL https://github.com/PortAudio/portaudio/archive/refs/tags/v19.7.0.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    CMAKE_ARGS
        -DPA_BUILD_SHARED=ON
        -DPA_BUILD_STATIC=OFF
        -DPA_BUILD_TESTS=OFF
        -DPA_BUILD_EXAMPLES=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
