#include "database.h"
#include "eibi_parser.h"
#include "aoki_parser.h"
#include <ctime>
#include <cctype>
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

std::vector<const ListenInfoEntry*> ListenInfoDatabase::queryFrequency(
    double freq, double toleranceHz,
    std::chrono::system_clock::time_point now,
    const std::string& preferredTargetArea) const {

    auto out = queryRange(freq - toleranceHz, freq + toleranceHz, now);

    if (!preferredTargetArea.empty()) {
        std::string pref = preferredTargetArea;
        for (auto& c : pref) c = (char)std::tolower((unsigned char)c);

        std::stable_sort(out.begin(), out.end(),
            [&pref](const ListenInfoEntry* a, const ListenInfoEntry* b) {
                auto matches = [&pref](const ListenInfoEntry* e) {
                    std::string ta = e->targetArea;
                    for (auto& c : ta) c = (char)std::tolower((unsigned char)c);
                    return ta.find(pref) != std::string::npos;
                };
                bool ma = matches(a), mb = matches(b);
                if (ma != mb) { return ma; } // matching entries first
                return false; // stable_sort keeps frequency order within each group
            });
    }

    return out;
}
