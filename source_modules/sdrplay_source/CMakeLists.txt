cmake_minimum_required(VERSION 3.13)
project(sdrplay_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# SDRplay API is closed-source and ships no pkg-config file. Resolution is
# Config.cmake-only (our deps recipe emits one) — the helper's pkg-config
# fallback will fail with a clear error if Config.cmake is missing, signalling
# that the deps build needs to be (re)run.
sdrpp_link_dep(sdrplay_source sdrplay TARGETS sdrplay::sdrplay_api sdrplay_api)
