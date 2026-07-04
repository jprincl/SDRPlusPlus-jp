cmake_minimum_required(VERSION 3.16...3.24)
project(spectran_http_source)

file(GLOB_RECURSE SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(spectran_http_source PRIVATE "src/")