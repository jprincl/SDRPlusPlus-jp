#ifndef __SDRPP_SPOTS_WWFF_H
#define __SDRPP_SPOTS_WWFF_H

#include "http_poller.h"

class WWFFProvider : public HTTPPoller {
public:
    WWFFProvider() {
        strcpy(url, "https://www.cqgma.org/api/spots/wwff/");
    }
protected:
    virtual void processResponse(std::string response) {
        json jsonSpots = json::parse(response)["RCD"];
        try {
            for(const auto& jsonSpot : jsonSpots.items()) {
                std::string label = jsonSpot.value().value("ACTIVATOR", "");
                std::transform(label.begin(), label.end(), label.begin(), ::toupper);
                std::string spotter = jsonSpot.value().value("SPOTTER", "");
                std::transform(spotter.begin(), spotter.end(), spotter.begin(), ::toupper);
                double frequency = std::stod(jsonSpot.value().value("QRG", "0"))*1000;
                std::tm t = {};
                int y,M,d,h,m,s;
                int dateValue = std::stoi(jsonSpot.value().value("DATE", "0"));
                int timeValue = std::stoi(jsonSpot.value().value("TIME", "0"));
                y = dateValue / 10000;
                M = dateValue / 100 % 100;
                d = dateValue % 100;
                h = timeValue / 100;
                m = timeValue % 100;
                s = 0;

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

                std::string comment = jsonSpot.value().value("TEXT", "");
                std::string location = jsonSpot.value().value("NAME", "");

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
            flog::error("error parsing wwff {0}", e.what());
        }
    }
};

#endif //__SDRPP_SPOTS_WWFF_H
