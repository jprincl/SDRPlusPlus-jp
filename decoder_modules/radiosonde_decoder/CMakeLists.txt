cmake_minimum_required(VERSION 3.13)
project(radiosonde_decoder)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(radiosonde_decoder sondedump)
