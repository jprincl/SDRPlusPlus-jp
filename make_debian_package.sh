#!/bin/sh

# Read the authoritative version from version.h
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_VERSION=$(grep -oE '"[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)[0-9]*)?"' "$SCRIPT_DIR/core/src/version.h" | tr -d '"')
if [ -z "$APP_VERSION" ]; then
    echo "ERROR: could not parse version from core/src/version.h" >&2
    exit 1
fi

# Determine the Debian version string.
#
# Debian uses '~' as a "pre-release" separator that sorts BELOW the base version,
# so "1.2.1~beta2" < "1.2.1" in apt's version comparison. We therefore replace
# the '-' in any pre-release suffix with '~' when building the Debian version.
# E.g. APP_VERSION "1.2.1-beta2" → DEB_BASE "1.2.1~beta2".
#
# - Tagged release commit (e.g. v1.2.1 or v1.2.1-beta2): use the converted base
#   version only, so the package manager sees exactly what was released.
# - Everything else (nightly, local): append the commits-since-tag count and
#   hash from git describe, e.g. "1.2.1~beta2-5-gabcdef1", so successive
#   nightlies order correctly and are traceable to their exact commit.
DEB_BASE=$(echo "$APP_VERSION" | sed 's/-/~/')
git -C "$SCRIPT_DIR" config --global --add safe.directory "$SCRIPT_DIR" 2>/dev/null || true
if git -C "$SCRIPT_DIR" describe --exact-match HEAD 2>/dev/null | grep -qE '^v[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)[0-9]*)?$'; then
    DEB_VERSION="${DEB_BASE}"
else
    describe=$(git -C "$SCRIPT_DIR" describe --tags --long --match "v[0-9]*.[0-9]*.[0-9]*" 2>/dev/null || true)
    if [ -n "$describe" ]; then
        BUILD_INFO=$(echo "$describe" | grep -oE '[0-9]+-g[0-9a-f]+$')
    else
        count=$(git -C "$SCRIPT_DIR" rev-list --count HEAD 2>/dev/null || echo "0")
        hash=$(git -C "$SCRIPT_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")
        BUILD_INFO="${count}-g${hash}"
    fi
    DEB_VERSION="${DEB_BASE}-${BUILD_INFO}"
fi

# Create directory structure
echo Create directory structure
mkdir sdrpp_debian_amd64
mkdir sdrpp_debian_amd64/DEBIAN

# Create package info
echo Create package info
echo Package: sdrpp-iak >> sdrpp_debian_amd64/DEBIAN/control
echo Version: $DEB_VERSION >> sdrpp_debian_amd64/DEBIAN/control
echo Maintainer: Vojtech Bubnik OK1IAK >> sdrpp_debian_amd64/DEBIAN/control
echo Architecture: all >> sdrpp_debian_amd64/DEBIAN/control
echo Description: Bloat-free SDR receiver software >> sdrpp_debian_amd64/DEBIAN/control
echo License: GPL-3.0-or-later >> sdrpp_debian_amd64/DEBIAN/control

# Install license file into documentation directory
mkdir -p sdrpp_debian_amd64/usr/share/doc/sdrpp-iak
cp "$SCRIPT_DIR/license" sdrpp_debian_amd64/usr/share/doc/sdrpp-iak/copyright
echo Depends: $2 >> sdrpp_debian_amd64/DEBIAN/control

# Copying files
ORIG_DIR=$PWD
cd $1
make install DESTDIR=$ORIG_DIR/sdrpp_debian_amd64
cd $ORIG_DIR

# Create package
echo Create package
dpkg-deb --build sdrpp_debian_amd64

# Cleanup
echo Cleanup
rm -rf sdrpp_debian_amd64
