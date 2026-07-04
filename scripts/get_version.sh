#!/bin/sh
# Single source of truth for reading the app version from core/src/version.h
# and deriving the git-based build-info suffix (commits since the matching
# vX.Y.Z tag, plus short hash). Used by the CI prepare job
# (.github/workflows/build_all.yml), the .deb packaging block in
# CMakeLists.txt, and macos/stage_macos_bundle.sh so they never compute a
# mismatched version for the same commit.
#
# Usage: get_version.sh [repo_root]
# Prints shell-assignment lines on stdout, meant to be eval'd:
#   VERSION=1.2.3[-alpha|beta|rc[N]]
#   VERSION_FULL=<VERSION, plus "+<count>-g<shorthash>" when HEAD is not
#                 exactly the matching release tag>
set -e

REPO_ROOT="${1:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}"

VERSION=$(grep -oE '"[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)[0-9]*)?"' "$REPO_ROOT/core/src/version.h" | tr -d '"')
if [ -z "$VERSION" ]; then
    echo "ERROR: could not parse version from core/src/version.h" >&2
    exit 1
fi

# Pass safe.directory per-invocation (git -c) rather than `config --global`,
# which would append a duplicate line to the caller's ~/.gitconfig on every
# run (this is invoked on every Linux cmake configure via the CPack block).
git_repo() {
    git -c safe.directory="$REPO_ROOT" -C "$REPO_ROOT" "$@"
}

DESCRIBE=$(git_repo describe --tags --long --match "v[0-9]*.[0-9]*.[0-9]*" 2>/dev/null || true)
if [ -n "$DESCRIBE" ]; then
    BUILD_INFO=$(echo "$DESCRIBE" | grep -oE '[0-9]+-g[0-9a-f]+$')
    BUILD_COUNT=$(echo "$BUILD_INFO" | grep -oE '^[0-9]+')
else
    BUILD_COUNT=$(git_repo rev-list --count HEAD 2>/dev/null || echo "0")
    HASH=$(git_repo rev-parse --short HEAD 2>/dev/null || echo "unknown")
    BUILD_INFO="${BUILD_COUNT}-g${HASH}"
fi

if [ "$BUILD_COUNT" = "0" ]; then
    VERSION_FULL="$VERSION"
else
    VERSION_FULL="${VERSION}+${BUILD_INFO}"
fi

echo "VERSION=$VERSION"
echo "VERSION_FULL=$VERSION_FULL"
