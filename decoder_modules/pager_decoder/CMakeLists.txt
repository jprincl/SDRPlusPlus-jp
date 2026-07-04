cmake_minimum_required(VERSION 3.16...3.24)
project(pager_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(pager_decoder PRIVATE "src/")