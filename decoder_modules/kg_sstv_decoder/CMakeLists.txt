cmake_minimum_required(VERSION 3.16...3.24)
project(kg_sstv_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(kg_sstv_decoder PRIVATE "src/")