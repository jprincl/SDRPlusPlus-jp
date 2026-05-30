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

# Gradle 9.4.1 matches the Android Gradle Plugin 9.2.1 we pin in
# android/build.gradle. Override GRADLE_VERSION to upgrade in lockstep with
# AGP; the wrapper jar in android/gradle/ is intentionally NOT committed
# (per android/.gitignore) — we drive builds via system Gradle on PATH
# instead so every environment runs the same pinned version.
GRADLE_VERSION="${GRADLE_VERSION:-9.4.1}"
GRADLE_INSTALL_ROOT="${GRADLE_INSTALL_ROOT:-$HOME/gradle}"
GRADLE_HOME="$GRADLE_INSTALL_ROOT/gradle-$GRADLE_VERSION"

export ANDROID_HOME
export ANDROID_SDK_ROOT
export ANDROID_NDK_ROOT
export ANDROID_NDK_TOOLCHAIN
export ANDROID_NDK_CMAKE
export GRADLE_HOME

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

# Install Gradle into $GRADLE_INSTALL_ROOT and put its bin/ on PATH. Skipped
# if the pinned version is already there (re-runs on local dev machines).
if [ ! -x "$GRADLE_HOME/bin/gradle" ]; then
    GRADLE_DOWNLOAD_DIR="$(mktemp -d)"
    GRADLE_ZIP="gradle-$GRADLE_VERSION-bin.zip"
    wget -P "$GRADLE_DOWNLOAD_DIR" "https://services.gradle.org/distributions/$GRADLE_ZIP"
    mkdir -p "$GRADLE_INSTALL_ROOT"
    7z x -y -o"$GRADLE_INSTALL_ROOT" "$GRADLE_DOWNLOAD_DIR/$GRADLE_ZIP" >/dev/null
    rm -rf "$GRADLE_DOWNLOAD_DIR"
fi

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "ANDROID_HOME=$ANDROID_HOME"
        echo "ANDROID_SDK_ROOT=$ANDROID_SDK_ROOT"
        echo "ANDROID_NDK_ROOT=$ANDROID_NDK_ROOT"
        echo "ANDROID_NDK_TOOLCHAIN=$ANDROID_NDK_TOOLCHAIN"
        echo "ANDROID_NDK_CMAKE=$ANDROID_NDK_CMAKE"
        echo "GRADLE_HOME=$GRADLE_HOME"
    } >> "$GITHUB_ENV"
fi

if [ -n "${GITHUB_PATH:-}" ]; then
    echo "$ANDROID_SDK_ROOT/cmdline-tools/latest/bin" >> "$GITHUB_PATH"
    echo "$ANDROID_SDK_ROOT/platform-tools" >> "$GITHUB_PATH"
    echo "$GRADLE_HOME/bin" >> "$GITHUB_PATH"
fi
