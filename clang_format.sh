#!/bin/bash
# Apply (or, with --check, verify) our clang-format style across all first-party source files.
# Vendored / third-party code that isn't formatted to our style is excluded via the pathspecs below.
#
# Usage:
#   ./clang_format.sh          Reformat all first-party source files in place.
#   ./clang_format.sh --check  Verify formatting without modifying files (non-zero exit on diff).
#
# The file list is emitted NUL-separated (git ls-files -z) so paths with spaces are safe.
clang_format_ls() {
    git ls-files -z '*.h' '*.hpp' '*.c' '*.cpp' \
        ':(exclude)core/libcorrect' \
        ':(exclude)core/std_replacement' \
        ':(exclude)core/src/imgui' \
        ':(exclude)misc_modules/discord_integration/discord-rpc' \
        ':(exclude)source_modules/sddc_source/libsddc' \
        ':(exclude)core/src/json.hpp' \
        ':(exclude)core/src/gui/file_dialogs.h'
}

case "$1" in
    --check|check)
        clang_format_ls | xargs -0 clang-format --style=file --dry-run -Werror
        ;;
    "")
        clang_format_ls | xargs -0 -I{} sh -c 'echo "Formatting: $1"; clang-format --style=file -i "$1"' _ {}
        echo "Lines: $(clang_format_ls | xargs -0 cat | wc -l)"
        echo "File Count: $(clang_format_ls | grep -zc .)"
        ;;
    *)
        echo "Usage: $0 [--check]" >&2
        exit 2
        ;;
esac
