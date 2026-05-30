cmake_minimum_required(VERSION 3.13)
project(audio_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(audio_source rtaudio PACKAGE_NAME RtAudio TARGETS RtAudio::rtaudio rtaudio)