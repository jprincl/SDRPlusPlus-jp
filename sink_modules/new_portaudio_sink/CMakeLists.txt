cmake_minimum_required(VERSION 3.16...3.24)
project(new_portaudio_sink)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

sdrpp_link_dep(new_portaudio_sink portaudio PC_NAME portaudio-2.0
	TARGETS portaudio portaudio_static
	SHARED_TARGETS portaudio
	STATIC_TARGETS portaudio_static)
