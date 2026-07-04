#!/bin/bash
# Vendored / third-party code that isn't formatted to our style is excluded via pathspecs below.

TOTAL_LINES=0
FILE_COUNT=0

CODE_FILES=$(git ls-files -z '*.h' '*.hpp' '*.c' '*.cpp' \
    ':(exclude)core/libcorrect' \
    ':(exclude)core/std_replacement' \
    ':(exclude)core/src/imgui' \
    ':(exclude)misc_modules/discord_integration/discord-rpc' \
    ':(exclude)source_modules/sddc_source/libsddc' \
    ':(exclude)core/src/json.hpp' \
    ':(exclude)core/src/gui/file_dialogs.h' \
    | tr '\0' '\n')
while read -r CPP_FILE_PATH; do
    [ -z "$CPP_FILE_PATH" ] && continue
    echo Formatting $CPP_FILE_PATH
    clang-format --style=file -i "$CPP_FILE_PATH"

    TOTAL_LINES=$(( $TOTAL_LINES + $(wc -l < "$CPP_FILE_PATH") ))
    FILE_COUNT=$(( $FILE_COUNT + 1 ))
done <<< "$CODE_FILES"

echo Lines: $TOTAL_LINES
echo File Count: $FILE_COUNT
