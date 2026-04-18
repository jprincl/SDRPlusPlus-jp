#!/bin/sh

# Read the authoritative version from version.h
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_VERSION=$(grep -oE '"[0-9]+\.[0-9]+\.[0-9]+"' "$SCRIPT_DIR/core/src/version.h" | tr -d '"')
if [ -z "$APP_VERSION" ]; then
    echo "ERROR: could not parse version from core/src/version.h" >&2
    exit 1
fi

# Build number: git commit count (works locally and in CI)
git -C "$SCRIPT_DIR" config --global --add safe.directory "$SCRIPT_DIR" 2>/dev/null || true
BUILD_COUNT=$(git -C "$SCRIPT_DIR" rev-list --count HEAD 2>/dev/null || echo "")
if [ -n "$BUILD_COUNT" ]; then
    DEB_VERSION="${APP_VERSION}-${BUILD_COUNT}"
else
    DEB_VERSION="${APP_VERSION}"
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
