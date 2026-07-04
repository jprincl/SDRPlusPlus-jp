cmake_minimum_required(VERSION 3.16...3.24)
project(perseus_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(perseus_source libperseus-sdr)
