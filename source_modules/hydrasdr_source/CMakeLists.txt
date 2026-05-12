cmake_minimum_required(VERSION 3.13)
project(hydrasdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(hydrasdr_source libhydrasdr)