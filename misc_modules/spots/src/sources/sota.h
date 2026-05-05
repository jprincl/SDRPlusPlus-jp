#ifndef __SDRPP_SPOTS_SOTA_H
#define __SDRPP_SPOTS_SOTA_H

#include "http_poller.h"

class SOTAProvider : public HTTPPoller {
public:
    SOTAProvider() {
        url = "https://api2.sota.org.uk/api/spots/-2/all";
    }
protected:
    virtual void processResponse(std::string response) {
        json jsonSpots;
        try {
            jsonSpots = json::parse(response);
        } catch (const std::exception& e) {
            flog::error("error parsing sotawatch json {0}", e.what());
            return;
        }
        for(const auto& jsonSpot : jsonSpots.items()) {
            try {
                std::string label = jsonSpot.value()["activatorCallsign"];
                std::string spotter = jsonSpot.value()["callsign"];
                double frequency = std::stod(jsonSpot.value()["frequency"].get<std::string>())*1000*1000;
                std::string spotTimeString = jsonSpot.value()["timeStamp"].get<std::string>();
                std::chrono::time_point<std::chrono::system_clock> spotTime;
                if (!parseIso8601Utc(spotTimeString, &spotTime)) {
                    flog::error("sotawatch: bad timeStamp '{0}'", spotTimeString);
                    continue;
                }

                std::string comment;
                if (!jsonSpot.value()["comments"].is_null()) {
                    comment = jsonSpot.value()["comments"].get<std::string>();
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
            } catch (const std::exception& e) {
                flog::error("sotawatch: skipping bad record: {0}", e.what());
            }
        }
    }
};

#endif //__SDRPP_SPOTS_SOTA_H
