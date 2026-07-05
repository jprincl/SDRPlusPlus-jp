cmake_minimum_required(VERSION 3.16...3.24)
project(noise_reduction_logmmse)

file(GLOB SRC "src/*.cpp" "src/omlsa_mcra/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(noise_reduction_logmmse PRIVATE "src/")
target_include_directories(noise_reduction_logmmse PRIVATE "src/omlsa_mcra")
target_include_directories(noise_reduction_logmmse PRIVATE "../../decoder_modules/radio/src")

if (MSVC)
    # logmmse.h uses M_PI; make it available regardless of include order.
    target_compile_definitions(noise_reduction_logmmse PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif ()
