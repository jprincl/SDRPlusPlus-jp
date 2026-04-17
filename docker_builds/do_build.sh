#!/bin/bash
set -e
cd /root

# VOLK_PACKAGE: volk library package name (libvolk2-dev or libvolk-dev)
# EXTRA_APT: space-separated extra apt packages (e.g. ca-certificates)
VOLK_PACKAGE=${VOLK_PACKAGE:-libvolk2-dev}

# Install dependencies and tools
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 install -y build-essential cmake git libfftw3-dev libglfw3-dev ${VOLK_PACKAGE} libzstd-dev libairspy-dev libairspyhf-dev \
    libiio-dev libad9361-dev librtaudio-dev libhackrf-dev librtlsdr-dev libbladerf-dev liblimesuite-dev p7zip-full wget portaudio19-dev \
    libcodec2-dev autoconf libtool xxd libspdlog-dev ${EXTRA_APT}

# Install SDRPlay libraries
SDRPLAY_ARCH=$(dpkg --print-architecture)
wget https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2
cp $SDRPLAY_ARCH/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so
cp inc/* /usr/include/

# Install libperseus
git clone https://github.com/Microtelecom/libperseus-sdr
cd libperseus-sdr
autoreconf -i
./configure
make
make install
ldconfig
cd ..

# Install librfnm
git clone https://github.com/AlexandreRouma/librfnm
cd librfnm
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j2
make install
cd ../../

# Install libfobos
git clone https://github.com/AlexandreRouma/libfobos
cd libfobos
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j2
make install
cd ../../

# Build and install libhydrasdr
git clone https://github.com/hydrasdr/hydrasdr-host hydrasdr-host
cd hydrasdr-host
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make -j2
make install
cd ../../

cd SDRPlusPlus
mkdir build && cd build
cmake .. -DOPT_BUILD_BLADERF_SOURCE=ON -DOPT_BUILD_LIMESDR_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=ON \
    -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_PERSEUS_SOURCE=ON \
    -DOPT_BUILD_RFNM_SOURCE=ON -DOPT_BUILD_FOBOSSDR_SOURCE=ON -DOPT_BUILD_HYDRASDR_RFONE_SOURCE=ON
make VERBOSE=1 -j2

cd ..
sh make_debian_package.sh ./build "libfftw3-dev, libglfw3-dev, ${VOLK_PACKAGE}, librtaudio-dev, libzstd-dev"
