#ifndef __SDRPP_SPOTS_MAIN_H
#define __SDRPP_SPOTS_MAIN_H

#include <string>
#include <chrono>
#include <vector>

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss (s);
    std::string item;

    while (getline (ss, item, delim)) {
        result.push_back (item);
    }

    return result;
}

int parseTime(const std::string &s, std::chrono::time_point<std::chrono::system_clock>* t) {
    std::tm tm{};
    // HHMM YYYY-mm-dd
    size_t loc = s.find(" ");
    if(loc == s.npos || loc+1 >= s.size()) {
        return 1;
    }
    std::string timeS = s.substr(0, loc);
    int time = std::stoi(timeS);
    if(time < 0) {
        return 2;
    }
    tm.tm_sec = 0;
    tm.tm_min = time % 100;
    if(tm.tm_min >= 60) {
        return 3;
    }
    tm.tm_hour = time / 100;
    if(tm.tm_hour >= 24) {
        return 4;
    }

    std::string dateS = s.substr(loc+1);
    loc = dateS.find("-");
    if(loc == s.npos || loc+1 >= dateS.size()) {
        return 5;
    }
    std::string yearS = dateS.substr(0, loc);
    int year = std::stoi(yearS);
    if(year < 0) {
        return 6;
    }
    dateS = dateS.substr(loc+1);
    loc = dateS.find("-");
    if(loc == s.npos || loc+1 >= dateS.size()) {
        return 7;
    }
    std::string monthS = dateS.substr(0, loc);
    int month = std::stoi(monthS);
    if(month < 1 || month > 12) {
        return 8;
    }
    std::string dayS = dateS.substr(loc+1);
    int day = std::stoi(dayS);
    if(day < 1 || day > 31) {
        return 9;
    }

    tm.tm_year = year - 1900;
    tm.tm_mon = month-1; // Jan = 0
    tm.tm_mday = day;
    tm.tm_isdst = 0;

    // from https://stackoverflow.com/a/38298359
    std::time_t tLocal = std::mktime(&tm);
    time_t tUTC = tLocal + (std::mktime(std::localtime(&tLocal)) - std::mktime(std::gmtime(&tLocal)));
    *t = std::chrono::system_clock::from_time_t(tUTC);

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

typedef void (*AddSpot)(Spot, void*, void*);

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
    void addSpot(Spot spot) {
        addSpotCallback(spot, addSpotSourceCtx, addSpotCtx);
    }
private:
    AddSpot addSpotCallback;
    void* addSpotCtx;
    void* addSpotSourceCtx;
};

#endif //__SDRPP_SPOTS_MAIN_H
