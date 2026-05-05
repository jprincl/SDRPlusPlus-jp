#ifndef __SDRPP_SPOTS_MAIN_H
#define __SDRPP_SPOTS_MAIN_H

#include <string>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <exception>
#include <sstream>
#include <vector>

inline std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss (s);
    std::string item;

    while (getline (ss, item, delim)) {
        result.push_back (item);
    }

    return result;
}

// Convert a UTC std::tm to time_t without going through localtime/gmtime
// (whose shared static buffer makes the classic offset trick UB and
// thread-unsafe).
inline std::time_t tmToUtcTimeT(std::tm* tm) {
#ifdef _WIN32
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

// Reject obviously-out-of-range fields before tmToUtcTimeT silently
// normalizes them (e.g. month 99 rolling into a different timestamp).
// Year window is generous on purpose: we just want to stop runaway
// futures and pre-epoch garbage, not validate the calendar exactly.
inline bool validTm(const std::tm& tm) {
    if (tm.tm_year < 70  || tm.tm_year > 200) return false;  // 1970..2100
    if (tm.tm_mon  < 0   || tm.tm_mon  > 11)  return false;
    if (tm.tm_mday < 1   || tm.tm_mday > 31)  return false;
    if (tm.tm_hour < 0   || tm.tm_hour > 23)  return false;
    if (tm.tm_min  < 0   || tm.tm_min  > 59)  return false;
    if (tm.tm_sec  < 0   || tm.tm_sec  > 60)  return false;  // leap second
    return true;
}

// Parse "YYYY-MM-DDTHH:MM[:SS[.fff]]" as UTC. Seconds are optional.
inline bool parseIso8601Utc(const std::string& s, std::chrono::system_clock::time_point* out) {
    int y = 0, M = 0, d = 0, h = 0, m = 0;
    float sec = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%f", &y, &M, &d, &h, &m, &sec) < 5) {
        return false;
    }
    std::tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = M - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = m;
    tm.tm_sec = (int)sec;
    if (!validTm(tm)) { return false; }
    *out = std::chrono::system_clock::from_time_t(tmToUtcTimeT(&tm));
    return true;
}

inline int parseTime(const std::string &s, std::chrono::time_point<std::chrono::system_clock>* t) {
    std::tm tm{};
    // HHMM YYYY-mm-dd
    size_t loc = s.find(" ");
    if(loc == s.npos || loc+1 >= s.size()) {
        return 1;
    }
    std::string timeS = s.substr(0, loc);
    int time;
    try { time = std::stoi(timeS); } catch (const std::exception&) { return 2; }
    if(time < 0) {
        return 2;
    }
    tm.tm_sec = 0;
    tm.tm_min = time % 100;
    tm.tm_hour = time / 100;

    std::string dateS = s.substr(loc+1);
    loc = dateS.find("-");
    if(loc == dateS.npos || loc+1 >= dateS.size()) {
        return 5;
    }
    std::string yearS = dateS.substr(0, loc);
    int year;
    try { year = std::stoi(yearS); } catch (const std::exception&) { return 6; }

    dateS = dateS.substr(loc+1);
    loc = dateS.find("-");
    if(loc == dateS.npos || loc+1 >= dateS.size()) {
        return 7;
    }
    std::string monthS = dateS.substr(0, loc);
    int month;
    try { month = std::stoi(monthS); } catch (const std::exception&) { return 8; }

    std::string dayS = dateS.substr(loc+1);
    int day;
    try { day = std::stoi(dayS); } catch (const std::exception&) { return 9; }

    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = 0;

    if (!validTm(tm)) { return 10; }

    *t = std::chrono::system_clock::from_time_t(tmToUtcTimeT(&tm));

    return 0;
}

struct Spot {
    std::string label;
    std::string spotter;
    double frequency;
    std::chrono::time_point<std::chrono::system_clock> spotTime;
    std::string comment;
    std::string location;
};

typedef void (*AddSpot)(const Spot&, void*, void*);

class SpotProvider {
public:
    virtual ~SpotProvider() = default;
    virtual void start() = 0;
    virtual void stop() = 0;

    void registerAddSpot(AddSpot a, void* sCtx, void* ctx) {
        addSpotCallback = a;
        addSpotSourceCtx = sCtx;
        addSpotCtx = ctx;
    }
    void registerAddSpot(void* sCtx) {
        // useful to re-register the sCtx which might have moved
        addSpotSourceCtx = sCtx;
    }
protected:
    void addSpot(const Spot& spot) {
        if (addSpotCallback)
            addSpotCallback(spot, addSpotSourceCtx, addSpotCtx);
    }
private:
    AddSpot addSpotCallback = nullptr;
    void* addSpotCtx = nullptr;
    void* addSpotSourceCtx = nullptr;
};

#endif //__SDRPP_SPOTS_MAIN_H
