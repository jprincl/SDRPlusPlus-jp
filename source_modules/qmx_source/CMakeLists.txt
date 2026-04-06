cmake_minimum_required(VERSION 3.13)
project(qmx_source)

set(SRC
    src/main.cpp
    src/FreqModeSync.cpp
)

include(${SDRPP_MODULE_CMAKE})

target_include_directories(qmx_source PRIVATE "../../decoder_modules/radio/src")

add_subdirectory("libqmx")
target_link_libraries(qmx_source PRIVATE qmx)