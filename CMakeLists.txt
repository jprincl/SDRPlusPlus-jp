cmake_minimum_required(VERSION 3.13)
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif ()
project(sdrpp)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime library" FORCE)
endif ()

# Backends
option(OPT_BACKEND_GLFW "Use the GLFW backend" ON)
option(OPT_BACKEND_ANDROID "Use the Android backend" OFF)

# Paths
if(DEFINED ENV{SDR_KIT_ROOT} AND NOT DEFINED SDR_KIT_ROOT)
    set(SDR_KIT_ROOT_DEFAULT "$ENV{SDR_KIT_ROOT}")
else()
    set(SDR_KIT_ROOT_DEFAULT "/sdr-kit")
endif()
set(SDR_KIT_ROOT "${SDR_KIT_ROOT_DEFAULT}" CACHE PATH "Root path for the SDR kit libraries")

# Dependency build management
# When OPT_BUILD_DEPS=ON, deps/ is configured and built as a sub-project before
# the main configure continues, and its install prefix is added to CMAKE_PREFIX_PATH
# (and surfaced as SDR_KIT_ROOT). Default OFF — normal flow is to build deps once
# (cmake --preset vs2026-x64 -S deps -B deps/build-vs2026-x64 && cmake --build ...)
# and reuse the install prefix across many app builds.
option(OPT_BUILD_DEPS "Build third-party dependencies before configuring SDR++" OFF)
option(SDRPP_DEPS_OUTPUT_QUIET "Suppress output of the dependency build" OFF)
set(SDRPP_DEPS_PRESET "default" CACHE STRING "Preset name to use for the dependency build")
set(SDRPP_DEPS_BUILD_DIR "" CACHE PATH "Override the binary directory of the dependency build")
set(SDRPP_DEPS_BUILD_CONFIG "" CACHE STRING "Build configuration to use when bootstrapping deps with a multi-config generator")
set(SDRPP_DEP_ROOT_PACKAGES "" CACHE STRING "Override the top-level dependency recipes propagated to deps when OPT_BUILD_DEPS=ON")

# Compatibility Options
option(OPT_OVERRIDE_STD_FILESYSTEM "Use a local version of std::filesystem on systems that don't have it yet" OFF)
option(OPT_BUILD_APPIMAGE "Build AppImage-specific runtime code paths (Linux only - overrides resource paths via $APPDIR at launch)" OFF)

# Sources   
option(OPT_BUILD_AIRSPY_SOURCE "Build Airspy Source Module (Dependencies: libairspy)" ON)
option(OPT_BUILD_AIRSPYHF_SOURCE "Build Airspy HF+ Source Module (Dependencies: libairspyhf)" ON)
option(OPT_BUILD_AUDIO_SOURCE "Build Audio Source Module (Dependencies: rtaudio)" ON)
option(OPT_BUILD_BADGESDR_SOURCE "Build BadgeSDR Source Module (Dependencies: libusb)" OFF)
option(OPT_BUILD_BLADERF_SOURCE "Build BladeRF Source Module (Dependencies: libbladeRF)" OFF)
option(OPT_BUILD_FILE_SOURCE "Wav file source" ON)
option(OPT_BUILD_FOBOSSDR_SOURCE "Build FobosSDR Source Module (Dependencies: libfobos)" OFF)
option(OPT_BUILD_HACKRF_SOURCE "Build HackRF Source Module (Dependencies: libhackrf)" ON)
option(OPT_BUILD_HAROGIC_SOURCE "Build Harogic Source Module (Dependencies: htra_api)" OFF)
option(OPT_BUILD_HERMES_SOURCE "Build Hermes Source Module (no dependencies required)" ON)
option(OPT_BUILD_HYDRASDR_RFONE_SOURCE "Build HydraSDR RFOne Source Module (Dependencies: libhydrasdr)" ON)
option(OPT_BUILD_KCSDR_SOURCE "Build KCSDR Source Module (Dependencies: libkcsdr)" OFF)
option(OPT_BUILD_KIWISDR_SOURCE "Build KiwiSDR WebSDR Source Module (no dependencies required)" ON)
option(OPT_BUILD_LIMESDR_SOURCE "Build LimeSDR Source Module (Dependencies: liblimesuite)" OFF)
option(OPT_BUILD_NETWORK_SOURCE "Build Network Source Module (no dependencies required)" ON)
option(OPT_BUILD_PERSEUS_SOURCE "Build Perseus Source Module (Dependencies: libperseus-sdr)" OFF)
option(OPT_BUILD_PLUTOSDR_SOURCE "Build PlutoSDR Source Module (Dependencies: libiio, libad9361)" ON)
option(OPT_BUILD_QMX_SOURCE "Build direct QMX USB Source Module (Dependencies: rtaudio or aaudio)" ON)
option(OPT_BUILD_QMXSERVER_SOURCE "Build QMX server Source Module (Dependencies: enet)" ON)
option(OPT_BUILD_RFNM_SOURCE "Build RFNM Source Module (Dependencies: librfnm)" OFF)
option(OPT_BUILD_RFSPACE_SOURCE "Build RFspace Source Module (no dependencies required)" ON)
option(OPT_BUILD_RTL_SDR_SOURCE "Build RTL-SDR Source Module (Dependencies: librtlsdr)" ON)
option(OPT_BUILD_RTL_TCP_SOURCE "Build RTL-TCP Source Module (no dependencies required)" ON)
option(OPT_BUILD_SDRPP_SERVER_SOURCE "Build SDR++ Server Source Module (no dependencies required)" ON)
option(OPT_BUILD_SDRPLAY_SOURCE "Build SDRplay Source Module (Dependencies: libsdrplay)" OFF)
option(OPT_BUILD_SOAPY_SOURCE "Build SoapySDR Source Module (Dependencies: soapysdr)" OFF)
option(OPT_BUILD_SPECTRAN_SOURCE "Build Spectran Source Module (Dependencies: Aaronia RTSA Suite)" OFF)
option(OPT_BUILD_SPECTRAN_HTTP_SOURCE "Build Spectran HTTP Source Module (no dependencies required)" ON)
option(OPT_BUILD_SPYSERVER_SOURCE "Build SpyServer Source Module (no dependencies required)" ON)
option(OPT_BUILD_USRP_SOURCE "Build USRP Source Module (libuhd)" OFF)

# Sinks
option(OPT_BUILD_ANDROID_AUDIO_SINK "Build Android Audio Sink Module (Dependencies: AAudio, only for android)" OFF)
option(OPT_BUILD_AUDIO_SINK "Build Audio Sink Module (Dependencies: rtaudio)" ON)
option(OPT_BUILD_NETWORK_SINK "Build Audio Sink Module (no dependencies required)" ON)
option(OPT_BUILD_NEW_PORTAUDIO_SINK "Build the new PortAudio Sink Module (Dependencies: portaudio)" OFF)
option(OPT_BUILD_PORTAUDIO_SINK "Build PortAudio Sink Module (Dependencies: portaudio)" OFF)

# Decoders
option(OPT_BUILD_ATV_DECODER "Build ATV decoder (no dependencies required)" ON)
option(OPT_BUILD_DAB_DECODER "Build the DAB/DAB+ decoder (no dependencies required)" OFF)
option(OPT_BUILD_FALCON9_DECODER "Build the falcon9 live decoder (Dependencies: ffplay)" OFF)
option(OPT_BUILD_KG_SSTV_DECODER "Build the KG SSTV (KG-STV) decoder module (no dependencies required)" OFF)
option(OPT_BUILD_M17_DECODER "Build the M17 decoder module (Dependencies: codec2)" OFF)
option(OPT_BUILD_METEOR_DEMODULATOR "Build the meteor demodulator module (no dependencies required)" ON)
option(OPT_BUILD_PAGER_DECODER "Build the pager decoder module (no dependencies required)" ON)
option(OPT_BUILD_RADIO "Main audio modulation decoder (AM, FM, SSB, etc...)" ON)
option(OPT_BUILD_RADIOSONDE_DECODER "Build the radiosonde decoder module (no dependencies required)" ON)
option(OPT_BUILD_RYFI_DECODER "RyFi data link decoder" OFF)
option(OPT_BUILD_VOR_RECEIVER "VOR beacon receiver" OFF)
option(OPT_BUILD_WEATHER_SAT_DECODER "Build the HRPT decoder module (no dependencies required)" OFF)

# Misc
option(OPT_BUILD_DISCORD_PRESENCE "Build the Discord Rich Presence module" ON)
option(OPT_BUILD_FREQUENCY_MANAGER "Build the Frequency Manager module" ON)
option(OPT_BUILD_IQ_EXPORTER "Build the IQ Exporter module" ON)
option(OPT_BUILD_RECORDER "Audio and baseband recorder" ON)
option(OPT_BUILD_RIGCTL_CLIENT "Rigctl client to make SDR++ act as a panadapter" ON)
option(OPT_BUILD_RIGCTL_SERVER "Rigctl backend for controlling SDR++ with software like gpredict" ON)
option(OPT_BUILD_SCANNER "Frequency scanner" ON)
option(OPT_BUILD_SCHEDULER "Build the scheduler" OFF)
option(OPT_BUILD_SPOTS "Build Spots module" ON)
option(OPT_BUILD_WEBSDR_VIEW "Build WebSDR View module (no dependencies required)" ON)

# Other options
option(USE_INTERNAL_LIBCORRECT "Use an internal version of libcorrect" ON)
option(USE_BUNDLE_DEFAULTS "Set the default resource and module directories to the right ones for a MacOS .app" OFF)
option(COPY_MSVC_REDISTRIBUTABLES "Copy over the Visual C++ Redistributable" OFF)

# SDRplay distributes its API as a closed-source binary. Available variants:
#   - Linux x86_64 / Windows x86 / Windows x64: shipped per-arch and supported.
#   - macOS: shipped as a universal .pkg (x86_64 + arm64 fat binary) — both
#     Apple Silicon and Intel resolve through deps/+sdrplay/install_macos.cmake.
#   - Linux ARM / Windows ARM64 / Android: no usable binary, auto-disable.
# The recipe and plugin both gate on OPT_BUILD_SDRPLAY_SOURCE, so flipping it
# OFF here centralizes the policy for every entry point (CI scripts, dev
# builds, IDEs).
if (OPT_BUILD_SDRPLAY_SOURCE)
    set(_sdrplay_unsupported "")
    if (ANDROID)
        set(_sdrplay_unsupported "Android")
    elseif (WIN32 AND (CMAKE_GENERATOR_PLATFORM MATCHES "^ARM64"
                       OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)"))
        set(_sdrplay_unsupported "Windows on ARM64")
    elseif (UNIX AND NOT APPLE AND NOT ANDROID
            AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|armv6|armv7)")
        set(_sdrplay_unsupported "Linux ${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    if (_sdrplay_unsupported)
        message(STATUS "SDRplay: no upstream binary available for ${_sdrplay_unsupported}; disabling OPT_BUILD_SDRPLAY_SOURCE")
        set(OPT_BUILD_SDRPLAY_SOURCE OFF CACHE BOOL
            "Build SDRplay Source Module (Dependencies: libsdrplay)" FORCE)
    endif ()
endif ()

include(${CMAKE_SOURCE_DIR}/cmake/sdrpp_collect_required_deps.cmake)
sdrpp_collect_required_dep_roots(_sdrpp_dep_root_packages_auto)
if (SDRPP_DEP_ROOT_PACKAGES)
    _sdrpp_normalize_dep_list(_sdrpp_dep_root_packages_effective "${SDRPP_DEP_ROOT_PACKAGES}")
    message(STATUS "Effective dependency roots (override): ${_sdrpp_dep_root_packages_effective}")
else ()
    set(_sdrpp_dep_root_packages_effective ${_sdrpp_dep_root_packages_auto})
    message(STATUS "Effective dependency roots (auto): ${_sdrpp_dep_root_packages_effective}")
endif ()

if (OPT_BUILD_DEPS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/deps/autobuild.cmake)
endif ()

# Module cmake path
set(SDRPP_MODULE_CMAKE "${CMAKE_SOURCE_DIR}/sdrpp_module.cmake")

# Windows runtime layout mirrors the ZIP bundle: the application and shared
# runtime DLLs live in the config root, while SDR++ plugins live under modules.
if (MSVC)
    set(SDRPP_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    set(SDRPP_MODULE_RUNTIME_OUTPUT_DIRECTORY "${SDRPP_RUNTIME_OUTPUT_DIRECTORY}/modules")
endif ()

# Root source folder
set(SDRPP_CORE_ROOT "${CMAKE_SOURCE_DIR}/core/src/")

# Compiler flags
if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    # Debug Flags
    if (MSVC)
        set(SDRPP_COMPILER_FLAGS /std:c++17 /EHsc)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(SDRPP_COMPILER_FLAGS -g -Og -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
    else ()
        set(SDRPP_COMPILER_FLAGS -g -Og -std=c++17)
    endif ()
else()
    # Normal Flags
    if (MSVC)
        set(SDRPP_COMPILER_FLAGS
            /std:c++17
            /EHsc
            $<$<CONFIG:Release>:/O2 /Ob2>
            $<$<CONFIG:RelWithDebInfo>:/O2 /Ob2>
            $<$<CONFIG:Debug>:/Od>
        )
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SDRPP_COMPILER_FLAGS -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
    else ()
        set(SDRPP_COMPILER_FLAGS -O3 -std=c++17)
    endif ()
endif()
set(SDRPP_MODULE_COMPILER_FLAGS ${SDRPP_COMPILER_FLAGS})

# Set a default install prefix
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "..." FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "..." FORCE)
    endif()
endif()

# Configure toolchain for android
if (ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate"
    )
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX14_EXTENSION_COMPILE_OPTION "-std=c++17")
endif (ANDROID)

# Provide sdrpp_link_dep() helper to all module subdirectories. Try Config.cmake
# first, fall back to pkg-config — keeps Linux apt/dpkg flow working while
# letting Windows resolve via our deps install prefix.
include(${CMAKE_SOURCE_DIR}/cmake/sdrpp_find_dep.cmake)

# Core of SDR++
add_subdirectory("core")

# Source modules
if (OPT_BUILD_AIRSPY_SOURCE)
add_subdirectory("source_modules/airspy_source")
endif (OPT_BUILD_AIRSPY_SOURCE)

if (OPT_BUILD_AIRSPYHF_SOURCE)
add_subdirectory("source_modules/airspyhf_source")
endif (OPT_BUILD_AIRSPYHF_SOURCE)

if (OPT_BUILD_AUDIO_SOURCE)
add_subdirectory("source_modules/audio_source")
endif (OPT_BUILD_AUDIO_SOURCE)

if (OPT_BUILD_BADGESDR_SOURCE)
add_subdirectory("source_modules/badgesdr_source")
endif (OPT_BUILD_BADGESDR_SOURCE)

if (OPT_BUILD_BLADERF_SOURCE)
add_subdirectory("source_modules/bladerf_source")
endif (OPT_BUILD_BLADERF_SOURCE)

if (OPT_BUILD_FILE_SOURCE)
add_subdirectory("source_modules/file_source")
endif (OPT_BUILD_FILE_SOURCE)

if (OPT_BUILD_FOBOSSDR_SOURCE)
add_subdirectory("source_modules/fobossdr_source")
endif (OPT_BUILD_FOBOSSDR_SOURCE)

if (OPT_BUILD_HACKRF_SOURCE)
add_subdirectory("source_modules/hackrf_source")
endif (OPT_BUILD_HACKRF_SOURCE)

if (OPT_BUILD_HAROGIC_SOURCE)
add_subdirectory("source_modules/harogic_source")
endif (OPT_BUILD_HAROGIC_SOURCE)

if (OPT_BUILD_HERMES_SOURCE)
add_subdirectory("source_modules/hermes_source")
endif (OPT_BUILD_HERMES_SOURCE)

if (OPT_BUILD_HYDRASDR_RFONE_SOURCE)
add_subdirectory("source_modules/hydrasdr_source")
endif (OPT_BUILD_HYDRASDR_RFONE_SOURCE)

if (OPT_BUILD_KCSDR_SOURCE)
add_subdirectory("source_modules/kcsdr_source")
endif (OPT_BUILD_KCSDR_SOURCE)

if (OPT_BUILD_KIWISDR_SOURCE)
add_subdirectory("source_modules/kiwisdr_source")
endif (OPT_BUILD_KIWISDR_SOURCE)

if (OPT_BUILD_LIMESDR_SOURCE)
add_subdirectory("source_modules/limesdr_source")
endif (OPT_BUILD_LIMESDR_SOURCE)

if (OPT_BUILD_NETWORK_SOURCE)
add_subdirectory("source_modules/network_source")
endif (OPT_BUILD_NETWORK_SOURCE)

if (OPT_BUILD_PERSEUS_SOURCE)
add_subdirectory("source_modules/perseus_source")
endif (OPT_BUILD_PERSEUS_SOURCE)

if (OPT_BUILD_PLUTOSDR_SOURCE)
add_subdirectory("source_modules/plutosdr_source")
endif (OPT_BUILD_PLUTOSDR_SOURCE)

if (OPT_BUILD_QMX_SOURCE)
add_subdirectory("source_modules/qmx_source")
endif (OPT_BUILD_QMX_SOURCE)

if (OPT_BUILD_QMXSERVER_SOURCE)
add_subdirectory("source_modules/qmxserver_source")
endif (OPT_BUILD_QMXSERVER_SOURCE)

if (OPT_BUILD_RFNM_SOURCE)
add_subdirectory("source_modules/rfnm_source")
endif (OPT_BUILD_RFNM_SOURCE)

if (OPT_BUILD_RFSPACE_SOURCE)
add_subdirectory("source_modules/rfspace_source")
endif (OPT_BUILD_RFSPACE_SOURCE)

if (OPT_BUILD_RTL_SDR_SOURCE)
add_subdirectory("source_modules/rtl_sdr_source")
endif (OPT_BUILD_RTL_SDR_SOURCE)

if (OPT_BUILD_RTL_TCP_SOURCE)
add_subdirectory("source_modules/rtl_tcp_source")
endif (OPT_BUILD_RTL_TCP_SOURCE)

if (OPT_BUILD_SDDC_SOURCE)
add_subdirectory("source_modules/sddc_source")
endif (OPT_BUILD_SDDC_SOURCE)

if (OPT_BUILD_SDRPP_SERVER_SOURCE)
add_subdirectory("source_modules/sdrpp_server_source")
endif (OPT_BUILD_SDRPP_SERVER_SOURCE)

if (OPT_BUILD_SDRPLAY_SOURCE)
add_subdirectory("source_modules/sdrplay_source")
endif (OPT_BUILD_SDRPLAY_SOURCE)

if (OPT_BUILD_SOAPY_SOURCE)
add_subdirectory("source_modules/soapy_source")
endif (OPT_BUILD_SOAPY_SOURCE)

if (OPT_BUILD_SPECTRAN_SOURCE)
add_subdirectory("source_modules/spectran_source")
endif (OPT_BUILD_SPECTRAN_SOURCE)

if (OPT_BUILD_SPECTRAN_HTTP_SOURCE)
add_subdirectory("source_modules/spectran_http_source")
endif (OPT_BUILD_SPECTRAN_HTTP_SOURCE)

if (OPT_BUILD_SPYSERVER_SOURCE)
add_subdirectory("source_modules/spyserver_source")
endif (OPT_BUILD_SPYSERVER_SOURCE)

if (OPT_BUILD_USRP_SOURCE)
add_subdirectory("source_modules/usrp_source")
endif (OPT_BUILD_USRP_SOURCE)


# Sink modules
if (OPT_BUILD_ANDROID_AUDIO_SINK)
add_subdirectory("sink_modules/android_audio_sink")
endif (OPT_BUILD_ANDROID_AUDIO_SINK)

if (OPT_BUILD_AUDIO_SINK)
add_subdirectory("sink_modules/audio_sink")
endif (OPT_BUILD_AUDIO_SINK)

if (OPT_BUILD_PORTAUDIO_SINK)
add_subdirectory("sink_modules/portaudio_sink")
endif (OPT_BUILD_PORTAUDIO_SINK)

if (OPT_BUILD_NETWORK_SINK)
add_subdirectory("sink_modules/network_sink")
endif (OPT_BUILD_NETWORK_SINK)

if (OPT_BUILD_NEW_PORTAUDIO_SINK)
add_subdirectory("sink_modules/new_portaudio_sink")
endif (OPT_BUILD_NEW_PORTAUDIO_SINK)


# Decoders
if (OPT_BUILD_ATV_DECODER)
add_subdirectory("decoder_modules/atv_decoder")
endif (OPT_BUILD_ATV_DECODER)

if (OPT_BUILD_DAB_DECODER)
add_subdirectory("decoder_modules/dab_decoder")
endif (OPT_BUILD_DAB_DECODER)

if (OPT_BUILD_FALCON9_DECODER)
add_subdirectory("decoder_modules/falcon9_decoder")
endif (OPT_BUILD_FALCON9_DECODER)

if (OPT_BUILD_KG_SSTV_DECODER)
add_subdirectory("decoder_modules/kg_sstv_decoder")
endif (OPT_BUILD_KG_SSTV_DECODER)

if (OPT_BUILD_M17_DECODER)
add_subdirectory("decoder_modules/m17_decoder")
endif (OPT_BUILD_M17_DECODER)

if (OPT_BUILD_METEOR_DEMODULATOR)
add_subdirectory("decoder_modules/meteor_demodulator")
endif (OPT_BUILD_METEOR_DEMODULATOR)

if (OPT_BUILD_PAGER_DECODER)
add_subdirectory("decoder_modules/pager_decoder")
endif (OPT_BUILD_PAGER_DECODER)

if (OPT_BUILD_RADIO)
add_subdirectory("decoder_modules/radio")
endif (OPT_BUILD_RADIO)

if (OPT_BUILD_RADIOSONDE_DECODER)
add_subdirectory("decoder_modules/radiosonde_decoder")
endif (OPT_BUILD_RADIOSONDE_DECODER)

if (OPT_BUILD_RYFI_DECODER)
add_subdirectory("decoder_modules/ryfi_decoder")
endif (OPT_BUILD_RYFI_DECODER)

if (OPT_BUILD_VOR_RECEIVER)
add_subdirectory("decoder_modules/vor_receiver")
endif (OPT_BUILD_VOR_RECEIVER)

if (OPT_BUILD_WEATHER_SAT_DECODER)
add_subdirectory("decoder_modules/weather_sat_decoder")
endif (OPT_BUILD_WEATHER_SAT_DECODER)


# Misc
if (OPT_BUILD_DISCORD_PRESENCE)
add_subdirectory("misc_modules/discord_integration")
endif (OPT_BUILD_DISCORD_PRESENCE)

if (OPT_BUILD_FREQUENCY_MANAGER)
add_subdirectory("misc_modules/frequency_manager")
endif (OPT_BUILD_FREQUENCY_MANAGER)

if (OPT_BUILD_IQ_EXPORTER)
add_subdirectory("misc_modules/iq_exporter")
endif (OPT_BUILD_IQ_EXPORTER)

if (OPT_BUILD_RECORDER)
add_subdirectory("misc_modules/recorder")
endif (OPT_BUILD_RECORDER)

if (OPT_BUILD_RIGCTL_CLIENT)
add_subdirectory("misc_modules/rigctl_client")
endif (OPT_BUILD_RIGCTL_CLIENT)

if (OPT_BUILD_RIGCTL_SERVER)
add_subdirectory("misc_modules/rigctl_server")
endif (OPT_BUILD_RIGCTL_SERVER)

if (OPT_BUILD_SCANNER)
add_subdirectory("misc_modules/scanner")
endif (OPT_BUILD_SCANNER)

if (OPT_BUILD_SCHEDULER)
add_subdirectory("misc_modules/scheduler")
endif (OPT_BUILD_SCHEDULER)

if (OPT_BUILD_SPOTS)
add_subdirectory("misc_modules/spots")
endif (OPT_BUILD_SPOTS)

if (OPT_BUILD_WEBSDR_VIEW)
add_subdirectory("misc_modules/websdr_view")
endif (OPT_BUILD_WEBSDR_VIEW)

if (MSVC)
    add_executable(sdrpp "src/main.cpp" "win32/resources.rc")
else ()
    add_executable(sdrpp "src/main.cpp")
endif ()

set_target_properties(sdrpp PROPERTIES OUTPUT_NAME "sdrpp-iak")
if (MSVC)
    set_target_properties(sdrpp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${SDRPP_RUNTIME_OUTPUT_DIRECTORY}")
endif ()

target_link_libraries(sdrpp PRIVATE sdrpp_core)

# libcurl is resolved by deps/+libcurl (bundled profiles) or via the distro
# package (Linux). Static deps builds absorb it into sdrpp_core; system mode
# picks up the distro libcurl through the dynamic linker's default search.

# Compiler arguments
target_compile_options(sdrpp PRIVATE ${SDRPP_COMPILER_FLAGS})

# Copy dynamic libs over
if (MSVC)
    get_property(_sdrpp_runtime_dlls GLOBAL PROPERTY SDRPP_RUNTIME_DLLS)
    if (_sdrpp_runtime_dlls)
        list(REMOVE_DUPLICATES _sdrpp_runtime_dlls)
    endif ()

    set(_sdrpp_runtime_source_dirs "")
    foreach (_pp ${CMAKE_PREFIX_PATH})
        if (EXISTS "${_pp}/bin")
            list(APPEND _sdrpp_runtime_source_dirs "${_pp}/bin")
        endif ()
    endforeach ()
    list(REMOVE_DUPLICATES _sdrpp_runtime_source_dirs)

    set(_sdrpp_runtime_manifest "${CMAKE_CURRENT_BINARY_DIR}/sdrpp_runtime_dlls_$<CONFIG>.cmake")
    set(_sdrpp_runtime_manifest_content "set(SDRPP_RUNTIME_DLLS\n")
    foreach (_dll IN LISTS _sdrpp_runtime_dlls)
        string(APPEND _sdrpp_runtime_manifest_content "    [=[${_dll}]=]\n")
    endforeach ()
    string(APPEND _sdrpp_runtime_manifest_content ")\nset(SDRPP_RUNTIME_SOURCE_DIRS\n")
    foreach (_dir IN LISTS _sdrpp_runtime_source_dirs)
        string(APPEND _sdrpp_runtime_manifest_content "    [=[${_dir}]=]\n")
    endforeach ()
    string(APPEND _sdrpp_runtime_manifest_content ")\n")
    file(GENERATE OUTPUT "${_sdrpp_runtime_manifest}" CONTENT "${_sdrpp_runtime_manifest_content}")

    get_filename_component(_sdrpp_msvc_bin_dir "${CMAKE_LINKER}" DIRECTORY)
    find_program(_sdrpp_dumpbin dumpbin HINTS "${_sdrpp_msvc_bin_dir}" NO_DEFAULT_PATH)

    add_custom_command(TARGET sdrpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/root" "$<TARGET_FILE_DIR:sdrpp>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sdrpp>/recordings"
        COMMENT "Copying Windows runtime root skeleton")

    add_custom_command(TARGET sdrpp POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DRUNTIME_MANIFEST="${_sdrpp_runtime_manifest}"
            -DDST="$<TARGET_FILE_DIR:sdrpp>"
            -DRUNTIME_DEPENDENCIES_COMMAND="${_sdrpp_dumpbin}"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_runtime_dlls.cmake"
        COMMENT "Copying linked runtime DLLs")

    if (COPY_MSVC_REDISTRIBUTABLES)
        # Get the list of Visual C++ runtime DLLs
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP True)
        include(InstallRequiredSystemLibraries)

        set(_sdrpp_msvc_redist_manifest "${CMAKE_CURRENT_BINARY_DIR}/sdrpp_msvc_redist_$<CONFIG>.cmake")
        set(_sdrpp_msvc_redist_manifest_content "set(SDRPP_MSVC_REDIST_DLLS\n")
        foreach (_dll IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            string(APPEND _sdrpp_msvc_redist_manifest_content "    [=[${_dll}]=]\n")
        endforeach ()
        string(APPEND _sdrpp_msvc_redist_manifest_content ")\n")
        file(GENERATE OUTPUT "${_sdrpp_msvc_redist_manifest}" CONTENT "${_sdrpp_msvc_redist_manifest_content}")

        add_custom_target(do_always_msvc ALL
            COMMAND ${CMAKE_COMMAND}
                -DCONFIG="$<CONFIG>"
                -DDST="$<TARGET_FILE_DIR:sdrpp>"
                -DREDIST_MANIFEST="${_sdrpp_msvc_redist_manifest}"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_msvc_redist.cmake"
            COMMENT "Copying MSVC redistributables"
            VERBATIM)
        add_dependencies(do_always_msvc sdrpp)
    endif ()
endif ()


if (${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD")
    add_custom_target(do_always ALL cp \"$<TARGET_FILE_DIR:sdrpp_core>/libsdrpp_core.so\" \"$<TARGET_FILE_DIR:sdrpp>\")
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    target_link_libraries(sdrpp PUBLIC pthread)
    add_custom_target(do_always ALL cp \"$<TARGET_FILE:sdrpp_core>\" \"$<TARGET_FILE_DIR:sdrpp>\")
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_custom_target(do_always ALL cp \"$<TARGET_FILE:sdrpp_core>\" \"$<TARGET_FILE_DIR:sdrpp>\")
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_custom_target(do_always ALL cp \"$<TARGET_FILE_DIR:sdrpp_core>/libsdrpp_core.dylib\" \"$<TARGET_FILE_DIR:sdrpp>\")
endif ()

# cmake .. "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_SCANNER=ON -DOPT_BUILD_SCHEDULER=ON -DOPT_BUILD_USRP_SOURCE=ON -DOPT_BUILD_PAGER_DECODER=ON

# Create module cmake file
configure_file(${CMAKE_SOURCE_DIR}/sdrpp_module.cmake ${CMAKE_CURRENT_BINARY_DIR}/sdrpp_module.cmake @ONLY)

# Install directives
install(TARGETS sdrpp DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/root/res/bandplans DESTINATION share/sdrpp-iak)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/root/res/colormaps DESTINATION share/sdrpp-iak)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/root/res/cty DESTINATION share/sdrpp-iak)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/root/res/fonts DESTINATION share/sdrpp-iak)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/root/res/icons DESTINATION share/sdrpp-iak)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/root/res/themes DESTINATION share/sdrpp-iak)
configure_file(${CMAKE_SOURCE_DIR}/sdrpp-iak.desktop ${CMAKE_CURRENT_BINARY_DIR}/sdrpp-iak.desktop @ONLY)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sdrpp-iak.desktop DESTINATION share/applications)
endif ()

if (MSVC)
    set(SDRPP_WINDOWS_BUNDLE_STAGE_ROOT "${CMAKE_BINARY_DIR}/stage/windows-bundle" CACHE INTERNAL "Root directory for Windows bundle staging" FORCE)
    set(_sdrpp_windows_bundle_stage_dir "${SDRPP_WINDOWS_BUNDLE_STAGE_ROOT}/$<CONFIG>")
    get_property(_sdrpp_plugin_targets GLOBAL PROPERTY SDRPP_PLUGIN_TARGETS)

    add_custom_target(stage_windows_bundle
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${_sdrpp_windows_bundle_stage_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_sdrpp_windows_bundle_stage_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/root" "${_sdrpp_windows_bundle_stage_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_sdrpp_windows_bundle_stage_dir}/modules"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:sdrpp>" "${_sdrpp_windows_bundle_stage_dir}"
        COMMENT "Staging Windows bundle under ${SDRPP_WINDOWS_BUNDLE_STAGE_ROOT}"
        VERBATIM)
    add_dependencies(stage_windows_bundle sdrpp)
    if (TARGET do_always)
        add_dependencies(stage_windows_bundle do_always)
    endif ()
    if (TARGET do_always_msvc)
        add_dependencies(stage_windows_bundle do_always_msvc)
    endif ()

    foreach (_sdrpp_plugin_target IN LISTS _sdrpp_plugin_targets)
        add_dependencies(stage_windows_bundle ${_sdrpp_plugin_target})
        add_custom_command(TARGET stage_windows_bundle POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${_sdrpp_plugin_target}>" "${_sdrpp_windows_bundle_stage_dir}/modules/"
            VERBATIM)
    endforeach ()
endif ()

if (APPLE)
    set(SDRPP_MACOS_BUNDLE_STAGE_ROOT "${CMAKE_BINARY_DIR}/stage/macos-bundle" CACHE INTERNAL "Root directory for macOS bundle staging" FORCE)
    set(_sdrpp_macos_bundle_dir "${SDRPP_MACOS_BUNDLE_STAGE_ROOT}/$<CONFIG>/SDR++-iak.app")
    get_property(_sdrpp_plugin_targets GLOBAL PROPERTY SDRPP_PLUGIN_TARGETS)

    set(_sdrpp_stage_macos_commands
        COMMAND ${CMAKE_COMMAND} -E env
            sh "${CMAKE_SOURCE_DIR}/macos/stage_macos_bundle.sh"
            "${_sdrpp_macos_bundle_dir}"
            "$<TARGET_FILE:sdrpp>"
            "$<TARGET_FILE:sdrpp_core>")

    foreach (_sdrpp_plugin_target IN LISTS _sdrpp_plugin_targets)
        list(APPEND _sdrpp_stage_macos_commands "$<TARGET_FILE:${_sdrpp_plugin_target}>")
    endforeach ()

    add_custom_target(stage_macos_bundle
        ${_sdrpp_stage_macos_commands}
        COMMENT "Staging macOS app bundle under ${SDRPP_MACOS_BUNDLE_STAGE_ROOT}"
        VERBATIM)
    add_dependencies(stage_macos_bundle sdrpp sdrpp_core)
    foreach (_sdrpp_plugin_target IN LISTS _sdrpp_plugin_targets)
        add_dependencies(stage_macos_bundle ${_sdrpp_plugin_target})
    endforeach ()
endif ()

# Create uninstall target
configure_file(${CMAKE_SOURCE_DIR}/cmake_uninstall.cmake ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake @ONLY)
add_custom_target(uninstall ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

# Create headers target
