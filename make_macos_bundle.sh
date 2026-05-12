#!/bin/sh
set -e

BUILD_DIR=$1
BUNDLE=$2
BUILD_CONFIG=${3:-Release}

get_cmake_cache_value() {
    grep -E "^$2:[^=]*=" "$1" | head -n 1 | sed 's/^[^=]*=//'
}

CACHE_PATH="$BUILD_DIR/CMakeCache.txt"
STAGE_ROOT=$(get_cmake_cache_value "$CACHE_PATH" SDRPP_MACOS_BUNDLE_STAGE_ROOT)
if [ -z "$STAGE_ROOT" ]; then
    STAGE_ROOT="$BUILD_DIR/stage/macos-bundle"
fi

STAGED_BUNDLE="$STAGE_ROOT/$BUILD_CONFIG/SDR++-iak.app"

cmake --build "$BUILD_DIR" --config "$BUILD_CONFIG" --target stage_macos_bundle

if [ ! -d "$STAGED_BUNDLE" ]; then
    echo "ERROR: staged macOS bundle not found at $STAGED_BUNDLE" >&2
    exit 1
fi

rm -rf "$BUNDLE"
cp -R "$STAGED_BUNDLE" "$BUNDLE"
