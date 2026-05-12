#
# PortAudio — used by OPT_BUILD_NEW_PORTAUDIO_SINK / OPT_BUILD_PORTAUDIO_SINK.
# Pure C library with CMake support. Android uses Oboe / AAudio sinks instead.
#
sdrpp_dep_get_linkage_option_bools(portaudio _portaudio_build_shared _portaudio_build_static)
if (_portaudio_build_shared)
    set(_portaudio_imported_target portaudio)
else ()
    set(_portaudio_imported_target portaudio_static)
endif ()

add_cmake_project(portaudio
    URL https://github.com/PortAudio/portaudio/archive/refs/tags/v19.7.0.tar.gz
    # URL_HASH SHA256=<TODO: pin after first verified build>
    CMAKE_ARGS
        -DPA_BUILD_SHARED=${_portaudio_build_shared}
        -DPA_BUILD_STATIC=${_portaudio_build_static}
        -DPA_BUILD_TESTS=OFF
        -DPA_BUILD_EXAMPLES=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

# Windows x64 install ships portaudio_x64.{dll,lib}; other platforms ship
# libportaudio.* — accept both forms.
sdrpp_validate_dep(portaudio
    TARGET    ${_portaudio_imported_target}
    LIB_NAMES portaudio_x64 portaudio portaudio_static_x64 portaudio_static
    DLL_NAMES portaudio_x64.dll portaudio.dll
    HEADER    portaudio.h
    REQUIRES_CONFIG)
