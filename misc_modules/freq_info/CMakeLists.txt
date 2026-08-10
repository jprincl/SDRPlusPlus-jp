cmake_minimum_required(VERSION 3.16...3.24)
project(freq_info)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(freq_info PRIVATE "src/")
