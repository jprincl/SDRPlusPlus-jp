#include "executable_path.h"
#include <utils/flog.h>

#ifdef _WIN32
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <string>
#else
#include <system_error>
#endif

namespace core {
    static std::filesystem::path queryExecutablePath() {
#if defined(_WIN32)
        std::wstring buf(MAX_PATH, L'\0');
        for (;;) {
            DWORD len = GetModuleFileNameW(NULL, buf.data(), (DWORD)buf.size());
            if (len == 0) { return {}; }
            // A return value equal to the buffer size means truncation
            if (len < buf.size()) {
                buf.resize(len);
                return std::filesystem::path(buf);
            }
            buf.resize(buf.size() * 2);
        }
#elif defined(__APPLE__)
        uint32_t len = 0;
        _NSGetExecutablePath(NULL, &len);
        std::string buf(len, '\0');
        if (_NSGetExecutablePath(buf.data(), &len) != 0) { return {}; }
        return std::filesystem::path(buf.c_str());
#elif defined(__linux__)
        std::error_code ec;
        auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec) { return {}; }
        return path;
#else
        return {};
#endif
    }

    std::filesystem::path getExecutableDirectory() {
        static const std::filesystem::path dir = []() {
            auto execPath = queryExecutablePath();
            if (!execPath.empty()) {
                // Resolve symlinks (e.g. the app launched through a symlink on
                // the PATH) so relative paths resolve next to the real binary.
                std::error_code ec;
                auto canonPath = std::filesystem::canonical(execPath, ec);
                return (ec ? execPath : canonPath).parent_path();
            }
            flog::warn("Could not determine the executable path, falling back to the working directory");
            return std::filesystem::current_path();
        }();
        return dir;
    }
}
