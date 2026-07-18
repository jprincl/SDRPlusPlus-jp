cmake_minimum_required(VERSION 3.16...3.24)
project(noise_reduction_logmmse)

file(GLOB SRC "src/*.cpp" "src/omlsa_mcra/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# M_PI & friends on MSVC, regardless of header inclusion order
target_compile_definitions(noise_reduction_logmmse PRIVATE _USE_MATH_DEFINES)

target_include_directories(noise_reduction_logmmse PRIVATE "src/")
target_include_directories(noise_reduction_logmmse PRIVATE "src/omlsa_mcra")

# Recycle DSP buffers through a size-class pool instead of plain heap
# allocation (see dsp_allocator in src/arrays.h). Must be set here so every
# TU agrees. Enabled on Android, where the hardened scudo allocator makes
# per-frame heap churn expensive; elsewhere plain allocation measures on par
# with pooling and keeps ASan/leak tooling useful.
if (ANDROID)
    target_compile_definitions(noise_reduction_logmmse PRIVATE SDRPP_NR_RECYCLE_ARRAYS=1)
endif ()
