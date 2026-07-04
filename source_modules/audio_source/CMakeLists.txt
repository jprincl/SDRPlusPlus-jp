cmake_minimum_required(VERSION 3.16...3.24)
project(audio_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(audio_source RtAudio TARGETS RtAudio::rtaudio rtaudio)