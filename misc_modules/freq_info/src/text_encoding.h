#pragma once
#include <string>

// eibispace.de's sked-*.csv (and possibly Aoki's fixed-width lists — not yet
// confirmed) are NOT UTF-8 — verified directly against the EiBi A26 file,
// where station names with diacritics (e.g. "Rádio", "Córdoba", "Valparaíso")
// are single Windows-1252/Latin-1 bytes. Fed straight to ImGui (which
// requires UTF-8), those bytes are invalid sequences and render as the
// substitution glyph ('?' or tofu) — this converts each line to proper
// UTF-8 before anything else touches it. For pure-ASCII input this is a
// complete no-op, so it's safe to apply defensively even where the source
// encoding hasn't been separately confirmed.
//
// Bytes 0x00-0x7F are ASCII and pass through unchanged (identical in
// Windows-1252 and UTF-8). Bytes 0xA0-0xFF map 1:1 to the same Unicode
// codepoint (Latin-1 Supplement) and are re-encoded as 2-byte UTF-8.
// Bytes 0x80-0x9F differ from Latin-1 in Windows-1252 (curly quotes,
// em-dash, Œ/œ, Š/š, Ž/ž, €, etc.) and use the explicit table below;
// unassigned slots in that range are passed through as-is since they
// essentially never appear in real station names.
inline std::string windows1252ToUtf8(const std::string& raw) {
    static const char32_t cp1252HighTable[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };

    std::string out;
    out.reserve(raw.size());
    for (unsigned char b : raw) {
        char32_t cp;
        if (b < 0x80) {
            out.push_back((char)b);
            continue;
        }
        else if (b < 0xA0) {
            cp = cp1252HighTable[b - 0x80];
        }
        else {
            cp = b; // Latin-1 Supplement: byte value == codepoint
        }

        if (cp < 0x80) {
            out.push_back((char)cp);
        }
        else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { return ""; }
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
