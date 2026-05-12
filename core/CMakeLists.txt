cmake_minimum_required(VERSION 3.13)
project(sdrpp_core)

if (USE_INTERNAL_LIBCORRECT)
    add_subdirectory("libcorrect/")
endif (USE_INTERNAL_LIBCORRECT)

if (USE_BUNDLE_DEFAULTS)
add_definitions(-DIS_MACOS_BUNDLE)
endif (USE_BUNDLE_DEFAULTS)

# Main code
file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

add_definitions(-DSDRPP_IS_CORE)
add_definitions(-DFLOG_ANDROID_TAG="SDR++ iak")
if (MSVC)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif ()

# Configure backend sources
if (OPT_BACKEND_GLFW)
    file(GLOB_RECURSE BACKEND_SRC "backends/glfw/*.cpp" "backends/glfw/*.c")
endif (OPT_BACKEND_GLFW)
if (OPT_BACKEND_ANDROID)
    file(GLOB_RECURSE BACKEND_SRC "backends/android/*.cpp" "backends/android/*.c")
    set(BACKEND_SRC ${BACKEND_SRC} ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
endif (OPT_BACKEND_ANDROID)

# Add code to dyn lib
add_library(sdrpp_core SHARED ${SRC} ${BACKEND_SRC})
if (MSVC AND SDRPP_RUNTIME_OUTPUT_DIRECTORY)
    set_target_properties(sdrpp_core PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SDRPP_RUNTIME_OUTPUT_DIRECTORY}")
    add_custom_command(TARGET sdrpp_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/$<TARGET_FILE_NAME:sdrpp_core>"
        VERBATIM)
endif ()

# Set compiler options
target_compile_options(sdrpp_core PRIVATE ${SDRPP_COMPILER_FLAGS})

# Set the install prefix
target_compile_definitions(sdrpp_core PUBLIC INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}")

# AppImage build: enable runtime code paths that resolve resources via $APPDIR.
if (OPT_BUILD_APPIMAGE)
    target_compile_definitions(sdrpp_core PUBLIC BUILD_APPIMAGE)
endif ()

# Include core headers
target_include_directories(sdrpp_core PUBLIC "src/")
target_include_directories(sdrpp_core PUBLIC "src/imgui")

# Configure backend includes and libraries
if (OPT_BACKEND_GLFW)
    target_include_directories(sdrpp_core PUBLIC "backends/glfw")
    target_include_directories(sdrpp_core PUBLIC "backends/glfw/imgui")
endif (OPT_BACKEND_GLFW)
if (OPT_BACKEND_ANDROID)
    target_include_directories(sdrpp_core PUBLIC "backends/android")
    target_include_directories(sdrpp_core PUBLIC "backends/android/imgui")
endif (OPT_BACKEND_ANDROID)

# Link to libcorrect
if (USE_INTERNAL_LIBCORRECT)
    target_include_directories(sdrpp_core PUBLIC "libcorrect/include")
    target_link_libraries(sdrpp_core PUBLIC correct_static)
endif (USE_INTERNAL_LIBCORRECT)

# libcurl is a core implementation detail. Modules use core networking
# wrappers (utils/curl_http, etc.) instead of including curl headers or
# linking libcurl directly. Resolved from the deps install prefix (bundled
# profile) or the distro libcurl on Linux.
sdrpp_link_dep(sdrpp_core libcurl PRIVATE
    PACKAGE_NAME CURL
    PC_NAME      libcurl
    TARGETS      CURL::libcurl)

# WebSocket support is mandatory. The bundled recipe always forces
# ENABLE_WEBSOCKETS=ON; distro libcurl may have been packaged without
# it (Ubuntu 20.04 ships 7.68, no WS at all), so probe explicitly when
# the imported target is in module form.
if (TARGET CURL::libcurl)
    include(CheckSymbolExists)
    get_target_property(_sdrpp_curl_inc CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
    if (_sdrpp_curl_inc)
        set(CMAKE_REQUIRED_INCLUDES "${_sdrpp_curl_inc}")
    endif ()
    set(CMAKE_REQUIRED_LIBRARIES CURL::libcurl)
    check_symbol_exists(curl_ws_send "curl/curl.h" SDRPP_CURL_HAS_WS)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if (NOT SDRPP_CURL_HAS_WS)
        message(FATAL_ERROR
            "libcurl was built without WebSocket support. Install a libcurl "
            "with --enable-websockets, or rebuild deps with "
            "SDRPP_DEP_FORCE_BUNDLED=libcurl.")
    endif ()
endif ()

if (OPT_OVERRIDE_STD_FILESYSTEM)
    target_include_directories(sdrpp_core PUBLIC "std_replacement")
endif (OPT_OVERRIDE_STD_FILESYSTEM)

if (MSVC)
    # OpenGL
    find_package(OpenGL REQUIRED)
    target_link_libraries(sdrpp_core PUBLIC OpenGL::GL)

    if (OPT_BACKEND_GLFW)
        sdrpp_link_dep(sdrpp_core glfw3 PUBLIC PC_NAME glfw3 TARGETS glfw3::glfw glfw)
    endif ()

    sdrpp_link_dep(sdrpp_core FFTW3f PUBLIC PC_NAME fftw3f TARGETS FFTW3::fftw3f)

    # WinSock2
    target_link_libraries(sdrpp_core PUBLIC wsock32 ws2_32 iphlpapi)

    sdrpp_link_dep(sdrpp_core zstd PUBLIC PC_NAME libzstd
        TARGETS zstd::libzstd_shared zstd::libzstd zstd zstd::libzstd_static
        SHARED_TARGETS zstd::libzstd_shared zstd::libzstd zstd
        STATIC_TARGETS zstd::libzstd_static)

    sdrpp_link_dep(sdrpp_core Volk PUBLIC PC_NAME volk TARGETS Volk::volk volk::volk volk)
elseif (ANDROID)
    target_include_directories(sdrpp_core PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue
    )

    if (OPT_BACKEND_GLFW)
        sdrpp_link_dep(sdrpp_core glfw3 PUBLIC PC_NAME glfw3 TARGETS glfw3::glfw glfw)
    endif ()

    sdrpp_link_dep(sdrpp_core FFTW3f PUBLIC PC_NAME fftw3f TARGETS FFTW3::fftw3f)
    sdrpp_link_dep(sdrpp_core zstd PUBLIC PC_NAME libzstd
        TARGETS zstd::libzstd_shared zstd::libzstd zstd zstd::libzstd_static
        SHARED_TARGETS zstd::libzstd_shared zstd::libzstd zstd
        STATIC_TARGETS zstd::libzstd_static)
    sdrpp_link_dep(sdrpp_core Volk PUBLIC PC_NAME volk TARGETS Volk::volk volk::volk volk)
    target_link_libraries(sdrpp_core PUBLIC
        android
        EGL
        GLESv3
        log
    )
else()
    find_package(OpenGL REQUIRED)

    if (OPT_BACKEND_GLFW)
        sdrpp_link_dep(sdrpp_core glfw3 PUBLIC PC_NAME glfw3 TARGETS glfw3::glfw glfw)
    endif ()
    sdrpp_link_dep(sdrpp_core FFTW3f PUBLIC PC_NAME fftw3f TARGETS FFTW3::fftw3f)
    sdrpp_link_dep(sdrpp_core zstd PUBLIC PC_NAME libzstd
        TARGETS zstd::libzstd_shared zstd::libzstd zstd zstd::libzstd_static
        SHARED_TARGETS zstd::libzstd_shared zstd::libzstd zstd
        STATIC_TARGETS zstd::libzstd_static)
    sdrpp_link_dep(sdrpp_core Volk PUBLIC PC_NAME volk TARGETS Volk::volk volk::volk volk)
    target_link_libraries(sdrpp_core PUBLIC
        ${OPENGL_LIBRARIES}
    )

    if (NOT USE_INTERNAL_LIBCORRECT)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(CORRECT REQUIRED libcorrect)
        target_include_directories(sdrpp_core PUBLIC ${CORRECT_INCLUDE_DIRS})
        target_link_directories(sdrpp_core PUBLIC ${CORRECT_LIBRARY_DIRS})
        target_link_libraries(sdrpp_core PUBLIC ${CORRECT_LIBRARIES})
    endif (NOT USE_INTERNAL_LIBCORRECT)

    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_link_libraries(sdrpp_core PUBLIC stdc++fs)
    endif ()

endif ()

set(CORE_FILES ${RUNTIME_OUTPUT_DIRECTORY} PARENT_SCOPE)

# cmake .. "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"

# Install directives
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux|FreeBSD")
    set_target_properties(sdrpp_core PROPERTIES OUTPUT_NAME "sdrpp_iak_core")
endif ()
install(TARGETS sdrpp_core DESTINATION lib)
