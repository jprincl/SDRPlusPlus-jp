cmake_minimum_required(VERSION 3.16...3.24)
project(sdrpp_server_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(sdrpp_server_source PRIVATE "src/")