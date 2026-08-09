cmake_minimum_required(VERSION 3.13)
project(web_map)

# Module sources
file(GLOB SRC "src/*.cpp")

if (MSVC)
    set(CMAKE_CXX_FLAGS "/O2 /Ob2 /std:c++17 /EHsc")
else ()
    set(CMAKE_CXX_FLAGS "-O3 -std=c++17 -fpermissive -Wno-unused-command-line-argument")
endif ()

# cpp-httplib is a vendored single header (MIT), no external deps beyond
# the platform socket API. On POSIX, sockets need pthread for the internal
# thread pool; on Windows it links ws2_32 automatically via #pragma comment.
find_package(Threads REQUIRED)

include(${SDRPP_MODULE_CMAKE})

target_include_directories(web_map PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(web_map PRIVATE Threads::Threads)
