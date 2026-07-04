cmake_minimum_required(VERSION 3.16...3.24)
project(websdr_view)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(websdr_view PRIVATE "src/")