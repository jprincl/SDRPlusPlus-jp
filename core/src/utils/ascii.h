#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace ascii {

    inline bool equalsIgnoreCase(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) { return false; }
        for (size_t i = 0; i < a.size(); i++) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    inline bool startsWithIgnoreCase(const std::string& value, const std::string& prefix) {
        if (value.size() < prefix.size()) { return false; }
        return equalsIgnoreCase(value.substr(0, prefix.size()), prefix);
    }

    inline std::string trim(const std::string& value) {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) { return {}; }
        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    // Strict-weak ordering for case-insensitive ASCII strings; suitable as a
    // std::map / std::set comparator.
    struct CaseInsensitiveLess {
        bool operator()(const std::string& a, const std::string& b) const {
            for (size_t i = 0, n = std::min(a.size(), b.size()); i < n; i++) {
                const int ca = std::tolower(static_cast<unsigned char>(a[i]));
                const int cb = std::tolower(static_cast<unsigned char>(b[i]));
                if (ca != cb) { return ca < cb; }
            }
            return a.size() < b.size();
        }
    };

}
