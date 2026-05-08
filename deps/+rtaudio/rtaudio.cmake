#
# RtAudio — used by OPT_BUILD_AUDIO_SINK / OPT_BUILD_AUDIO_SOURCE and the
# non-Android path of OPT_BUILD_QMX_SOURCE. Android uses Oboe.
#
# Pinned to the same commit the existing Windows CI uses (build_all.yml line
# 164/297) — known to work with SDR++. Bump only after verifying.
#
add_cmake_project(rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio
    GIT_TAG        2f2fca4502d506abc50f6d4473b2836d24cfb1e3
    GIT_SHALLOW    OFF
    CMAKE_ARGS
        -DRTAUDIO_BUILD_TESTING=OFF
        -DRTAUDIO_BUILD_STATIC_LIBS=OFF
        -DRTAUDIO_BUILD_SHARED_LIBS=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)
