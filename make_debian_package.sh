#!/bin/sh

# Read the authoritative version from version.h
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_VERSION=$(grep -oE '"[0-9]+\.[0-9]+\.[0-9]+"' "$SCRIPT_DIR/core/src/version.h" | tr -d '"')
if [ -z "$APP_VERSION" ]; then
    echo "ERROR: could not parse version from core/src/version.h" >&2
    exit 1
fi

# Determine the Debian version string.
# - Tagged release commit (e.g. v1.2.1): use the bare version "1.2.1" so package
#   manager databases show exactly what was released.
# - Everything else (nightly, local): append the git commit count as a build number,
#   e.g. "1.2.1-347", so successive nightlies are ordered correctly by apt.
git -C "$SCRIPT_DIR" config --global --add safe.directory "$SCRIPT_DIR" 2>/dev/null || true
if git -C "$SCRIPT_DIR" describe --exact-match HEAD 2>/dev/null | grep -qE '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
    DEB_VERSION="${APP_VERSION}"
else
    BUILD_COUNT=$(git -C "$SCRIPT_DIR" rev-list --count HEAD 2>/dev/null || echo "")
    if [ -n "$BUILD_COUNT" ]; then
        DEB_VERSION="${APP_VERSION}-${BUILD_COUNT}"
    else
        DEB_VERSION="${APP_VERSION}"
    fi
fi

# Create directory structure
echo Create directory structure
mkdir sdrpp_debian_amd64
mkdir sdrpp_debian_amd64/DEBIAN

# Create package info
echo Create package info
echo Package: sdrpp-iak >> sdrpp_debian_amd64/DEBIAN/control
echo Version: $DEB_VERSION >> sdrpp_debian_amd64/DEBIAN/control
echo Maintainer: Ryzerth >> sdrpp_debian_amd64/DEBIAN/control
echo Architecture: all >> sdrpp_debian_amd64/DEBIAN/control
echo Description: Bloat-free SDR receiver software >> sdrpp_debian_amd64/DEBIAN/control
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
