cmake_minimum_required(VERSION 3.16...3.24)
project(sddc_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

add_subdirectory("./libsddc")
target_link_libraries(sddc_source PRIVATE sddc)