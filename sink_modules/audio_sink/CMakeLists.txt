cmake_minimum_required(VERSION 3.13)
project(audio_sink)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(audio_sink RtAudio TARGETS RtAudio::rtaudio rtaudio)
