#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <limits>
#include "entry.h"

class ListenInfoDatabase {
public:
    // Each source keeps its own slot and is reloaded independently — the
    // UI imports EiBi and Aoki one at a time (separate file, separate
    // button press), so loading one must not disturb data already loaded
    // from the other. Full reload per source, not incremental (each file
    // is the source of truth for that source; no merge/dedup within it).
    void loadEibi(const std::string& path);
    void loadAoki(const std::string& path);

    size_t size() const { return combined.size(); }
    size_t eibiCount() const { return eibiEntries.size(); }
    size_t aokiCount() const { return aokiEntries.size(); }

    // Time-filtered, frequency-ascending, for everything currently in
    // [lowFreq, highFreq]. Used both for waterfall markers and for the
    // panel's "entries in view" table — same result feeds both. Frequency
    // stays the primary order; when two entries share (or nearly share)
    // a frequency, the same distance/target-area ranking used by
    // queryFrequency() below breaks the tie, so a table row or waterfall
    // lane order for a shared channel matches what "Now tuned" already
    // shows for that same channel.
    std::vector<const ListenInfoEntry*> queryRange(
        double lowFreq, double highFreq,
        std::chrono::system_clock::time_point now,
        const std::string& preferredTargetArea = "",
        double listenerLat = std::numeric_limits<double>::quiet_NaN(),
        double listenerLon = std::numeric_limits<double>::quiet_NaN()) const;

    // Narrow window around one frequency (+/- toleranceHz), time-filtered,
    // ranked in three tiers: (1) entries with known coordinates AND a known
    // listener location, sorted by ascending distance — the strongest
    // signal, since it's an actual measurement rather than a coarse zone
    // code; (2) entries without usable coordinates but matching
    // preferredTargetArea (case-insensitive substring against the entry's
    // targetArea field); (3) everything else, in frequency order. Pass an
    // empty preferredTargetArea and/or NAN coordinates to skip the
    // corresponding tier.
    std::vector<const ListenInfoEntry*> queryFrequency(
        double freq, double toleranceHz,
        std::chrono::system_clock::time_point now,
        const std::string& preferredTargetArea = "",
        double listenerLat = std::numeric_limits<double>::quiet_NaN(),
        double listenerLon = std::numeric_limits<double>::quiet_NaN()) const;

private:
    std::vector<ListenInfoEntry> eibiEntries;
    std::vector<ListenInfoEntry> aokiEntries;
    std::vector<ListenInfoEntry> combined; // eibiEntries + aokiEntries, kept sorted by frequency ascending

    void rebuildCombined();

    // Weekday + HHMM check against an entry's schedule, including
    // overnight (startTime > endTime) wraparound. system_clock::time_point
    // is always UTC-based (no local-time ambiguity), matching EiBi's UTC
    // schedule times.
    static bool isActiveNow(const ListenInfoEntry& e, std::chrono::system_clock::time_point now);
};
