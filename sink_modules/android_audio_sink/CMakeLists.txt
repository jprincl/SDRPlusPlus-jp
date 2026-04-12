cmake_minimum_required(VERSION 3.13)
project(audio_sink)

file(GLOB SRC "src/*.cpp")

find_package(oboe REQUIRED CONFIG)
option(ANDROID_AUDIO_SINK_USE_CALLBACK "Use Oboe callback mode for the Android audio sink" OFF)

include(${SDRPP_MODULE_CMAKE})

if (ANDROID_AUDIO_SINK_USE_CALLBACK)
    target_compile_definitions(audio_sink PRIVATE ANDROID_AUDIO_SINK_USE_CALLBACK=1)
else()
    target_compile_definitions(audio_sink PRIVATE ANDROID_AUDIO_SINK_USE_CALLBACK=0)
endif()

target_link_libraries(audio_sink PRIVATE oboe::oboe)
