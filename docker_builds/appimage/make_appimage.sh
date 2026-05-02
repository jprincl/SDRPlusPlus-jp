#!/bin/bash
# Build a portable SDR++ iak AppImage.
# Run inside the docker_builds/appimage Dockerfile. Source tree must be
# bind-mounted at /root/SDRPlusPlus.
# Output: /root/sdrpp-iak-<arch>.AppImage
set -euo pipefail

SRC_DIR="${SRC_DIR:-/root/SDRPlusPlus}"
ARCH="$(uname -m)"
APP_NAME="sdrpp-iak"
APPDIR="/AppDir"
# VERSION_FULL is set by CI (matches the version in core/src/version.h plus
# git build info, e.g. 1.2.3+45-gabc1234). Required — the filename should
# always identify the build.
: "${VERSION_FULL:?VERSION_FULL must be set (e.g. 1.2.3+45-gabc1234)}"
OUT="/root/${APP_NAME}-${VERSION_FULL}-${ARCH}.AppImage"

echo "=== Building AppImage for ${APP_NAME} ${VERSION_FULL} (${ARCH}) ==="
echo "Source: ${SRC_DIR}"

# Allow git operations on the bind-mounted tree.
git config --global --add safe.directory "${SRC_DIR}"

# ---------------------------------------------------------------------------
# Configure & build SDR++ with /usr install prefix.
# /usr is required: linuxdeploy expects an AppDir layout rooted at usr/.
# ---------------------------------------------------------------------------
cd "${SRC_DIR}"
rm -rf build
mkdir build
cd build

cmake "${SRC_DIR}" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDRPP_CURL_SOURCE=bundled \
    -DOPT_BUILD_APPIMAGE=ON \
    -DOPT_BUILD_BLADERF_SOURCE=ON \
    -DOPT_BUILD_LIMESDR_SOURCE=ON \
    -DOPT_BUILD_SDRPLAY_SOURCE=ON \
    -DOPT_BUILD_PERSEUS_SOURCE=ON \
    -DOPT_BUILD_RFNM_SOURCE=ON \
    -DOPT_BUILD_FOBOSSDR_SOURCE=ON \
    -DOPT_BUILD_HYDRASDR_RFONE_SOURCE=ON \
    -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON \
    -DOPT_BUILD_M17_DECODER=ON

make -j"$(nproc)"

# ---------------------------------------------------------------------------
# Install into AppDir.
# ---------------------------------------------------------------------------
rm -rf "${APPDIR}"
make install DESTDIR="${APPDIR}"

# linuxdeploy expects the icon and desktop file at the AppDir root in addition
# to the standard usr/share/... locations.
cp "${APPDIR}/usr/share/${APP_NAME}/icons/sdrpp.png" "${APPDIR}/${APP_NAME}.png"

# CMake-generated desktop file points at /usr/bin/sdrpp-iak. AppImages need
# Exec= to be a bare binary name and Icon= to be a basename (no extension).
DESKTOP_SRC="${APPDIR}/usr/share/applications/${APP_NAME}.desktop"
DESKTOP_DST="${APPDIR}/${APP_NAME}.desktop"
sed -e "s|^Exec=.*|Exec=${APP_NAME}|" \
    -e "s|^Icon=.*|Icon=${APP_NAME}|" \
    "${DESKTOP_SRC}" > "${DESKTOP_DST}"
cp "${DESKTOP_DST}" "${DESKTOP_SRC}"

# ---------------------------------------------------------------------------
# Bundle dependencies. GPU/X11/glibc/glfw libs come from the host — bundling
# them breaks driver ABI on the user's machine.
# ---------------------------------------------------------------------------
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH:-}"

linuxdeploy \
    --appdir "${APPDIR}" \
    --desktop-file "${DESKTOP_DST}" \
    --icon-file "${APPDIR}/${APP_NAME}.png" \
    --exclude-library "*libGL.so*" \
    --exclude-library "*libGLX.so*" \
    --exclude-library "*libEGL.so*" \
    --exclude-library "*libGLdispatch.so*" \
    --exclude-library "*libOpenGL.so*" \
    --exclude-library "*libdrm.so*" \
    --exclude-library "*libd3dadapter9.so*" \
    --exclude-library "*libvulkan.so*" \
    --exclude-library "*libxcb*" \
    --exclude-library "*libX11*" \
    --exclude-library "*libXext*" \
    --exclude-library "*libXrender*" \
    --exclude-library "*libnvidia*" \
    --exclude-library "*libvdpau*" \
    --exclude-library "*libLLVM*" \
    --exclude-library "*swrast*" \
    --exclude-library "*iris*" \
    --exclude-library "*radeonsi*" \
    --exclude-library "*nouveau*" \
    --exclude-library "*vmwgfx*" \
    --exclude-library "*libc.so*" \
    --exclude-library "*libglfw*"

# ---------------------------------------------------------------------------
# Replace linuxdeploy's generated AppRun with one that gives a friendly
# diagnostic when host-provided libglfw3 is missing — otherwise the user
# just sees the dynamic loader's "libglfw.so.3: cannot open shared object".
# ---------------------------------------------------------------------------
# linuxdeploy may create AppRun as a symlink to usr/bin/sdrpp-iak. Remove it
# first so the here-doc below creates a new launcher instead of overwriting
# the real app binary through that symlink.
rm -f "${APPDIR}/AppRun"
cat > "${APPDIR}/AppRun" <<'APPRUN_EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"

have_glfw() {
    for ldc in /sbin/ldconfig /usr/sbin/ldconfig ldconfig; do
        if command -v "$ldc" >/dev/null 2>&1; then
            "$ldc" -p 2>/dev/null | grep -qE 'libglfw\.so\.3' && return 0
        fi
    done
    for d in /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /lib /lib64; do
        [ -e "$d/libglfw.so.3" ] && return 0
    done
    return 1
}

if ! have_glfw; then
    cat >&2 <<'MSG'
ERROR: libglfw3 is not installed on this system.

SDR++ iak relies on the host's libglfw3 because GLFW links against your
system's OpenGL and X11 stack — bundling it inside the AppImage would
break GPU drivers. Install it with:

  Debian / Ubuntu / Mint:   sudo apt install libglfw3
  Fedora / RHEL / Rocky:    sudo dnf install glfw
  openSUSE:                 sudo zypper install libglfw3
  Arch / Manjaro:           sudo pacman -S glfw

Then re-run the AppImage.
MSG
    exit 1
fi

export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export PATH="${HERE}/usr/bin:${PATH}"
exec "${HERE}/usr/bin/sdrpp-iak" "$@"
APPRUN_EOF
chmod +x "${APPDIR}/AppRun"

# ---------------------------------------------------------------------------
# Pack the AppImage.
# ---------------------------------------------------------------------------
appimagetool "${APPDIR}" "${OUT}"

ls -lh "${OUT}"
echo "=== AppImage ready: ${OUT} ==="
