#include "bookmark.h"
#include <algorithm>
#include <cctype>

const char* demodModeList[] = {
    "NFM",
    "WFM",
    "AM",
    "DSB",
    "USB",
    "CW",
    "LSB",
    "RAW"
};

const char* demodModeListTxt = "NFM\0WFM\0AM\0DSB\0USB\0CW\0LSB\0RAW\0";

// demodModeList has no terminator and callers see it as an incomplete
// array type, so they cannot size it themselves.
const int demodModeCount = (int)(sizeof(demodModeList) / sizeof(demodModeList[0]));

FrequencyBookmark bookmarkFromJson(const json& j) {
    FrequencyBookmark bm;
    bm.frequency = j["frequency"];
    bm.bandwidth = j["bandwidth"];
    bm.mode = j["mode"];
    bm.startTime = j.contains("startTime") ? (int)j["startTime"] : 0;
    bm.endTime = j.contains("endTime") ? (int)j["endTime"] : 0;
    for (int i = 0; i < 7; i++) { bm.days[i] = true; }
    if (j.contains("days") && j["days"].is_array()) {
        int count = std::min<int>(7, j["days"].size());
        for (int i = 0; i < count; i++) {
            if (j["days"][i].is_boolean()) { bm.days[i] = j["days"][i]; }
        }
    }
    bm.geoinfo = (j.contains("geoinfo") && j["geoinfo"].is_string()) ? (std::string)j["geoinfo"] : "";
    bm.notes = (j.contains("notes") && j["notes"].is_string()) ? (std::string)j["notes"] : "";
    bm.selected = false;
    return bm;
}

json bookmarkToJson(const FrequencyBookmark& bm) {
    json j;
    j["frequency"] = bm.frequency;
    j["bandwidth"] = bm.bandwidth;
    j["mode"] = bm.mode;
    j["startTime"] = bm.startTime;
    j["endTime"] = bm.endTime;
    json days = json::array();
    for (int i = 0; i < 7; i++) { days.push_back(bm.days[i]); }
    j["days"] = days;
    j["geoinfo"] = bm.geoinfo;
    j["notes"] = bm.notes;
    return j;
}

bool compareWaterfallBookmarks(const WaterfallBookmark& wbm1, const WaterfallBookmark& wbm2) {
    return (wbm1.bookmark.frequency < wbm2.bookmark.frequency);
}

bool compareBookmarksFreqAsc(const std::pair<std::string, FrequencyBookmark>& a, const std::pair<std::string, FrequencyBookmark>& b) {
    return a.second.frequency < b.second.frequency;
}

bool compareBookmarksFreqDesc(const std::pair<std::string, FrequencyBookmark>& a, const std::pair<std::string, FrequencyBookmark>& b) {
    return a.second.frequency > b.second.frequency;
}

ImU32 hexStrToColor(const std::string& col) {
    if (col.length() == 7 && col[0] == '#' && std::all_of(col.begin() + 1, col.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        int r = std::stoi(col.substr(1, 2), NULL, 16);
        int g = std::stoi(col.substr(3, 2), NULL, 16);
        int b = std::stoi(col.substr(5, 2), NULL, 16);
        return IM_COL32(r, g, b, 255);
    }
    return IM_COL32(255, 255, 0, 255);
}

ImVec4 color32ToVec4(ImU32 col) {
    ImVec4 val;
    float sc = 1.0f / 255.0f;
    val.x = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) * sc;
    val.y = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) * sc;
    val.z = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) * sc;
    val.w = (float)((col >> IM_COL32_A_SHIFT) & 0xFF) * sc;
    return val;
}
