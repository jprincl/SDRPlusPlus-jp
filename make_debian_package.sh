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
mkdir sdrpp_debian_pkg
mkdir sdrpp_debian_pkg/DEBIAN

# Install license file into documentation directory
mkdir -p sdrpp_debian_pkg/usr/share/doc/sdrpp-iak
cp "$SCRIPT_DIR/license" sdrpp_debian_pkg/usr/share/doc/sdrpp-iak/copyright

# Stage build artifacts into the package tree so runtime deps can be derived
# from the actual ELF NEEDED entries below.
ORIG_DIR=$PWD
cd $1
make install DESTDIR=$ORIG_DIR/sdrpp_debian_pkg
cd $ORIG_DIR

STAGE="$ORIG_DIR/sdrpp_debian_pkg"

# SDRplay's API is a closed-source binary blob: no Debian package ships it,
# and our deps build lays it down under deps/build-<preset>-<config>/destdir
# rather than the main install tree. Without it in $STAGE, dpkg-shlibdeps
# can't resolve sdrplay_source.so's NEEDED entry (libsdrplay_api.so.3) and
# the resulting .deb would install a plugin the dynamic linker can't load.
# Ship the .so + its SONAME / linker-name symlinks alongside the rest of
# our libraries in /usr/lib so both build-time shlibdeps and runtime dlopen
# resolve through standard paths. AppImage builds don't need this — linux
# deploy bundles the library into the AppDir automatically.
SDRPLAY_LIB_REAL=$(find "$SCRIPT_DIR/deps" -type f -name 'libsdrplay_api.so.*.*' 2>/dev/null | head -n1)
if [ -n "$SDRPLAY_LIB_REAL" ]; then
    SDRPLAY_LIB_DIR=$(dirname "$SDRPLAY_LIB_REAL")
    mkdir -p "$STAGE/usr/lib"
    # cp -P preserves the SONAME and linker-name symlinks created by
    # deps/+sdrplay/install_linux.cmake.
    cp -P "$SDRPLAY_LIB_DIR"/libsdrplay_api.so* "$STAGE/usr/lib/"
fi

# Resolve runtime deps via dpkg-shlibdeps. This adapts to the build host's
# distribution conventions (libcurl4 vs libcurl4t64, libvolk2.X vs libvolk3,
# the libssl ABI of the day) instead of relying on a hand-maintained list of
# -dev packages. Libraries we ship under the staging tree are satisfied
# locally via -l so they don't bleed into the dependency list.
echo Compute runtime dependencies

FILES=""
for f in $(find "$STAGE" -type f \( -name '*.so' -o -name '*.so.*' -o -path '*/bin/*' \)); do
    case $(file -b "$f") in
        "ELF "*) FILES="$FILES $f" ;;
    esac
done

# dpkg-shlibdeps insists on a debian/ source tree; build a throwaway one.
SHLIB_TMP=$(mktemp -d)
mkdir -p "$SHLIB_TMP/debian"
cat > "$SHLIB_TMP/debian/control" <<EOF
Source: sdrpp-iak
Maintainer: noreply@invalid

Package: sdrpp-iak
Architecture: any
Description: stub
EOF

(
    cd "$SHLIB_TMP"
    dpkg-shlibdeps --ignore-missing-info \
        -l"$STAGE/usr/lib" \
        -l"$STAGE/usr/lib/sdrpp-iak/plugins" \
        $FILES
)
SHLIBS_DEPS=$(sed -n 's/^shlibs:Depends=//p' "$SHLIB_TMP/debian/substvars")
rm -rf "$SHLIB_TMP"

if [ -z "$SHLIBS_DEPS" ]; then
    echo "ERROR: dpkg-shlibdeps produced no Depends" >&2
    exit 1
fi

# Create package info
echo Create package info
DEB_ARCH=$(dpkg --print-architecture)
{
    echo "Package: sdrpp-iak"
    echo "Version: $DEB_VERSION"
    echo "Maintainer: Vojtech Bubnik OK1IAK"
    echo "Architecture: $DEB_ARCH"
    echo "Description: Bloat-free SDR receiver software"
    echo "License: GPL-3.0-or-later"
    echo "Depends: $SHLIBS_DEPS"
} > sdrpp_debian_pkg/DEBIAN/control

# Create package
echo Create package
dpkg-deb --build sdrpp_debian_pkg

# Cleanup
echo Cleanup
rm -rf sdrpp_debian_pkg
