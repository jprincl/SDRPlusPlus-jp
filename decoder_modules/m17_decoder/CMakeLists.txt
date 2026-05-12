cmake_minimum_required(VERSION 3.13)
project(m17_decoder)

file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(m17_decoder PRIVATE "src/")

sdrpp_link_dep(m17_decoder codec2)
