#!/bin/sh
# Single source of truth for reading the app version from core/src/version.h
# and deriving the git-based build-info suffix (commits since the matching
# vX.Y.Z tag, plus short hash). Used by both the CI get_version job
# (.github/workflows/build_all.yml) and the .deb packaging block in
# CMakeLists.txt so the two never compute a mismatched count/hash for the
# same commit.
#
# Usage: get_version.sh [repo_root]
# Prints shell-assignment lines on stdout, meant to be eval'd:
#   VERSION=1.2.3[-alpha|beta|rc[N]]
#   BUILD_COUNT=<commits since the matching tag, or since repo root>
#   BUILD_INFO=<count>-g<shorthash>
set -e

REPO_ROOT="${1:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}"

VERSION=$(grep -oE '"[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)[0-9]*)?"' "$REPO_ROOT/core/src/version.h" | tr -d '"')
if [ -z "$VERSION" ]; then
    echo "ERROR: could not parse version from core/src/version.h" >&2
    exit 1
fi

git -C "$REPO_ROOT" config --global --add safe.directory "$REPO_ROOT" 2>/dev/null || true

DESCRIBE=$(git -C "$REPO_ROOT" describe --tags --long --match "v[0-9]*.[0-9]*.[0-9]*" 2>/dev/null || true)
if [ -n "$DESCRIBE" ]; then
    BUILD_INFO=$(echo "$DESCRIBE" | grep -oE '[0-9]+-g[0-9a-f]+$')
    BUILD_COUNT=$(echo "$BUILD_INFO" | grep -oE '^[0-9]+')
else
    BUILD_COUNT=$(git -C "$REPO_ROOT" rev-list --count HEAD 2>/dev/null || echo "0")
    HASH=$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo "unknown")
    BUILD_INFO="${BUILD_COUNT}-g${HASH}"
fi

echo "VERSION=$VERSION"
echo "BUILD_COUNT=$BUILD_COUNT"
echo "BUILD_INFO=$BUILD_INFO"
