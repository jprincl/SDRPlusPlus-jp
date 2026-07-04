#!/bin/bash
# Vendored / third-party code that isn't formatted to our style is excluded via pathspecs below.
git ls-files -z '*.h' '*.hpp' '*.c' '*.cpp' \
    ':(exclude)core/libcorrect' \
    ':(exclude)core/std_replacement' \
    ':(exclude)core/src/imgui' \
    ':(exclude)misc_modules/discord_integration/discord-rpc' \
    ':(exclude)source_modules/sddc_source/libsddc' \
    ':(exclude)core/src/json.hpp' \
    ':(exclude)core/src/gui/file_dialogs.h' \
    | xargs -0 clang-format --style=file --dry-run -Werror
