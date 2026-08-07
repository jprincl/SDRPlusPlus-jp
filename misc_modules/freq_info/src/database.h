#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include "entry.h"

class ListenInfoDatabase {
public:
    // Full reload, not incremental — matches the "re-import = re-read the
    // whole file" decision (EiBi/Aoki files are the source of truth, we
    // don't merge/dedup across imports).
    void loadEibi(const std::string& path);

    size_t size() const { return entries.size(); }

    // Time-filtered, frequency-ascending, for everything currently in
    // [lowFreq, highFreq]. Used both for waterfall markers and for the
    // panel's "entries in view" table — same result feeds both.
    std::vector<const ListenInfoEntry*> queryRange(
        double lowFreq, double highFreq,
        std::chrono::system_clock::time_point now) const;

    // Narrow window around one frequency (+/- toleranceHz), time-filtered,
    // with entries matching `preferredTargetArea` sorted first (case-
    // insensitive substring match against the entry's targetArea field).
    // Pass an empty preferredTargetArea to skip that ranking and get
    // frequency order only.
    std::vector<const ListenInfoEntry*> queryFrequency(
        double freq, double toleranceHz,
        std::chrono::system_clock::time_point now,
        const std::string& preferredTargetArea = "") const;

private:
    std::vector<ListenInfoEntry> entries; // kept sorted by frequency ascending

    // Weekday + HHMM check against an entry's schedule, including
    // overnight (startTime > endTime) wraparound. system_clock::time_point
    // is always UTC-based (no local-time ambiguity), matching EiBi's UTC
    // schedule times.
    static bool isActiveNow(const ListenInfoEntry& e, std::chrono::system_clock::time_point now);
};
