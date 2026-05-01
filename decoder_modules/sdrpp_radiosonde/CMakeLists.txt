cmake_minimum_required(VERSION 3.13)
project(radiosonde_decoder C CXX)

# Build sondedump's "radiosonde" static library (the actual decoder core).
# Force its options off so it doesn't pull ncurses/portaudio or generate an
# uninstall target that would clash with the parent project's.
set(UNINSTALL_TARGET_SAVED "${UNINSTALL_TARGET}")
set(UNINSTALL_TARGET OFF CACHE INTERNAL "")
set(ENABLE_TUI       OFF CACHE INTERNAL "")
set(ENABLE_AUDIO     OFF CACHE INTERNAL "")
add_subdirectory("src/decode/sondedump" EXCLUDE_FROM_ALL)
set(UNINSTALL_TARGET "${UNINSTALL_TARGET_SAVED}")

# Build the SDR++ plugin DLL via the standard module convention.
file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(radiosonde_decoder PRIVATE "src/")
target_link_libraries(radiosonde_decoder PRIVATE radiosonde)
