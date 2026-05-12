cmake_minimum_required(VERSION 3.13)
project(rtl_sdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(rtl_sdr_source librtlsdr
    PACKAGE_NAME   rtlsdr
    TARGETS        rtlsdr::rtlsdr rtlsdr
    STATIC_TARGETS rtlsdr::rtlsdr_static
    SHARED_TARGETS rtlsdr::rtlsdr)
# librtlsdr's Config.cmake / .pc declares libusb-1.0 transitively, so a
# separate libusb link is normally not required. Keep an explicit fallback
# for environments where the transitive declaration is absent.
sdrpp_link_dep(rtl_sdr_source libusb PC_NAME libusb-1.0)
