cmake_minimum_required(VERSION 3.16...3.24)
project(spots)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(spots PRIVATE "src/")
