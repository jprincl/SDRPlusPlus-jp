#pragma once
#include "bookmark.h"

// Bookmark broadcast schedule (on-air) logic. Times are HHMM integers in UTC,
// days are indexed 0 (Sunday) through 6 (Saturday).
// UTC helpers adapted from Otto Pattemore's shortwave-station-list module (GPL-3.0)
// https://github.com/OttoPattemore/shortwave-station-list-sdrpp

int getUTCTime();
int getWeekDay();
bool timeValid(int time);
bool bookmarkOnline(const FrequencyBookmark& bm, int now, int weekDay);
