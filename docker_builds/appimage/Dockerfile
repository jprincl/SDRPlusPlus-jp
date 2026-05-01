FROM ubuntu:focal
ENV DEBIAN_FRONTEND=noninteractive

# Build toolchain and SDR++ runtime dependencies.
# Focal is chosen for its old glibc baseline (2.31) — the AppImage will run on
# any Linux distro shipped after early 2020.
RUN apt-get -o Acquire::Retries=3 update && \
    apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
        build-essential cmake git pkg-config ca-certificates \
        wget unzip p7zip-full file patchelf xxd \
        autoconf libtool \
        libfftw3-dev libglfw3-dev libvolk2-dev libzstd-dev \
        libairspy-dev libairspyhf-dev libhackrf-dev librtlsdr-dev \
        libbladerf-dev liblimesuite-dev \
        libiio-dev libad9361-dev \
        librtaudio-dev portaudio19-dev libpulse-dev \
        libcodec2-dev libspdlog-dev liborc-0.4-dev && \
    rm -rf /var/lib/apt/lists/*

# Install SDRplay API (required — no silent fallback).
RUN wget -q https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run -O /tmp/sdrplay.run && \
    7z x /tmp/sdrplay.run -o/tmp/sdrplay_extracted >/dev/null && \
    7z x /tmp/sdrplay_extracted -o/tmp/sdrplay_pkg >/dev/null && \
    cp /tmp/sdrplay_pkg/$(dpkg --print-architecture)/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so && \
    cp /tmp/sdrplay_pkg/inc/* /usr/include/ && \
    ldconfig && \
    rm -rf /tmp/sdrplay*

# Install libperseus-sdr.
RUN cd /tmp && \
    git clone --depth=1 https://github.com/Microtelecom/libperseus-sdr && \
    cd libperseus-sdr && \
    autoreconf -i && \
    ./configure && \
    make -j"$(nproc)" && \
    make install && \
    ldconfig && \
    rm -rf /tmp/libperseus-sdr

# Install librfnm.
RUN cd /tmp && \
    git clone --depth=1 https://github.com/AlexandreRouma/librfnm && \
    cd librfnm && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr && \
    make -j"$(nproc)" && \
    make install && \
    ldconfig && \
    rm -rf /tmp/librfnm

# Install libfobos.
RUN cd /tmp && \
    git clone --depth=1 https://github.com/AlexandreRouma/libfobos && \
    cd libfobos && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr && \
    make -j"$(nproc)" && \
    make install && \
    ldconfig && \
    rm -rf /tmp/libfobos

# Install libhydrasdr (rfone_host).
RUN cd /tmp && \
    git clone --depth=1 https://github.com/hydrasdr/rfone_host && \
    cd rfone_host && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr && \
    make -j"$(nproc)" && \
    make install && \
    ldconfig && \
    rm -rf /tmp/rfone_host

# Install linuxdeploy and appimagetool for the host architecture.
# Both ship as AppImages that need FUSE — inside Docker we extract them once.
RUN ARCH=$(uname -m) && \
    wget -q -O /tmp/linuxdeploy.AppImage \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" && \
    chmod +x /tmp/linuxdeploy.AppImage && \
    cd /tmp && /tmp/linuxdeploy.AppImage --appimage-extract >/dev/null && \
    mv squashfs-root /opt/linuxdeploy && \
    ln -sf /opt/linuxdeploy/AppRun /usr/local/bin/linuxdeploy && \
    rm /tmp/linuxdeploy.AppImage && \
    wget -q -O /tmp/appimagetool.AppImage \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${ARCH}.AppImage" && \
    chmod +x /tmp/appimagetool.AppImage && \
    cd /tmp && /tmp/appimagetool.AppImage --appimage-extract >/dev/null && \
    mv squashfs-root /opt/appimagetool && \
    ln -sf /opt/appimagetool/AppRun /usr/local/bin/appimagetool && \
    rm /tmp/appimagetool.AppImage

COPY make_appimage.sh /root/make_appimage.sh
RUN chmod +x /root/make_appimage.sh
