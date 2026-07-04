cmake_minimum_required(VERSION 3.16...3.24)
project(dab_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(dab_decoder PRIVATE "src/")

sdrpp_link_dep(dab_decoder codec2)
