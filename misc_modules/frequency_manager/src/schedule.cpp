#include "schedule.h"
#include <ctime>

int getUTCTime() {
    std::time_t now = std::time(NULL);
    std::tm* now_tm = std::gmtime(&now);
    return (now_tm->tm_hour * 100) + now_tm->tm_min;
}

int getWeekDay() {
    std::time_t now = std::time(NULL);
    std::tm* now_tm = std::gmtime(&now);
    return now_tm->tm_wday;
}

bool timeValid(int time) {
    // Check HHMM time validity
    int hours = time / 100;
    int minutes = time % 100;
    return (hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59);
}

bool bookmarkOnline(const FrequencyBookmark& bm, int now, int weekDay) {
    if (!bm.days[weekDay]) {
        return false;
    }

    if (bm.startTime == 0 && bm.endTime == 0) {
        return true;
    }
    else if (bm.startTime < bm.endTime) {
        return (bm.startTime <= now) && (now < bm.endTime);
    }
    else if (bm.startTime > bm.endTime) {
        // Overnight schedule (e.g. 2200 - 0600)
        return (now >= bm.startTime) || (now <= bm.endTime);
    }
    else {
        return false; // Start and end times are equal (except 0000)
    }
}
