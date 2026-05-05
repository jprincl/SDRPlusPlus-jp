#ifndef __SDRPP_SPOTS_WWFF_H
#define __SDRPP_SPOTS_WWFF_H

#include <algorithm>
#include <cctype>

#include "http_poller.h"

class WWFFProvider : public HTTPPoller {
public:
    WWFFProvider() {
        url = "https://www.cqgma.org/api/spots/wwff/";
    }
protected:
    virtual void processResponse(std::string response) {
        json jsonSpots;
        try {
            jsonSpots = json::parse(response)["RCD"];
        } catch (const std::exception& e) {
            flog::error("error parsing wwff json {0}", e.what());
            return;
        }
        for(const auto& jsonSpot : jsonSpots.items()) {
            try {
                std::string label = jsonSpot.value().value("ACTIVATOR", "");
                std::transform(label.begin(), label.end(), label.begin(), ::toupper);
                std::string spotter = jsonSpot.value().value("SPOTTER", "");
                std::transform(spotter.begin(), spotter.end(), spotter.begin(), ::toupper);
                double frequency = std::stod(jsonSpot.value().value("QRG", "0"))*1000;
                if (frequency <= 0) {
                    flog::error("wwff: bad QRG {0}", jsonSpot.value().value("QRG", ""));
                    continue;
                }
                int dateValue = std::stoi(jsonSpot.value().value("DATE", "0"));
                int timeValue = std::stoi(jsonSpot.value().value("TIME", "0"));
                int y = dateValue / 10000;
                int M = dateValue / 100 % 100;
                int d = dateValue % 100;
                int h = timeValue / 100;
                int m = timeValue % 100;

                std::tm time = {};
                time.tm_year = y - 1900;
                time.tm_mon = M - 1;
                time.tm_mday = d;
                time.tm_hour = h;
                time.tm_min = m;
                time.tm_sec = 0;
                if (!validTm(time)) {
                    flog::error("wwff: bad DATE/TIME {0}/{1}", dateValue, timeValue);
                    continue;
                }
                std::chrono::time_point<std::chrono::system_clock> spotTime =
                    std::chrono::system_clock::from_time_t(tmToUtcTimeT(&time));

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
            } catch (const std::exception& e) {
                flog::error("wwff: skipping bad record: {0}", e.what());
            }
        }
    }
};

#endif //__SDRPP_SPOTS_WWFF_H
