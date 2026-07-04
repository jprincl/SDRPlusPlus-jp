cmake_minimum_required(VERSION 3.16...3.24)
project(falcon9_decoder)

file(GLOB_RECURSE SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(falcon9_decoder PRIVATE "src/")