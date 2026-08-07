#pragma once
#include <stdint.h>

// ITA2 / CCITT-2 ("Baudot") decoding tables used by amateur RTTY.
// 5-bit codes, two shift sets: LTRS (letters) and FIGS (figures).
// USOS (Unshift On Space) returns to the LTRS set after a space when enabled.
//
// Special codes:
//   0x1F (11111) -> LTRS shift
//   0x1B (11011) -> FIGS shift
//   0x04 (00100) -> SPACE
//   0x02 (00010) -> LF
//   0x08 (01000) -> CR
//   0x00 (00000) -> NUL / blank

namespace rtty {

    static const char BAUDOT_LTRS[32] = {
        '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U',
        '\r', 'D', 'R',  'J', 'N', 'F', 'C', 'K',
        'T',  'Z', 'L',  'W', 'H', 'Y', 'P', 'Q',
        'O',  'B', 'G',  '\1', 'M', 'X', 'V', '\1'  // '\1' = shift markers, handled separately
    };

    // US-TTY figures layout (the common amateur arrangement).
    static const char BAUDOT_FIGS[32] = {
        '\0', '3', '\n', '-', ' ', '\'', '8', '7',
        '\r', '$', '4',  '\a', ',', '!', ':', '(',
        '5',  '"', ')',  '2', '#', '6', '0', '1',
        '9',  '?', '&',  '\1', '.', '/', ';', '\1'
    };

    static const uint8_t BAUDOT_LTRS_SHIFT = 0x1F;
    static const uint8_t BAUDOT_FIGS_SHIFT = 0x1B;
    static const uint8_t BAUDOT_SPACE      = 0x04;

    // Decode one 5-bit code given the current shift state.
    // Returns the decoded character, or 0 if the code only changes shift state.
    // 'figs' is updated in place; 'usos' applies the unshift-on-space behaviour.
    static inline char baudotDecode(uint8_t code, bool& figs, bool usos) {
        code &= 0x1F;
        if (code == BAUDOT_LTRS_SHIFT) { figs = false; return 0; }
        if (code == BAUDOT_FIGS_SHIFT) { figs = true;  return 0; }
        char c = figs ? BAUDOT_FIGS[code] : BAUDOT_LTRS[code];
        if (code == BAUDOT_SPACE && usos) { figs = false; }
        if (c == '\1') { return 0; } // safety: never emit raw shift markers
        return c;
    }

}
