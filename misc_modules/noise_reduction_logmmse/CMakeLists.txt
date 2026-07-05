cmake_minimum_required(VERSION 3.16...3.24)
project(noise_reduction_logmmse)

file(GLOB SRC "src/*.cpp" "src/omlsa_mcra/*.cpp")
# Original OMLSA file-based test harness, not part of the plugin
list(FILTER SRC EXCLUDE REGEX "omlsa_mcra/main\\.cpp$")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(noise_reduction_logmmse PRIVATE "src/")
target_include_directories(noise_reduction_logmmse PRIVATE "src/omlsa_mcra")
target_include_directories(noise_reduction_logmmse PRIVATE "../../decoder_modules/radio/src")
