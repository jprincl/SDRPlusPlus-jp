cmake_minimum_required(VERSION 3.13)
project(qmx_source)

set(SRC
    src/main.cpp
    src/FreqModeSync.cpp
)

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif ()

target_include_directories(qmx_source PRIVATE "../../decoder_modules/radio/src")

add_subdirectory("libqmx")
target_link_libraries(qmx_source PRIVATE qmx)