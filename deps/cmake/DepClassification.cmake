#
# Initial dependency classification table for SDR++.
#
# Linkage defaults stay shared unless a dependency has been reviewed and marked
# as a static source-build candidate. Distro/system packages stay shared.
#

if (DEFINED SDRPP_DEP_CLASSIFICATION_LOADED)
    return()
endif ()
set(SDRPP_DEP_CLASSIFICATION_LOADED TRUE)

# Core/runtime dependencies.
sdrpp_register_dep(fftw3
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE core)

sdrpp_register_dep(glfw3
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE core)

sdrpp_register_dep(volk
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE core)

sdrpp_register_dep(zstd
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE core)

# Shared runtime dependencies used by multiple modules or by multiple other deps.
sdrpp_register_dep(libusb
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE shared-runtime)

sdrpp_register_dep(portaudio
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE shared-runtime)

sdrpp_register_dep(rtaudio
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE shared-runtime)

sdrpp_register_dep(zlib
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE shared-runtime)

sdrpp_register_dep(pthreads
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE shared-runtime)

# Single-module / feature-specific dependencies.
sdrpp_register_dep(codec2
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)

sdrpp_register_dep(libad9361
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE module-private)

sdrpp_register_dep(libairspy
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE module-private)

sdrpp_register_dep(libairspyhf
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE module-private)

sdrpp_register_dep(libbladeRF
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)

sdrpp_register_dep(libcurl
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE core)

sdrpp_register_dep(mbedtls
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:static distro:static android:static
    USAGE module-private)

sdrpp_register_dep(libfobos
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:static distro:static android:static
    USAGE module-private)

sdrpp_register_dep(libhackrf
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)

sdrpp_register_dep(libhydrasdr
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:static distro:static android:static
    USAGE module-private)

sdrpp_register_dep(libiio
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE module-private)

sdrpp_register_dep(libmirisdr
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)

sdrpp_register_dep(libperseus-sdr
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:static distro:static android:static
    USAGE module-private)

sdrpp_register_dep(librfnm
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)

sdrpp_register_dep(librtlsdr
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE module-private)

sdrpp_register_dep(libxml2
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:static distro:shared android:static
    USAGE module-private)

sdrpp_register_dep(LimeSuite
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)

sdrpp_register_dep(sdrplay
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)

sdrpp_register_dep(sondedump
    DEFAULT_SOURCE  portable:bundled distro:bundled android:bundled
    DEFAULT_LINKAGE portable:static distro:static android:static
    USAGE module-private)

sdrpp_register_dep(spdlog
    DEFAULT_SOURCE  portable:bundled distro:system android:bundled
    DEFAULT_LINKAGE portable:shared distro:shared android:shared
    USAGE module-private)
