cmake_minimum_required(VERSION 3.13)
project(audio_sink)

file(GLOB SRC "src/*.cpp")

find_package(oboe REQUIRED CONFIG)

include(${SDRPP_MODULE_CMAKE})

target_link_libraries(audio_sink PRIVATE oboe::oboe)
