cmake_minimum_required(VERSION 3.16...3.24)
project(plutosdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(plutosdr_source libiio)
sdrpp_link_dep(plutosdr_source libad9361)
