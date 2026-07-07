cmake_minimum_required(VERSION 3.13)
project(libqmx)

option(QMX_CAT_DEBUG_TIMING "Enable QMX CAT timing diagnostics" OFF)

file(GLOB_RECURSE LIBQMX_SRC "src/*.cpp")

# Objective-C++ sources (e.g. the macOS microphone-permission helper) only
# compile on Apple platforms; keep them out of the build elsewhere.
if (APPLE)
    file(GLOB_RECURSE LIBQMX_OBJCXX_SRC "src/*.mm")
    list(APPEND LIBQMX_SRC ${LIBQMX_OBJCXX_SRC})
endif ()

add_library(qmx STATIC ${LIBQMX_SRC})
set_target_properties(qmx PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(qmx PUBLIC cxx_std_17)
target_include_directories(qmx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(qmx PUBLIC QMX_CAT_DEBUG_TIMING=$<BOOL:${QMX_CAT_DEBUG_TIMING}>)

if (MSVC)
    target_link_libraries(qmx PRIVATE ole32 avrt winmm)
elseif (ANDROID)
    sdrpp_link_dep(qmx libusb PC_NAME libusb-1.0)
elseif (APPLE)
    target_link_libraries(qmx PRIVATE
        "-framework CoreAudio"
        "-framework AudioUnit"
        "-framework CoreFoundation"
        "-framework AVFoundation" # AVCaptureDevice microphone authorization
    )
elseif (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)

    target_include_directories(qmx PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_directories(qmx PRIVATE ${ALSA_LIBRARY_DIRS})
    target_link_libraries(qmx PRIVATE ${ALSA_LIBRARIES})
endif ()
