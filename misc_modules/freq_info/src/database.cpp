#include "database.h"
#include "eibi_parser.h"
#include "aoki_parser.h"
#include <ctime>
#include <cctype>
#include <cmath>
#include <utils/flog.h>

void ListenInfoDatabase::loadEibi(const std::string& path) {
    eibiEntries = parseEibiCsv(path);
    rebuildCombined();
    flog::info("freq_info: eibi now holds {0} entries ({1} total)", eibiEntries.size(), combined.size());
}

void ListenInfoDatabase::loadAoki(const std::string& path) {
    aokiEntries = parseAokiTxt(path);
    rebuildCombined();
    flog::info("freq_info: aoki now holds {0} entries ({1} total)", aokiEntries.size(), combined.size());
}

void ListenInfoDatabase::rebuildCombined() {
    combined.clear();
    combined.reserve(eibiEntries.size() + aokiEntries.size());
    combined.insert(combined.end(), eibiEntries.begin(), eibiEntries.end());
    combined.insert(combined.end(), aokiEntries.begin(), aokiEntries.end());
    std::sort(combined.begin(), combined.end(),
              [](const ListenInfoEntry& a, const ListenInfoEntry& b) {
                  return a.frequency < b.frequency;
              });
}

bool ListenInfoDatabase::isActiveNow(const ListenInfoEntry& e, std::chrono::system_clock::time_point now) {
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    int nowHHMM = utc.tm_hour * 100 + utc.tm_min;
    int wday = utc.tm_wday; // 0=Sunday..6=Saturday — matches ListenInfoEntry::days[] index directly

    int start = e.startTime;
    int end = e.endTime;

    if (start <= end) {
        // Same-day window, e.g. 0500-0700.
        if (nowHHMM < start || nowHHMM > end) { return false; }
        return e.days[wday];
    } else {
        // Overnight window, e.g. 2200-0600. Two cases:
        //  - we're in the evening part (nowHHMM >= start): belongs to today's day flag
        //  - we're in the early-morning part (nowHHMM <= end): the broadcast
        //    STARTED yesterday, so it's yesterday's day flag that matters,
        //    not today's.
        if (nowHHMM >= start) {
            return e.days[wday];
        }
        if (nowHHMM <= end) {
            int yesterday = (wday + 6) % 7;
            return e.days[yesterday];
        }
        return false;
    }
}

std::vector<const ListenInfoEntry*> ListenInfoDatabase::queryRange(
    double lowFreq, double highFreq,
    std::chrono::system_clock::time_point now) const {

    std::vector<const ListenInfoEntry*> out;
    if (combined.empty() || lowFreq > highFreq) { return out; }

    auto lo = std::lower_bound(combined.begin(), combined.end(), lowFreq,
        [](const ListenInfoEntry& e, double f) { return e.frequency < f; });
    auto hi = std::upper_bound(combined.begin(), combined.end(), highFreq,
        [](double f, const ListenInfoEntry& e) { return f < e.frequency; });

    for (auto it = lo; it != hi; ++it) {
        if (isActiveNow(*it, now)) { out.push_back(&(*it)); }
    }
    return out;
}

namespace {
// Great-circle distance in km. Standard haversine formula — accurate
// enough for "which of these transmitters is closer" ranking; no need for
// anything more precise (e.g. ellipsoidal) at this scale.
double haversineKm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371.0; // mean Earth radius, km
    constexpr double DEG2RAD = 3.14159265358979323846 / 180.0;
    double dLat = (lat2 - lat1) * DEG2RAD;
    double dLon = (lon2 - lon1) * DEG2RAD;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2)
             + std::cos(lat1 * DEG2RAD) * std::cos(lat2 * DEG2RAD)
             * std::sin(dLon / 2) * std::sin(dLon / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return R * c;
}
} // namespace

std::vector<const ListenInfoEntry*> ListenInfoDatabase::queryFrequency(
    double freq, double toleranceHz,
    std::chrono::system_clock::time_point now,
    const std::string& preferredTargetArea,
    double listenerLat, double listenerLon) const {

    auto out = queryRange(freq - toleranceHz, freq + toleranceHz, now);

    bool haveListenerLoc = !std::isnan(listenerLat) && !std::isnan(listenerLon);
    std::string pref = preferredTargetArea;
    for (auto& c : pref) c = (char)std::tolower((unsigned char)c);
    bool havePref = !pref.empty();

    if (!haveListenerLoc && !havePref) { return out; } // nothing to rank by — frequency order as-is

    auto targetMatches = [&pref](const ListenInfoEntry* e) {
        std::string ta = e->targetArea;
        for (auto& c : ta) c = (char)std::tolower((unsigned char)c);
        return ta.find(pref) != std::string::npos;
    };
    auto hasCoords = [&](const ListenInfoEntry* e) {
        return haveListenerLoc && !std::isnan(e->lat) && !std::isnan(e->lon);
    };

    // Three tiers: (1) known distance — the strongest signal, an actual
    // measurement rather than a coarse zone code; sorted closest-first.
    // (2) no usable coordinates but a target-area match. (3) everything
    // else, left in frequency order (stable_sort preserves it within a tier).
    std::stable_sort(out.begin(), out.end(),
        [&](const ListenInfoEntry* a, const ListenInfoEntry* b) {
            bool aCoord = hasCoords(a), bCoord = hasCoords(b);
            if (aCoord != bCoord) { return aCoord; }
            if (aCoord && bCoord) {
                double da = haversineKm(listenerLat, listenerLon, a->lat, a->lon);
                double db = haversineKm(listenerLat, listenerLon, b->lat, b->lon);
                return da < db;
            }
            if (havePref) {
                bool ma = targetMatches(a), mb = targetMatches(b);
                if (ma != mb) { return ma; }
            }
            return false;
        });

    return out;
}
