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

# Prefer an existing SDK on the runner (GitHub Actions images set
# ANDROID_HOME and preinstall platforms/build-tools/ndk/cmake); only fall
# back to a fresh install under $HOME/Android when nothing's there.
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android}}"
ANDROID_HOME="$ANDROID_SDK_ROOT"
ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION"
ANDROID_NDK_TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
ANDROID_NDK_CMAKE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

# Gradle 9.4.1 matches the Android Gradle Plugin 9.2.1 we pin in
# android/build.gradle. Override GRADLE_VERSION to upgrade in lockstep with
# AGP; the wrapper jar in android/gradle/ is intentionally NOT committed
# (per android/.gitignore) — we drive builds via system Gradle on PATH
# instead so every environment runs the same pinned version. On GitHub
# Actions, the gradle/actions/setup-gradle@v6 step installs and PATH-exposes
# this version before us, so the detect-and-skip block below short-circuits.
GRADLE_VERSION="${GRADLE_VERSION:-9.4.1}"
GRADLE_INSTALL_ROOT="${GRADLE_INSTALL_ROOT:-$HOME/gradle}"
GRADLE_HOME="$GRADLE_INSTALL_ROOT/gradle-$GRADLE_VERSION"

export ANDROID_HOME
export ANDROID_SDK_ROOT
export ANDROID_NDK_ROOT
export ANDROID_NDK_TOOLCHAIN
export ANDROID_NDK_CMAKE
export GRADLE_HOME

# ---------------------------------------------------------------------------
# SDK cmdline-tools bootstrap: only download when sdkmanager isn't already on
# disk. GitHub Actions ubuntu images ship cmdline-tools preinstalled under
# $ANDROID_HOME/cmdline-tools/latest/, so this block is a no-op there.
# ---------------------------------------------------------------------------
SDKMANAGER="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
if [ ! -x "$SDKMANAGER" ]; then
    echo "Installing Android cmdline-tools to $ANDROID_SDK_ROOT/cmdline-tools/latest"
    DOWNLOAD_DIR="$(mktemp -d)"
    trap 'rm -rf "$DOWNLOAD_DIR"' EXIT

    wget -P "$DOWNLOAD_DIR" "https://dl.google.com/android/repository/$ANDROID_INSTALLER"
    7z x -y -o"$DOWNLOAD_DIR" "$DOWNLOAD_DIR/$ANDROID_INSTALLER"
    mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"
    mv "$DOWNLOAD_DIR/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
else
    echo "Reusing existing sdkmanager at $SDKMANAGER"
fi

# ---------------------------------------------------------------------------
# Per-component install: probe the filesystem for each SDK component's
# canonical marker file (a sentinel sdkmanager itself writes), and only
# invoke sdkmanager for the subset that's missing. Saves the ~30-90s startup
# overhead of sdkmanager when everything's already installed (the common
# case on a CI runner with preinstalled components matching our pins).
# ---------------------------------------------------------------------------
component_installed() {
    # Each SDK component installs a source.properties (NDK / CMake / build-
    # tools) or a package.xml (platforms) sentinel at a deterministic path
    # — sdkmanager writes them on successful install, so their presence is
    # an accurate proxy for "this version is here" without paying for a
    # sdkmanager invocation.
    local marker="$1"
    [ -f "$ANDROID_SDK_ROOT/$marker" ]
}

MISSING_COMPONENTS=()
component_installed "platforms/android-$ANDROID_SDK_PLATFORM/package.xml" \
    || MISSING_COMPONENTS+=("platforms;android-$ANDROID_SDK_PLATFORM")
component_installed "build-tools/$ANDROID_BUILD_TOOLS_VERSION/source.properties" \
    || MISSING_COMPONENTS+=("build-tools;$ANDROID_BUILD_TOOLS_VERSION")
component_installed "ndk/$ANDROID_NDK_VERSION/source.properties" \
    || MISSING_COMPONENTS+=("ndk;$ANDROID_NDK_VERSION")
component_installed "cmake/$ANDROID_CMAKE_VERSION/source.properties" \
    || MISSING_COMPONENTS+=("cmake;$ANDROID_CMAKE_VERSION")

if [ ${#MISSING_COMPONENTS[@]} -gt 0 ]; then
    echo "Installing missing SDK components: ${MISSING_COMPONENTS[*]}"
    # sdkmanager closes stdin after accepting the licenses, which makes yes
    # exit with SIGPIPE. The package installation below still reports real
    # failures.
    yes | "$SDKMANAGER" --sdk_root="$ANDROID_SDK_ROOT" --licenses >/dev/null || true
    "$SDKMANAGER" --sdk_root="$ANDROID_SDK_ROOT" --install "${MISSING_COMPONENTS[@]}"
else
    echo "All required SDK components already installed; skipping sdkmanager."
fi

# ---------------------------------------------------------------------------
# Gradle bootstrap: skip when `gradle` is already on PATH at the right
# version (gradle/actions/setup-gradle@v6 in CI, manual install on dev
# machines) or when we've previously extracted the pinned version under
# $GRADLE_HOME. Otherwise fetch the official distribution zip.
# ---------------------------------------------------------------------------
gradle_on_path_matches() {
    command -v gradle >/dev/null 2>&1 || return 1
    # `gradle --version` writes a multi-line block; the "Gradle X.Y.Z" line
    # is what we want. Compare against the pinned version exactly.
    local v
    v="$(gradle --version 2>/dev/null | awk '/^Gradle /{print $2; exit}')"
    [ "$v" = "$GRADLE_VERSION" ]
}

if [ -x "$GRADLE_HOME/bin/gradle" ]; then
    echo "Reusing existing Gradle install at $GRADLE_HOME"
elif gradle_on_path_matches; then
    GRADLE_PATH_DIR="$(dirname "$(command -v gradle)")"
    GRADLE_HOME="$(dirname "$GRADLE_PATH_DIR")"
    echo "Reusing Gradle $GRADLE_VERSION already on PATH at $GRADLE_HOME"
else
    echo "Installing Gradle $GRADLE_VERSION to $GRADLE_INSTALL_ROOT"
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
    # Only add our Gradle bin if it isn't already present (setup-gradle@v6
    # injects its own bin into PATH earlier in the workflow; pushing ours
    # too would be redundant but harmless).
    if ! command -v gradle >/dev/null 2>&1; then
        echo "$GRADLE_HOME/bin" >> "$GITHUB_PATH"
    fi
fi
