#pragma once
#include <filesystem>

namespace core {
    // Absolute path of the directory containing the running executable.
    // Falls back to the current working directory if the platform query fails.
    std::filesystem::path getExecutableDirectory();
}
