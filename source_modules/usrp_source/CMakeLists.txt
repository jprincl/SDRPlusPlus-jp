cmake_minimum_required(VERSION 3.16...3.24)
project(usrp_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# UHD is not currently provided by the deps build. Linux distros ship `libuhd-dev`
# with both Config.cmake and pkg-config; helper resolves either.
sdrpp_link_dep(usrp_source UHD PC_NAME uhd TARGETS UHD::UHD uhd)
