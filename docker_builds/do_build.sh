#!/bin/bash
set -e
cd /root

# VOLK_PACKAGE: volk library package name (libvolk2-dev or libvolk-dev)
# EXTRA_APT: space-separated extra apt packages (e.g. ca-certificates)
# CURL_SOURCE: 'system' uses the distro libcurl; 'bundled' forces deps to
# build libcurl from source (needs libssl-dev for the OpenSSL backend).
VOLK_PACKAGE=${VOLK_PACKAGE:-libvolk2-dev}
CURL_SOURCE=${CURL_SOURCE:-system}

CURL_DEP_ARG=""
if [ "$CURL_SOURCE" = "bundled" ]; then
    EXTRA_APT="${EXTRA_APT:-} libssl-dev"
    CURL_DEP_ARG="-DSDRPP_DEP_FORCE_BUNDLED=libcurl"
fi

# Install dependencies and tools
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 install -y build-essential cmake git pkg-config libfftw3-dev libglfw3-dev ${VOLK_PACKAGE} libzstd-dev libcurl4-openssl-dev libairspy-dev libairspyhf-dev \
    libiio-dev libad9361-dev librtaudio-dev libhackrf-dev librtlsdr-dev libbladerf-dev liblimesuite-dev p7zip-full wget portaudio19-dev \
    libcodec2-dev autoconf libtool xxd libspdlog-dev ${EXTRA_APT}

# Allow git commands on the volume-mounted repo (git ≥ 2.35.2 safe.directory check)
git config --global --add safe.directory /root/SDRPlusPlus

cd SDRPlusPlus
mkdir build && cd build
cmake .. ${CURL_DEP_ARG} -DOPT_BUILD_DEPS=ON -DSDRPP_DEPS_PRESET=default -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON \
    -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_PERSEUS_SOURCE=ON \
    -DOPT_BUILD_RFNM_SOURCE=ON -DOPT_BUILD_FOBOSSDR_SOURCE=ON -DOPT_BUILD_HYDRASDR_RFONE_SOURCE=ON
make VERBOSE=1 -j2

cd ..
sh make_debian_package.sh ./build
