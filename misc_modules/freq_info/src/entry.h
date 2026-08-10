#pragma once
#include <imgui.h>
#include <string>
#include <cmath>

// One station/broadcast record, from any imported source (eibi, aoki, ...).
// Modeled on frequency_manager's FrequencyBookmark: startTime/endTime/days
// use the exact same representation so any future UI code (editors,
// formatters) can be shared or ported directly.
struct ListenInfoEntry {
    double frequency = 0.0;    // Hz (converted from the source's kHz at parse time)
    double bandwidth = 0.0;    // 0 = unknown/unused; most broadcast entries don't carry one
    int mode = -1;             // demod hint if the source gives one; -1 = unknown

    // Time window — same format as FrequencyBookmark so UI code is reusable.
    int startTime = 0;         // HHMM, UTC
    int endTime = 2400;        // HHMM, UTC (2400 = runs to end of day)
    bool days[7] = {true, true, true, true, true, true, true}; // index 0 = Sunday, matches FM's convention
    bool daysKnown = true;     // false = schedule string wasn't a clean weekday set (irr/tent/alt/...);
                                // we default such entries to "every day" but flag them so the UI can say so

    std::string name;          // station name — first-class field, not stuffed into notes
    std::string language;
    std::string targetArea;    // EiBi "Target", e.g. "Eu", "As" — used for candidate ranking
    std::string ituCountry;
    std::string transmitterSite;
    std::string scheduleRaw;   // original days/comment string, kept for display when daysKnown == false

    double lat = NAN;
    double lon = NAN;          // NAN when the source doesn't give coordinates (most eibi entries)
    double powerKw = -1.0;     // -1 = unknown

    std::string source;        // "eibi" / "aoki" / ...
};

// Drawing wrapper — mirrors frequency_manager's WaterfallBookmark. The
// cached rect is filled in during onFFTRedraw and reused for hit-testing
// in onInputProcess, exactly like FM/Spots do.
struct WaterfallListenInfoLabel {
    ListenInfoEntry entry;
    ImU32 color;
    ImVec2 rectMin;
    ImVec2 rectMax;
};
