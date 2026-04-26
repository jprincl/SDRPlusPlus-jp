#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace base64 {

    inline std::string encode(const uint8_t* data, size_t len) {
        static const char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        size_t i = 0;
        for (; i + 3 <= len; i += 3) {
            uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
            out.push_back(alphabet[(v >> 18) & 0x3F]);
            out.push_back(alphabet[(v >> 12) & 0x3F]);
            out.push_back(alphabet[(v >> 6) & 0x3F]);
            out.push_back(alphabet[v & 0x3F]);
        }
        if (i < len) {
            uint32_t v = (uint32_t)data[i] << 16;
            if (i + 1 < len) { v |= (uint32_t)data[i + 1] << 8; }
            out.push_back(alphabet[(v >> 18) & 0x3F]);
            out.push_back(alphabet[(v >> 12) & 0x3F]);
            out.push_back(i + 1 < len ? alphabet[(v >> 6) & 0x3F] : '=');
            out.push_back('=');
        }
        return out;
    }

}
