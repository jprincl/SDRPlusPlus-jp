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

// Above this frequency, real shortwave-skip propagation dominates and raw
// distance stops being a meaningful predictor of audibility — a station
// 15,000 km away can be stronger than one 2,000 km away depending on how
// that specific path is currently skipping, so ranking "closest first"
// there would be actively misleading, not just weaker. Below it (LW/MW/
// tropical bands), propagation is mostly ground-wave or simple single-hop
// skywave, where distance genuinely does predict audibility well — this
// is exactly the range where the distance tier was built for and tested.
constexpr double DISTANCE_RANKING_MAX_HZ = 5000000.0; // 5 MHz

// Shared by queryRange() (as a same-frequency tiebreaker) and
// queryFrequency() (as the entire ranking, since it's already narrowed to
// one cluster) — having one function means a co-channel cluster ranks
// identically everywhere it's shown (table, waterfall lanes, "Now tuned"),
// instead of each place re-implementing its own version that could drift.
// Three tiers: (1) known distance, only below DISTANCE_RANKING_MAX_HZ — an
// actual measurement, so it beats (2) a coarse target-area match (the only
// signal left above that threshold, or when coordinates aren't known —
// most EiBi entries), which beats (3) no preference either way (stable_sort
// leaves those in whatever order they arrived in).
struct Ranker {
    bool haveListenerLoc;
    double listenerLat, listenerLon;
    bool havePref;
    std::string prefLower;

    bool hasCoords(const ListenInfoEntry* e) const {
        return haveListenerLoc && !std::isnan(e->lat) && !std::isnan(e->lon)
            && e->frequency < DISTANCE_RANKING_MAX_HZ;
    }
    bool targetMatches(const ListenInfoEntry* e) const {
        std::string ta = e->targetArea;
        for (auto& c : ta) c = (char)std::tolower((unsigned char)c);
        return ta.find(prefLower) != std::string::npos;
    }
    // True if 'a' should rank ahead of 'b' by source/target/distance alone
    // (no frequency component).
    bool before(const ListenInfoEntry* a, const ListenInfoEntry* b) const {
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
    }
    bool active() const { return haveListenerLoc || havePref; }
};

Ranker makeRanker(const std::string& preferredTargetArea, double listenerLat, double listenerLon) {
    Ranker r;
    r.haveListenerLoc = !std::isnan(listenerLat) && !std::isnan(listenerLon);
    r.listenerLat = listenerLat;
    r.listenerLon = listenerLon;
    r.prefLower = preferredTargetArea;
    for (auto& c : r.prefLower) c = (char)std::tolower((unsigned char)c);
    r.havePref = !r.prefLower.empty();
    return r;
}

} // namespace

std::vector<const ListenInfoEntry*> ListenInfoDatabase::queryRange(
    double lowFreq, double highFreq,
    std::chrono::system_clock::time_point now,
    const std::string& preferredTargetArea,
    double listenerLat, double listenerLon) const {

    std::vector<const ListenInfoEntry*> out;
    if (combined.empty() || lowFreq > highFreq) { return out; }

    auto lo = std::lower_bound(combined.begin(), combined.end(), lowFreq,
        [](const ListenInfoEntry& e, double f) { return e.frequency < f; });
    auto hi = std::upper_bound(combined.begin(), combined.end(), highFreq,
        [](double f, const ListenInfoEntry& e) { return f < e.frequency; });

    for (auto it = lo; it != hi; ++it) {
        if (isActiveNow(*it, now)) { out.push_back(&(*it)); }
    }

    Ranker ranker = makeRanker(preferredTargetArea, listenerLat, listenerLon);
    if (ranker.active()) {
        // Frequency stays the primary order (that's how a table/waterfall
        // should read overall) — the ranker only breaks ties between
        // entries close enough in frequency to be effectively the same
        // channel (within 1 Hz; real co-channel entries share the exact
        // nominal frequency, this isn't about nearby-but-distinct channels).
        std::stable_sort(out.begin(), out.end(),
            [&](const ListenInfoEntry* a, const ListenInfoEntry* b) {
                if (std::abs(a->frequency - b->frequency) > 1.0) {
                    return a->frequency < b->frequency;
                }
                return ranker.before(a, b);
            });
    }

    return out;
}

std::vector<const ListenInfoEntry*> ListenInfoDatabase::queryFrequency(
    double freq, double toleranceHz,
    std::chrono::system_clock::time_point now,
    const std::string& preferredTargetArea,
    double listenerLat, double listenerLon) const {

    auto out = queryRange(freq - toleranceHz, freq + toleranceHz, now);

    Ranker ranker = makeRanker(preferredTargetArea, listenerLat, listenerLon);
    if (ranker.active()) {
        // Whole window is one cluster here (already narrowed to +/-
        // toleranceHz around a single point), so no frequency component —
        // rank purely by source/target/distance.
        std::stable_sort(out.begin(), out.end(),
            [&](const ListenInfoEntry* a, const ListenInfoEntry* b) {
                return ranker.before(a, b);
            });
    }

    return out;
}
