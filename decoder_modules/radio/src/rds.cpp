#include "rds.h"
#include <string.h>
#include <map>
#include <algorithm>

#include <utils/flog.h>

namespace rds {
    std::map<uint16_t, BlockType> SYNDROMES = {
        { 0b1111011000, BLOCK_TYPE_A  },
        { 0b1111010100, BLOCK_TYPE_B  },
        { 0b1001011100, BLOCK_TYPE_C  },
        { 0b1111001100, BLOCK_TYPE_CP },
        { 0b1001011000, BLOCK_TYPE_D  }
    };

    std::map<BlockType, uint16_t> OFFSETS = {
        { BLOCK_TYPE_A,  0b0011111100 },
        { BLOCK_TYPE_B,  0b0110011000 },
        { BLOCK_TYPE_C,  0b0101101000 },
        { BLOCK_TYPE_CP, 0b1101010000 },
        { BLOCK_TYPE_D,  0b0110110100 }
    };

    const BlockType nextType[_BLOCK_TYPE_COUNT] = {
        BLOCK_TYPE_B, // From BLOCK_TYPE_A,
        BLOCK_TYPE_C, // From BLOCK_TYPE_B,
        BLOCK_TYPE_D, // From BLOCK_TYPE_C,
        BLOCK_TYPE_D, // From BLOCK_TYPE_CP,
        BLOCK_TYPE_A  // From BLOCK_TYPE_D,
    };

    std::map<uint16_t, const char*> THREE_LETTER_CALLS = {
        { 0x99A5, "KBW" },
        { 0x99A6, "KCY" },
        { 0x9990, "KDB" },
        { 0x99A7, "KDF" },
        { 0x9950, "KEX" },
        { 0x9951, "KFH" },
        { 0x9952, "KFI" },
        { 0x9953, "KGA" },
        { 0x9991, "KGB" },
        { 0x9954, "KGO" },
        { 0x9955, "KGU" },
        { 0x9956, "KGW" },
        { 0x9957, "KGY" },
        { 0x99AA, "KHQ" },
        { 0x9958, "KID" },
        { 0x9959, "KIT" },
        { 0x995A, "KJR" },
        { 0x995B, "KLO" },
        { 0x995C, "KLZ" },
        { 0x995D, "KMA" },
        { 0x995E, "KMJ" },
        { 0x995F, "KNX" },
        { 0x9960, "KOA" },
        { 0x99AB, "KOB" },
        { 0x9992, "KOY" },
        { 0x9993, "KPQ" },
        { 0x9964, "KQV" },
        { 0x9994, "KSD" },
        { 0x9965, "KSL" },
        { 0x9966, "KUJ" },
        { 0x9995, "KUT" },
        { 0x9967, "KVI" },
        { 0x9968, "KWG" },
        { 0x9996, "KXL" },
        { 0x9997, "KXO" },
        { 0x996B, "KYW" },
        { 0x9999, "WBT" },
        { 0x996D, "WBZ" },
        { 0x996E, "WDZ" },
        { 0x996F, "WEW" },
        { 0x999A, "WGH" },
        { 0x9971, "WGL" },
        { 0x9972, "WGN" },
        { 0x9973, "WGR" },
        { 0x999B, "WGY" },
        { 0x9975, "WHA" },
        { 0x9976, "WHB" },
        { 0x9977, "WHK" },
        { 0x9978, "WHO" },
        { 0x999C, "WHP" },
        { 0x999D, "WIL" },
        { 0x997A, "WIP" },
        { 0x99B3, "WIS" },
        { 0x997B, "WJR" },
        { 0x99B4, "WJW" },
        { 0x99B5, "WJZ" },
        { 0x997C, "WKY" },
        { 0x997D, "WLS" },
        { 0x997E, "WLW" },
        { 0x999E, "WMC" },
        { 0x999F, "WMT" },
        { 0x9981, "WOC" },
        { 0x99A0, "WOI" },
        { 0x9983, "WOL" },
        { 0x9984, "WOR" },
        { 0x99A1, "WOW" },
        { 0x99B9, "WRC" },
        { 0x99A2, "WRR" },
        { 0x99A3, "WSB" },
        { 0x99A4, "WSM" },
        { 0x9988, "WWJ" },
        { 0x9989, "WWL" }
    };

    std::map<uint16_t, const char*> NAT_LOC_LINKED_STATIONS = {
        { 0xB01, "NPR-1" },
        { 0xB02, "CBC - Radio One" },
        { 0xB03, "CBC - Radio Two" },
        { 0xB04, "Radio-Canada - Première Chaîne" },
        { 0xB05, "Radio-Canada - Espace Musique" },
        { 0xB06, "CBC" },
        { 0xB07, "CBC" },
        { 0xB08, "CBC" },
        { 0xB09, "CBC" },
        { 0xB0A, "NPR-2" },
        { 0xB0B, "NPR-3" },
        { 0xB0C, "NPR-4" },
        { 0xB0D, "NPR-5" },
        { 0xB0E, "NPR-6" }
    };

    //                           9876543210
    const uint16_t LFSR_POLY = 0b0110111001;
    const uint16_t IN_POLY   = 0b1100011011;

    const int BLOCK_LEN = 26;
    const int DATA_LEN = 16;
    const int POLY_LEN = 10;

    // RDS/EBU G0 default character repertoire (IEC 62106, Annex E), as UTF-8.
    // Control positions (0x00-0x1F) and undefined slots render as space; the
    // five glyphs Roboto-Medium lacks (arrows, box-draw bar) use ASCII fallbacks.
    // Written as \xNN byte escapes so the table is charset-independent (no MSVC
    // /utf-8 dependency).
    static const char* const EBU_REPERTOIRE[256] = {
        /* 0x00 */ " ", " ", " ", " ", " ", " ", " ", " ",
        /* 0x08 */ " ", " ", " ", " ", " ", " ", " ", " ",
        /* 0x10 */ " ", " ", " ", " ", " ", " ", " ", " ",
        /* 0x18 */ " ", " ", " ", " ", " ", " ", " ", " ",
        /* 0x20 */ " ", "!", "\x22", "#", "\xc2\xa4", "%", "&", "'",
        /* 0x28 */ "(", ")", "*", "+", ",", "-", ".", "/",
        /* 0x30 */ "0", "1", "2", "3", "4", "5", "6", "7",
        /* 0x38 */ "8", "9", ":", ";", "<", "=", ">", "?",
        /* 0x40 */ "@", "A", "B", "C", "D", "E", "F", "G",
        /* 0x48 */ "H", "I", "J", "K", "L", "M", "N", "O",
        /* 0x50 */ "P", "Q", "R", "S", "T", "U", "V", "W",
        /* 0x58 */ "X", "Y", "Z", "[", "\x5c", "]", "\xe2\x80\x95", "_",
        /* 0x60 */ "|", "a", "b", "c", "d", "e", "f", "g",
        /* 0x68 */ "h", "i", "j", "k", "l", "m", "n", "o",
        /* 0x70 */ "p", "q", "r", "s", "t", "u", "v", "w",
        /* 0x78 */ "x", "y", "z", "{", "|", "}", "\xc2\xaf", "-",
        /* 0x80 */ "\xc3\xa1", "\xc3\xa0", "\xc3\xa9", "\xc3\xa8", "\xc3\xad", "\xc3\xac", "\xc3\xb3", "\xc3\xb2",
        /* 0x88 */ "\xc3\xba", "\xc3\xb9", "\xc3\x91", "\xc3\x87", "\xc5\x9e", "\xc3\x9f", "\xc2\xa1", "\xc4\xb2",
        /* 0x90 */ "\xc3\xa2", "\xc3\xa4", "\xc3\xaa", "\xc3\xab", "\xc3\xae", "\xc3\xaf", "\xc3\xb4", "\xc3\xb6",
        /* 0x98 */ "\xc3\xbb", "\xc3\xbc", "\xc3\xb1", "\xc3\xa7", "\xc5\x9f", "\xc4\x9f", "\xc4\xb1", "\xc4\xb3",
        /* 0xA0 */ "\xc2\xaa", "\xce\xb1", "\xc2\xa9", "\xe2\x80\xb0", "\xc4\x9e", "\xc4\x9b", "\xc5\x88", "\xc5\x91",
        /* 0xA8 */ "\xcf\x80", "\xe2\x82\xac", "\xc2\xa3", "$", "<", "^", ">", "v",
        /* 0xB0 */ "\xc2\xba", "\xc2\xb9", "\xc2\xb2", "\xc2\xb3", "\xc2\xb1", "\xc4\xb0", "\xc5\x84", "\xc5\xb1",
        /* 0xB8 */ "\xc2\xb5", "\xc2\xbf", "\xc3\xb7", "\xc2\xb0", "\xc2\xbc", "\xc2\xbd", "\xc2\xbe", "\xc2\xa7",
        /* 0xC0 */ "\xc3\x81", "\xc3\x80", "\xc3\x89", "\xc3\x88", "\xc3\x8d", "\xc3\x8c", "\xc3\x93", "\xc3\x92",
        /* 0xC8 */ "\xc3\x9a", "\xc3\x99", "\xc5\x98", "\xc4\x8c", "\xc5\xa0", "\xc5\xbd", "\xc3\x90", "\xc4\xbf",
        /* 0xD0 */ "\xc3\x82", "\xc3\x84", "\xc3\x8a", "\xc3\x8b", "\xc3\x8e", "\xc3\x8f", "\xc3\x94", "\xc3\x96",
        /* 0xD8 */ "\xc3\x9b", "\xc3\x9c", "\xc5\x99", "\xc4\x8d", "\xc5\xa1", "\xc5\xbe", "\xc4\x91", "\xc5\x80",
        /* 0xE0 */ "\xc3\x83", "\xc3\x85", "\xc3\x86", "\xc5\x92", "\xc5\xb7", "\xc3\x9d", "\xc3\x95", "\xc3\x98",
        /* 0xE8 */ "\xc3\x9e", "\xc5\x8a", "\xc5\x94", "\xc4\x86", "\xc5\x9a", "\xc5\xb9", "\xc5\xa6", "\xc3\xb0",
        /* 0xF0 */ "\xc3\xa3", "\xc3\xa5", "\xc3\xa6", "\xc5\x93", "\xc5\xb5", "\xc3\xbd", "\xc3\xb5", "\xc3\xb8",
        /* 0xF8 */ "\xc3\xbe", "\xc5\x8b", "\xc5\x95", "\xc4\x87", "\xc5\x9b", "\xc5\xba", "\xc5\xa7", "?",
    };

    std::string Decoder::repertoireToUtf8(const std::string& raw) {
        std::string out;
        out.reserve(raw.size() * 2);
        for (unsigned char c : raw) { out += EBU_REPERTOIRE[c]; }
        return out;
    }

    void Decoder::process(uint8_t* symbols, int count) {
        for (int i = 0; i < count; i++) {
            // Shift in the bit
            shiftReg = ((shiftReg << 1) & 0x3FFFFFF) | (symbols[i] & 1);

            // Skip if we need to shift in new data
            if (--skip > 0) { continue; }

            // Calculate the syndrome and update sync status
            uint16_t syn = calcSyndrome(shiftReg);
            auto synIt = SYNDROMES.find(syn);
            bool knownSyndrome = synIt != SYNDROMES.end();
            sync = std::clamp<int>(knownSyndrome ? ++sync : --sync, 0, 4);
            
            // If we're still no longer in sync, try to resync
            if (!sync) { continue; }

            // Figure out which block we've got
            BlockType type;
            if (knownSyndrome) {
                type = SYNDROMES[syn];
            }
            else {
                // Assume the type is the one following the previous block
                type = nextType[lastType];
            }

            // Save block while correcting errors (NOT YET) <- idk why the "not yet is here", TODO: find why
            blocks[type] = correctErrors(shiftReg, type, blockAvail[type]);

            // If block type is A, decode it directly, otherwise, update continous count
            if (type == BLOCK_TYPE_A) {
                decodeBlockA();
            }
            else if (type == BLOCK_TYPE_B) { contGroup = 1; }
            else if ((type == BLOCK_TYPE_C || type == BLOCK_TYPE_CP) && lastType == BLOCK_TYPE_B) { contGroup++; }
            else if (type == BLOCK_TYPE_D && (lastType == BLOCK_TYPE_C || lastType == BLOCK_TYPE_CP)) { contGroup++; }
            else {
                // If block B is available, decode it alone.
                if (contGroup == 1) {
                    decodeBlockB();
                }
                contGroup = 0;
            }

            // If we've got an entire group, process it
            if (contGroup >= 3) {
                contGroup = 0;
                decodeGroup();
            }

            // // Remember the last block type and skip to new block
            lastType = type;
            skip = BLOCK_LEN;
        }
    }

    uint16_t Decoder::calcSyndrome(uint32_t block) {
        uint16_t syn = 0;

        // Calculate the syndrome using a LFSR
        for (int i = BLOCK_LEN - 1; i >= 0; i--) {
            // Shift the syndrome and keep the output
            uint8_t outBit = (syn >> (POLY_LEN - 1)) & 1;
            syn = (syn << 1) & 0b1111111111;

            // Apply LFSR polynomial
            syn ^= LFSR_POLY * outBit;

            // Apply input polynomial.
            syn ^= IN_POLY * ((block >> i) & 1);
        }

        return syn;
    }

    uint32_t Decoder::correctErrors(uint32_t block, BlockType type, bool& recovered) {        
        // Subtract the offset from block
        block ^= (uint32_t)OFFSETS[type];
        uint32_t out = block;

        // Calculate syndrome of corrected block
        uint16_t syn = calcSyndrome(block);

        // Use the syndrome register to do error correction if errors are present
        uint8_t errorFound = 0;
        if (syn) {
            for (int i = DATA_LEN - 1; i >= 0; i--) {
                // Check if the 5 leftmost bits are all zero
                errorFound |= !(syn & 0b11111);

                // Write output
                uint8_t outBit = (syn >> (POLY_LEN - 1)) & 1;
                out ^= (errorFound & outBit) << (i + POLY_LEN);

                // Shift syndrome
                syn = (syn << 1) & 0b1111111111;
                syn ^= LFSR_POLY * outBit * !errorFound;
            }
        }
        recovered = !(syn & 0b11111);

        return out;
    }

    void Decoder::decodeBlockA() {
        // Acquire lock
        std::lock_guard<std::mutex> lck(blockAMtx);

        // If it didn't decode properly return
        if (!blockAvail[BLOCK_TYPE_A]) { return; }

        // Decode PI code
        piCode = (blocks[BLOCK_TYPE_A] >> 10) & 0xFFFF;
        countryCode = (blocks[BLOCK_TYPE_A] >> 22) & 0xF;
        programCoverage = (AreaCoverage)((blocks[BLOCK_TYPE_A] >> 18) & 0xF);
        programRefNumber = (blocks[BLOCK_TYPE_A] >> 10) & 0xFF;
        callsign = decodeCallsign(piCode);

        // Update timeout
        blockALastUpdate = std::chrono::high_resolution_clock::now();;
    }

    void Decoder::decodeBlockB() {
        // Acquire lock
        std::lock_guard<std::mutex> lck(blockBMtx);

        // If it didn't decode properly return (TODO: Make sure this is not needed)
        if (!blockAvail[BLOCK_TYPE_B]) { return; }

        // Decode group type and version
        groupType = (blocks[BLOCK_TYPE_B] >> 22) & 0xF;
        groupVer = (GroupVersion)((blocks[BLOCK_TYPE_B] >> 21) & 1);

        // Decode traffic program and program type
        trafficProgram = (blocks[BLOCK_TYPE_B] >> 20) & 1;
        programType = (ProgramType)((blocks[BLOCK_TYPE_B] >> 15) & 0x1F);

        // Update timeout
        blockBLastUpdate = std::chrono::high_resolution_clock::now();
    }

    void Decoder::decodeGroup0() {
        // Acquire lock
        std::lock_guard<std::mutex> lck(group0Mtx);

        // Decode Block B data
        trafficAnnouncement = (blocks[BLOCK_TYPE_B] >> 14) & 1;
        music = (blocks[BLOCK_TYPE_B] >> 13) & 1;
        uint8_t diBit = (blocks[BLOCK_TYPE_B] >> 12) & 1;
        uint8_t offset = ((blocks[BLOCK_TYPE_B] >> 10) & 0b11);
        uint8_t diOffset = 3 - offset;
        uint8_t psOffset = offset * 2;

        // Decode Block C data
        if (groupVer == GROUP_VER_A && blockAvail[BLOCK_TYPE_C]) {
            alternateFrequency = (blocks[BLOCK_TYPE_C] >> 10) & 0xFFFF;
        }

        // Write DI bit to the decoder identification
        decoderIdent &= ~(1 << diOffset);
        decoderIdent |= (diBit << diOffset);

        // Write chars at offset the PSName
        if (blockAvail[BLOCK_TYPE_D]) {
            programServiceName[psOffset] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
            programServiceName[psOffset + 1] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;

            // Update the full version only when reaching the end
            if (offset == 3) {
                programServiceNameFullUpdate = programServiceName;
            }
        }

        // Update timeout
        group0LastUpdate = std::chrono::high_resolution_clock::now();
    }

    void Decoder::decodeGroup2() {
        // Acquire lock
        std::lock_guard<std::mutex> lck(group2Mtx);

        // Get char offset and write chars in the Radiotext
        bool nAB = (blocks[BLOCK_TYPE_B] >> 14) & 1;
        uint8_t offset = (blocks[BLOCK_TYPE_B] >> 10) & 0xF;

        // Clear text field if the A/B flag changed
        if (nAB != rtAB) {
            radioTextFullUpdate = radioText;
            radioText = "                                                                ";
        }
        rtAB = nAB;

        // Write char at offset in Radiotext
        if (groupVer == GROUP_VER_A) {
            uint8_t rtOffset = offset * 4;
            if (blockAvail[BLOCK_TYPE_C]) {
                radioText[rtOffset] = (blocks[BLOCK_TYPE_C] >> 18) & 0xFF;
                radioText[rtOffset + 1] = (blocks[BLOCK_TYPE_C] >> 10) & 0xFF;
            }
            if (blockAvail[BLOCK_TYPE_D]) {
                radioText[rtOffset + 2] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
                radioText[rtOffset + 3] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;
            }

            // If a carriage return was sent, update the full text
            if (offset == 0xF || radioText[rtOffset] == 0x0D || radioText[rtOffset + 1] == 0x0D || radioText[rtOffset + 2] == 0x0D || radioText[rtOffset + 3] == 0x0D) {
                radioTextFullUpdate = radioText;
            }
        }
        else {
            uint8_t rtOffset = offset * 2;
            if (blockAvail[BLOCK_TYPE_D]) {
                radioText[rtOffset] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
                radioText[rtOffset + 1] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;
            }

            // If a carriage return was sent, update the full text
            if (offset == 0xF || radioText[rtOffset] == 0x0D || radioText[rtOffset + 1] == 0x0D) {
                radioTextFullUpdate = radioText;
            }
        }

        // Update timeout
        group2LastUpdate = std::chrono::high_resolution_clock::now();
    }

    void Decoder::decodeGroup10() {
        // Acquire lock
        std::lock_guard<std::mutex> lck(group10Mtx);

        // Check if the text needs to be cleared
        bool ab = (blocks[BLOCK_TYPE_B] >> 14) & 1;
        if (ab != ptnAB) {
            programTypeName = "        ";
        }
        ptnAB = ab;

        // Decode segment address
        bool addr = (blocks[BLOCK_TYPE_B] >> 10) & 1;

        // Save text depending on address
        if (addr) {
            if (blockAvail[BLOCK_TYPE_C]) {
                programTypeName[4] = (blocks[BLOCK_TYPE_C] >> 18) & 0xFF;
                programTypeName[5] = (blocks[BLOCK_TYPE_C] >> 10) & 0xFF;
            }
            if (blockAvail[BLOCK_TYPE_D]) {
                programTypeName[6] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
                programTypeName[7] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;
            }
        }
        else {
            if (blockAvail[BLOCK_TYPE_C]) {
                programTypeName[0] = (blocks[BLOCK_TYPE_C] >> 18) & 0xFF;
                programTypeName[1] = (blocks[BLOCK_TYPE_C] >> 10) & 0xFF;
            }
            if (blockAvail[BLOCK_TYPE_D]) {
                programTypeName[2] = (blocks[BLOCK_TYPE_D] >> 18) & 0xFF;
                programTypeName[3] = (blocks[BLOCK_TYPE_D] >> 10) & 0xFF;
            }
        }

        // Update timeout
        group10LastUpdate = std::chrono::high_resolution_clock::now();
    }

    void Decoder::decodeGroup() {
        // Make sure blocks B is available
        if (!blockAvail[BLOCK_TYPE_B]) { return; }

        // Decode block B
        decodeBlockB();

        // Decode depending on group type
        switch (groupType) {
        case 0:
            decodeGroup0();
            break;
        case 2:
            decodeGroup2();
            break;
        case 10:
            decodeGroup10();
            break;
        default:
            break;
        }
    }

    std::string Decoder::base26ToCall(uint16_t pi) {
        // Determin first better based on offset
        bool w = (pi >= 21672);
        std::string callsign(w ? "W" : "K");

        // Base25 decode the rest
        std::string restStr;
        int rest = pi - (w ? 21672 : 4096);
        while (rest) {
            restStr += 'A' + (rest % 26);
            rest /= 26;
        }

        // Pad with As
        while (restStr.size() < 3) {
            restStr += 'A';
        }

        // Reorder chars
        for (int i = restStr.size() - 1; i >= 0; i--) {
            callsign += restStr[i];
        }

        return callsign;
    }

    std::string Decoder::decodeCallsign(uint16_t pi) {
        if ((pi >> 8) == 0xAF) {
            // AFXY -> XY00
            return base26ToCall((pi & 0xFF) << 8);
        }
        else if ((pi >> 12) == 0xA) {
            // AXYZ -> X0YZ
            return base26ToCall((((pi >> 8) & 0xF) << 12) | (pi & 0xFF));
        }
        else if (pi >= 0x9950 && pi <= 0x9EFF) {
            // 3 letter callsigns
            if (THREE_LETTER_CALLS.find(pi) != THREE_LETTER_CALLS.end()) {
                return THREE_LETTER_CALLS[pi];
            }
            else {
                return "Not Assigned";
            }
        }
        else if (pi >= 0x1000 && pi <= 0x994F) {
            // Normal encoding
            if ((pi & 0xFF) == 0 || ((pi >> 8) & 0xF) == 0) {
                return "Not Assigned";
            }
            else {
                return base26ToCall(pi);
            }
        }
        else if (pi >= 0xB000 && pi <= 0xEFFF) {
            uint16_t _pi = ((pi >> 12) << 8) | (pi & 0xFF);
            if (NAT_LOC_LINKED_STATIONS.find(_pi) != NAT_LOC_LINKED_STATIONS.end()) {
                return NAT_LOC_LINKED_STATIONS[_pi];
            }
            else {
                return "Not Assigned";
            }
        }
        else {
            return "Not Assigned";
        }
    }

    bool Decoder::blockAValid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - blockALastUpdate)).count() < RDS_BLOCK_A_TIMEOUT_MS;
    }

    bool Decoder::blockBValid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - blockBLastUpdate)).count() < RDS_BLOCK_B_TIMEOUT_MS;
    }

    bool Decoder::group0Valid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - group0LastUpdate)).count() < RDS_GROUP_0_TIMEOUT_MS;
    }

    bool Decoder::group2Valid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - group2LastUpdate)).count() < RDS_GROUP_2_TIMEOUT_MS;
    }

    bool Decoder::group10Valid() {
        auto now = std::chrono::high_resolution_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(now - group10LastUpdate)).count() < RDS_GROUP_10_TIMEOUT_MS;
    }
}
