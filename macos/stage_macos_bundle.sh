#!/bin/sh
set -e

BUNDLE=$1
MAIN_EXEC=$2
CORE_DYLIB=$3
shift 3

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

. "$SCRIPT_DIR/bundle_utils.sh"

rm -rf "$BUNDLE"
bundle_create_struct "$BUNDLE"

cp -R "$REPO_ROOT/root/res"/* "$BUNDLE/Contents/Resources/"
bundle_create_icns "$REPO_ROOT/root/res/icons/sdrpp.macos.png" "$BUNDLE/Contents/Resources/sdrpp"

APP_VERSION=$(grep -oE '"[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)[0-9]*)?"' "$REPO_ROOT/core/src/version.h" | tr -d '"')
if [ -z "$APP_VERSION" ]; then
    echo "ERROR: could not parse version from core/src/version.h" >&2
    exit 1
fi

bundle_create_plist sdrpp-iak "SDR++ iak" org.ok1iak.sdrpp "$APP_VERSION" sdri sdrpp-iak sdrpp "$BUNDLE/Contents/Info.plist"

bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/MacOS" "$MAIN_EXEC"
bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Frameworks" "$CORE_DYLIB"

for plugin_path in "$@"; do
    bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$plugin_path"
done

# SDRplay's closed-source API .so is blacklisted in bundle_utils.sh
# (versioned names like libsdrplay_api.so.3.14 / .so.3.15 would otherwise
# trip bundle_install_binary's recursive dep walk against the plugin), so
# explicit-bundle the SONAME variant from where the deps build deposits
# it. deps/+sdrplay/install_macos.cmake guarantees this path exists when
# OPT_BUILD_SDRPLAY_SOURCE was on at deps-build time.
DEPS_LIB_DIR=$(ls -d "$REPO_ROOT"/deps/build-*/destdir/usr/local/lib 2>/dev/null | head -n1)
SDRPLAY_LIB="$DEPS_LIB_DIR/libsdrplay_api.so.3"
if [ -f "$SDRPLAY_LIB" ]; then
    bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Frameworks" "$SDRPLAY_LIB"
fi

bundle_sign "$BUNDLE"