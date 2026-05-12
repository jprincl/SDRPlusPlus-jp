cmake_minimum_required(VERSION 3.13)
project(soapy_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# SoapySDR is not currently provided by the deps build. Linux/macOS ship it via
# their package managers; both pkg-config and Config.cmake are available.
sdrpp_link_dep(soapy_source SoapySDR TARGETS SoapySDR::SoapySDR SoapySDR)
