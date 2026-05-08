#
# sdrpp_link_dep(<target> <pkg> [PC_NAME <name>] [TARGETS <names...>])
#
# Find a third-party library and link it into <target>. Strategy:
#   1. Try find_package(<pkg> CONFIG) — works when the dep ships a Config.cmake
#      (covers Windows + our deps build, plus most distros' -dev packages).
#   2. Fall back to pkg_check_modules with IMPORTED_TARGET — covers Linux with
#      apt/dpkg-installed -dev packages that ship only .pc files, and our
#      deps prefix when a recipe installs only pkg-config.
#
# Args:
#   <target>           consumer target to link the dependency into
#   <pkg>              name passed to find_package() and the default pkg-config
#                      module name; also the default basis for imported target
#                      names (<pkg>::<pkg>, <pkg>)
#   PC_NAME <name>     pkg-config module name if it differs from <pkg>
#                      (e.g. libusb's pkg-config name is "libusb-1.0")
#   TARGETS <names>    candidate IMPORTED target names exported by Config.cmake.
#                      Defaults to "<pkg>::<pkg>;<pkg>". Override when upstream
#                      uses a non-conventional naming (e.g. FFTW3 exports
#                      "FFTW3::fftw3f").
#
# Examples:
#   sdrpp_link_dep(hackrf_source libhackrf)
#   sdrpp_link_dep(rtl_sdr_source librtlsdr TARGETS librtlsdr::rtlsdr rtlsdr)
#   sdrpp_link_dep(plutosdr_source libusb PC_NAME libusb-1.0)
#

if (COMMAND sdrpp_link_dep)
    return()
endif ()

function(sdrpp_link_dep target pkg)
    cmake_parse_arguments(P "" "PC_NAME" "TARGETS" ${ARGN})

    set(_pc ${P_PC_NAME})
    if (NOT _pc)
        set(_pc ${pkg})
    endif ()

    set(_targets ${P_TARGETS})
    if (NOT _targets)
        set(_targets ${pkg}::${pkg} ${pkg})
    endif ()

    # Step 1 — CMake config package (Windows + most modern -dev packages).
    find_package(${pkg} CONFIG QUIET)
    foreach (t ${_targets})
        if (TARGET ${t})
            target_link_libraries(${target} PRIVATE ${t})
            return()
        endif ()
    endforeach ()

    # Step 2 — pkg-config (Linux apt/dpkg + macOS brew + bare .pc installs).
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_${pkg}_PC REQUIRED IMPORTED_TARGET ${_pc})
    target_link_libraries(${target} PRIVATE PkgConfig::_${pkg}_PC)
endfunction()
