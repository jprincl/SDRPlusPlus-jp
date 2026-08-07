#include "eibi_parser.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <utils/flog.h>

namespace {

std::vector<std::string> splitSemicolons(const std::string& line) {
    // Manual find()-based split. std::getline(ss, field, ';') silently
    // drops the final field when the line ends with the delimiter — and
    // EiBi lines always end in ';;' for the unused fields #10/#11, so
    // that approach was undercounting every single line by one.
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t pos = line.find(';', start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { return ""; }
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// EiBi day-of-week digits are 1=Monday..7=Sunday. Our days[] follows
// FrequencyBookmark's convention, index 0 = Sunday .. index 6 = Saturday.
int eibiDigitToDaysIndex(char c) {
    switch (c) {
        case '1': return 1; // Monday
        case '2': return 2; // Tuesday
        case '3': return 3; // Wednesday
        case '4': return 4; // Thursday
        case '5': return 5; // Friday
        case '6': return 6; // Saturday
        case '7': return 0; // Sunday
        default:  return -1;
    }
}

// Two-letter weekday abbreviations, same index mapping as above.
int eibiAbbrevToDaysIndex(const std::string& ab) {
    if (ab == "Su") return 0;
    if (ab == "Mo") return 1;
    if (ab == "Tu") return 2;
    if (ab == "We") return 3;
    if (ab == "Th") return 4;
    if (ab == "Fr") return 5;
    if (ab == "Sa") return 6;
    return -1;
}

// Parses field #3 (Days). Returns true if it recognized a clean
// weekday set (digits or Mo/Tu/.../Su pairs) and filled `days`
// accordingly; returns false for anything else (irr/tent/alt/1.Sa/...),
// in which case the caller defaults to "every day" and keeps the raw
// string for display.
bool parseDays(const std::string& raw, bool days[7]) {
    std::string s = trim(raw);
    if (s.empty()) {
        for (int i = 0; i < 7; i++) days[i] = true;
        return true;
    }

    // All-digit case, e.g. "1245"
    bool allDigits = true;
    for (char c : s) {
        if (!std::isdigit((unsigned char)c)) { allDigits = false; break; }
    }
    if (allDigits) {
        for (int i = 0; i < 7; i++) days[i] = false;
        for (char c : s) {
            int idx = eibiDigitToDaysIndex(c);
            if (idx < 0) { return false; } // shouldn't happen given allDigits, but be safe
            days[idx] = true;
        }
        return true;
    }

    // Two-letter pairs, e.g. "MoTuThFr"
    if (s.size() % 2 == 0) {
        bool allPairsValid = true;
        for (size_t i = 0; i < s.size(); i += 2) {
            if (eibiAbbrevToDaysIndex(s.substr(i, 2)) < 0) { allPairsValid = false; break; }
        }
        if (allPairsValid) {
            for (int i = 0; i < 7; i++) days[i] = false;
            for (size_t i = 0; i < s.size(); i += 2) {
                days[eibiAbbrevToDaysIndex(s.substr(i, 2))] = true;
            }
            return true;
        }
    }

    // Anything else: alt, altFr, harm, imod, irr, Haj, MF-15, Ram, tent,
    // test, 15Sep, LSB, USB, 1.Sa, 1WeFr, Last7, ... — not modeled for v1.
    return false;
}

// Parses "HHMM-HHMM" into two ints. Does NOT resolve overnight ranges
// (e.g. 2200-0600) — that's the query function's job, not the parser's;
// storing raw start/end here keeps the parser format-agnostic.
bool parseTimeRange(const std::string& raw, int& startTime, int& endTime) {
    size_t dash = raw.find('-');
    if (dash == std::string::npos) { return false; }
    std::string a = raw.substr(0, dash);
    std::string b = raw.substr(dash + 1);
    if (a.empty() || b.empty()) { return false; }
    try {
        startTime = std::stoi(a);
        endTime = std::stoi(b);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

} // namespace

std::vector<ListenInfoEntry> parseEibiCsv(const std::string& path) {
    std::vector<ListenInfoEntry> out;

    std::ifstream file(path);
    if (!file.is_open()) {
        flog::error("freq_info: could not open EiBi file: {0}", path);
        return out;
    }

    std::string line;
    int lineNo = 0;
    int skipped = 0;
    while (std::getline(file, line)) {
        lineNo++;
        if (line.empty()) { continue; }

        std::vector<std::string> f = splitSemicolons(line);
        // Spec: "each line has to contain ten semicolons - not more, not less"
        // i.e. exactly 11 fields.
        if (f.size() != 11) {
            skipped++;
            continue;
        }

        ListenInfoEntry e;

        // Field 1: frequency in kHz -> Hz
        try {
            e.frequency = std::stod(trim(f[0])) * 1000.0;
        } catch (const std::exception&) {
            skipped++;
            continue;
        }

        // Field 2: time range
        if (!parseTimeRange(trim(f[1]), e.startTime, e.endTime)) {
            skipped++;
            continue;
        }

        // Field 3: days
        e.daysKnown = parseDays(f[2], e.days);
        if (!e.daysKnown) {
            e.scheduleRaw = trim(f[2]);
        }

        // Fields 4-8
        e.ituCountry = trim(f[3]);
        e.name = trim(f[4]);
        e.language = trim(f[5]);
        e.targetArea = trim(f[6]);
        e.transmitterSite = trim(f[7]);

        // Field 9: persistence code. 8 = inactive entry, dropped by
        // EiBi's own tooling from the freq/bc files — do the same here.
        int persistence = 0;
        try {
            persistence = std::stoi(trim(f[8]));
        } catch (const std::exception&) {
            // malformed but not fatal — keep the entry, treat as normal
        }
        if (persistence == 8) { continue; }

        e.source = "eibi";
        out.push_back(std::move(e));
    }

    if (skipped > 0) {
        flog::warn("freq_info: skipped {0} malformed line(s) in {1}", skipped, path);
    }
    flog::info("freq_info: parsed {0} entries from {1}", out.size(), path);

    return out;
}
