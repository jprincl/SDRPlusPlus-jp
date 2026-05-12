cmake_minimum_required(VERSION 3.13)
project(limesdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# LimeSuite ships only LimeSuiteConfig.cmake (no pkg-config). The helper's
# CONFIG step picks it up; pkg-config fallback will fail loudly if absent.
sdrpp_link_dep(limesdr_source LimeSuite TARGETS LimeSuite::LimeSuite LimeSuite)
