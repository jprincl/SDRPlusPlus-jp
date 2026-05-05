#ifndef __SDRPP_SPOTS_POTA_H
#define __SDRPP_SPOTS_POTA_H

#include "http_poller.h"

class POTAProvider : public HTTPPoller {
public:
    POTAProvider() {
        url = "https://api.pota.app/spot";
    }
protected:
    virtual void processResponse(std::string response) {
        json jsonSpots;
        try {
            jsonSpots = json::parse(response);
        } catch (const std::exception& e) {
            flog::error("error parsing pota.app json {0}", e.what());
            return;
        }
        for(const auto& jsonSpot : jsonSpots.items()) {
            try {
                std::string label = jsonSpot.value()["activator"];
                std::string spotter = jsonSpot.value()["spotter"];
                double frequency = std::stod(jsonSpot.value()["frequency"].get<std::string>())*1000;
                if (frequency <= 0) {
                    flog::error("pota.app: bad frequency {0}", frequency);
                    continue;
                }
                std::string spotTimeString = jsonSpot.value()["spotTime"].get<std::string>();
                std::chrono::time_point<std::chrono::system_clock> spotTime;
                if (!parseIso8601Utc(spotTimeString, &spotTime)) {
                    flog::error("pota.app: bad spotTime '{0}'", spotTimeString);
                    continue;
                }

                std::string comment = jsonSpot.value()["name"].get<std::string>()+" "+jsonSpot.value()["comments"].get<std::string>();
                std::string location = jsonSpot.value()["locationDesc"];

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
                flog::error("pota.app: skipping bad record: {0}", e.what());
            }
        }
    }
};

#endif //__SDRPP_SPOTS_POTA_H
