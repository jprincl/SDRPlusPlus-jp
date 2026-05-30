#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_GRADLE="$SCRIPT_DIR/android/app/build.gradle"

read_gradle_value() {
    local pattern="$1"
    local value

    value="$(sed -nE "s/${pattern}/\\1/p" "$APP_GRADLE")"
    if [ -z "$value" ]; then
        echo "Unable to read Android build setting from $APP_GRADLE" >&2
        exit 1
    fi
    echo "$value"
}

# Keep the SDK components aligned with android/app/build.gradle.
ANDROID_SDK_PLATFORM="${ANDROID_SDK_PLATFORM:-$(read_gradle_value '^[[:space:]]*compileSdk[[:space:]]+([0-9]+).*')}"
ANDROID_BUILD_TOOLS_VERSION="${ANDROID_BUILD_TOOLS_VERSION:-${ANDROID_SDK_PLATFORM}.0.0}"
ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-$(read_gradle_value '^[[:space:]]*ndkVersion[[:space:]]+"([^"]+)".*')}"
ANDROID_CMAKE_VERSION="${ANDROID_CMAKE_VERSION:-$(read_gradle_value '^[[:space:]]*version[[:space:]]*=[[:space:]]*"([^"]+)".*')}"
ANDROID_INSTALLER="${ANDROID_INSTALLER:-commandlinetools-linux-14742923_latest.zip}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android}}"
ANDROID_HOME="$ANDROID_SDK_ROOT"
ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION"
ANDROID_NDK_TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
ANDROID_NDK_CMAKE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

export ANDROID_HOME
export ANDROID_SDK_ROOT
export ANDROID_NDK_ROOT
export ANDROID_NDK_TOOLCHAIN
export ANDROID_NDK_CMAKE

SDKMANAGER="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
if [ ! -x "$SDKMANAGER" ]; then
    DOWNLOAD_DIR="$(mktemp -d)"
    trap 'rm -rf "$DOWNLOAD_DIR"' EXIT

    wget -P "$DOWNLOAD_DIR" "https://dl.google.com/android/repository/$ANDROID_INSTALLER"
    7z x -y -o"$DOWNLOAD_DIR" "$DOWNLOAD_DIR/$ANDROID_INSTALLER"
    mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"
    mv "$DOWNLOAD_DIR/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
fi

# sdkmanager closes stdin after accepting the licenses, which makes yes exit
# with SIGPIPE. The package installation below still reports real failures.
yes | "$SDKMANAGER" --sdk_root="$ANDROID_SDK_ROOT" --licenses >/dev/null || true
"$SDKMANAGER" --sdk_root="$ANDROID_SDK_ROOT" --install \
    "platforms;android-$ANDROID_SDK_PLATFORM" \
    "build-tools;$ANDROID_BUILD_TOOLS_VERSION" \
    "ndk;$ANDROID_NDK_VERSION" \
    "cmake;$ANDROID_CMAKE_VERSION"

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "ANDROID_HOME=$ANDROID_HOME"
        echo "ANDROID_SDK_ROOT=$ANDROID_SDK_ROOT"
        echo "ANDROID_NDK_ROOT=$ANDROID_NDK_ROOT"
        echo "ANDROID_NDK_TOOLCHAIN=$ANDROID_NDK_TOOLCHAIN"
        echo "ANDROID_NDK_CMAKE=$ANDROID_NDK_CMAKE"
    } >> "$GITHUB_ENV"
fi

if [ -n "${GITHUB_PATH:-}" ]; then
    echo "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin" >> "$GITHUB_PATH"
    echo "$ANDROID_SDK_ROOT/platform-tools" >> "$GITHUB_PATH"
fi
