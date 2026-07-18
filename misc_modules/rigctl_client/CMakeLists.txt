cmake_minimum_required(VERSION 3.16...3.24)
project(rigctl_client)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(rigctl_client PRIVATE "src/")