#ifndef __SDRPP_SPOTS_SOTA_H
#define __SDRPP_SPOTS_SOTA_H

#include "http_poller.h"

class SOTAProvider : public HTTPPoller {
public:
    SOTAProvider() {
        strcpy(url, "https://api2.sota.org.uk/api/spots/-2/all");
    }
protected:
    virtual void processResponse(std::string response) {
        json jsonSpots = json::parse(response);
        try {
            for(const auto& jsonSpot : jsonSpots.items()) {
                std::string label = jsonSpot.value()["activatorCallsign"];
                std::string spotter = jsonSpot.value()["callsign"];
                double frequency = std::stod(jsonSpot.value()["frequency"].get<std::string>())*1000*1000;
                std::string spotTimeString = jsonSpot.value()["timeStamp"].get<std::string>();
                std::tm t = {};
                int y,M,d,h,m;
                float s;
                sscanf(spotTimeString.c_str(), "%d-%d-%dT%d:%d:%f", &y, &M, &d, &h, &m, &s);
                std::tm time = { 0 };
                time.tm_year = y - 1900; // Year since 1900
                time.tm_mon = M - 1;     // 0-11
                time.tm_mday = d;        // 1-31
                time.tm_hour = h;        // 0-23
                time.tm_min = m;         // 0-59
                time.tm_sec = (int)s;    // 0-61 (0-60 in C++11)
                // expressed in UTC
                // from https://stackoverflow.com/a/38298359
                std::time_t tLocal = std::mktime(&time);
                time_t tUTC = tLocal + (std::mktime(std::localtime(&tLocal)) - std::mktime(std::gmtime(&tLocal)));
                std::chrono::time_point<std::chrono::system_clock> spotTime = std::chrono::system_clock::from_time_t(tUTC);

                std::string comment = "";
                if (!jsonSpot.value()["comments"].is_null()) {
                    comment = jsonSpot.value()["comments"].get<std::string>()+" "+jsonSpot.value()["comments"].get<std::string>();
                }
                std::string location = jsonSpot.value()["summitDetails"];

                Spot spot = {
                    label,
                    spotter,
                    frequency,
                    spotTime,
                    comment,
                    location
                };
                addSpot(spot);
            }
        } catch (const json::type_error& e) {
            flog::error("error parsing sotawatch {0}", e.what());
        }
    }
};

#endif //__SDRPP_SPOTS_SOTA_H
