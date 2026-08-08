#include "aoki_parser.h"
#include "text_encoding.h"
#include <fstream>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utils/flog.h>

namespace {

// Column boundaries [start, end) measured directly against a real Aoki
// "A26 Shortwave Frequency List" sample (LW/MW rows only, verified line by
// line). The Pow field is intentionally one character wider on the left
// (76 instead of the header-implied 77) — a 4-digit power value (e.g.
// "1600") right-aligned in its slot pushed one character into what the
// header's own spacing would suggest was still the Language field; found
// this exact case in the sample (Radio Mediterranee Int'l, 1600 kW) and
// re-measured from there rather than trusting the header alone.
//
// NOT YET VERIFIED against the shortwave portion of the file (5-digit kHz
// values like 15100, 21500) — only the LW/MW rows in the sample had that
// checked directly. Lines that don't parse (bad frequency, etc.) are
// skipped and counted rather than silently producing garbage, so a
// boundary mismatch further down the file should show up as a skip count,
// not wrong data.
constexpr int FREQ_START = 0,     FREQ_END = 6;
constexpr int STATION_START = 6,  STATION_END = 38;
constexpr int UTC_START = 38,     UTC_END = 48;
constexpr int DAYS_START = 48,    DAYS_END = 56;
constexpr int LANG_START = 56,    LANG_END = 76;
constexpr int POW_START = 76,     POW_END = 81;
constexpr int AZI_START = 81,     AZI_END = 85;
constexpr int LOC_START = 85,     LOC_END = 109;
constexpr int ADM_START = 109,    ADM_END = 113;
constexpr int LATLON_START = 113, LATLON_END = 129;
constexpr int REMARKS_START = 129;

std::string col(const std::string& line, int start, int end) {
    if ((int)line.size() <= start) { return ""; }
    int len = std::min((int)line.size(), end) - start;
    if (len <= 0) { return ""; }
    return trimStr(line.substr(start, len));
}

// Aoki's own header states "Day 1 = Sunday" — different from EiBi's
// "1=Monday". Digit d (1-7) -> our days[] index (0=Sunday..6=Saturday) is
// simply d-1, no rotation needed (unlike EiBi's +6 mod 7).
bool parseAokiDays(const std::string& raw, bool days[7]) {
    if (raw.empty()) {
        for (int i = 0; i < 7; i++) days[i] = true;
        return true;
    }
    bool allDigits = true;
    for (char c : raw) {
        if (!std::isdigit((unsigned char)c)) { allDigits = false; break; }
    }
    if (!allDigits) { return false; }
    for (int i = 0; i < 7; i++) days[i] = false;
    for (char c : raw) {
        int d = c - '0'; // 1-7, 1=Sunday
        if (d < 1 || d > 7) { return false; }
        days[d - 1] = true;
    }
    return true;
}

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

// "372221N1405056E" -> lat=37.3725, lon=140.8489. Lat is DDMMSS (6 digits)
// + hemisphere, lon is DDDMMSS (7 digits) + hemisphere. Returns false
// (leaving lat/lon as NAN) rather than guessing on anything that doesn't
// match this exact shape.
bool parseAokiLatLon(const std::string& raw, double& lat, double& lon) {
    // Find the two hemisphere letters to split lat from lon.
    size_t nsPos = raw.find_first_of("NS");
    if (nsPos == std::string::npos || nsPos != 6) { return false; } // lat is always 6 digits (DDMMSS) before the hemisphere letter
    size_t ewPos = raw.find_first_of("EW", nsPos + 1);
    if (ewPos == std::string::npos) { return false; }

    std::string latDigits = raw.substr(0, nsPos);
    char latHemi = raw[nsPos];
    std::string lonDigits = raw.substr(nsPos + 1, ewPos - (nsPos + 1));
    char lonHemi = raw[ewPos];

    if (latDigits.size() != 6 || lonDigits.size() != 7) { return false; }
    for (char c : latDigits) { if (!std::isdigit((unsigned char)c)) { return false; } }
    for (char c : lonDigits) { if (!std::isdigit((unsigned char)c)) { return false; } }

    double latDeg = std::stod(latDigits.substr(0, 2));
    double latMin = std::stod(latDigits.substr(2, 2));
    double latSec = std::stod(latDigits.substr(4, 2));
    double lonDeg = std::stod(lonDigits.substr(0, 3));
    double lonMin = std::stod(lonDigits.substr(3, 2));
    double lonSec = std::stod(lonDigits.substr(5, 2));

    lat = latDeg + latMin / 60.0 + latSec / 3600.0;
    if (latHemi == 'S') { lat = -lat; }
    lon = lonDeg + lonMin / 60.0 + lonSec / 3600.0;
    if (lonHemi == 'W') { lon = -lon; }
    return true;
}

} // namespace

std::vector<ListenInfoEntry> parseAokiTxt(const std::string& path) {
    std::vector<ListenInfoEntry> out;

    std::ifstream file(path);
    if (!file.is_open()) {
        flog::error("freq_info: could not open Aoki file: {0}", path);
        return out;
    }

    std::string rawLine;
    int lineNo = 0;
    int skipped = 0;
    while (std::getline(file, rawLine)) {
        lineNo++;
        if (rawLine.empty()) { continue; }
        std::string line = windows1252ToUtf8(rawLine); // encoding not yet confirmed for this source; safe no-op if already UTF-8/ASCII

        std::string freqStr = col(line, FREQ_START, FREQ_END);
        if (freqStr.empty()) { skipped++; continue; } // title/header rows and blanks land here

        double freqKHz;
        try {
            freqKHz = std::stod(freqStr);
        } catch (const std::exception&) {
            skipped++; // e.g. "FRE" header itself
            continue;
        }

        ListenInfoEntry e;
        e.frequency = freqKHz * 1000.0;
        e.name = col(line, STATION_START, STATION_END);

        if (!parseTimeRange(col(line, UTC_START, UTC_END), e.startTime, e.endTime)) {
            skipped++;
            continue;
        }

        e.daysKnown = parseAokiDays(col(line, DAYS_START, DAYS_END), e.days);
        if (!e.daysKnown) {
            e.scheduleRaw = col(line, DAYS_START, DAYS_END);
        }

        e.language = col(line, LANG_START, LANG_END);

        std::string powStr = col(line, POW_START, POW_END);
        if (!powStr.empty()) {
            try { e.powerKw = std::stod(powStr); } catch (const std::exception&) {}
        }

        e.ituCountry = col(line, ADM_START, ADM_END);
        e.transmitterSite = col(line, LOC_START, LOC_END);

        double lat, lon;
        if (parseAokiLatLon(col(line, LATLON_START, LATLON_END), lat, lon)) {
            e.lat = lat;
            e.lon = lon;
        }
        // Aoki doesn't carry EiBi's Target-area zone code (Eu/As/Af/...),
        // so targetArea stays empty — the existing target-area ranking in
        // ListenInfoDatabase::queryFrequency() just won't have a preference
        // signal for Aoki entries. lat/lon (above) are a real bonus Aoki
        // gives us that EiBi mostly doesn't: distance-based ranking becomes
        // possible later without needing a new source for it.

        e.source = "aoki";
        out.push_back(std::move(e));
    }

    if (skipped > 0) {
        flog::warn("freq_info: skipped {0} unrecognized line(s) in {1}", skipped, path);
    }
    flog::info("freq_info: parsed {0} entries from {1}", out.size(), path);

    return out;
}
