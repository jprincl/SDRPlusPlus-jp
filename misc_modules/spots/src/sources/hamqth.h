#ifndef __SDRPP_SPOTS_HAMQTH_H
#define __SDRPP_SPOTS_HAMQTH_H

#include "http_poller.h"

class HamQTHProvider : public HTTPPoller {
public:
    HamQTHProvider() {
        url = "https://www.hamqth.com/dxc_csv.php?limit=200";
    }

protected:
    virtual void processResponse(std::string responseBody) {
        std::vector<std::string> lines = split(responseBody, '\n');
        for(const auto& line : lines) {
            try {
                std::vector<std::string> parts = split(line, '^');
                if(parts.size() < 10) {
                    flog::error("got invalid response line from hamqth (parts length) {0}", line);
                    continue;
                }

                // frequency comes in kHz
                double frequency = std::stod(parts[1]);
                if(frequency <= 0) {
                    flog::error("got invalid response line from hamqth (frequency) {0}", line);
                    continue;
                }
                frequency *= 1000;

                // time is HHMM YYYY-MM-DD
                std::chrono::time_point<std::chrono::system_clock> spotTime;
                if(parseTime(parts[4], &spotTime) != 0) {
                    flog::error("got invalid response line from hamqth (spot time) {0}", line);
                    continue;
                }

                std::string label = parts[2];
                std::string spotter = parts[0];
                std::string comment = parts[3];
                std::string location = parts[9];

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
                flog::error("hamqth: skipping bad line '{0}': {1}", line, e.what());
            }
        }
    }
};

#endif //__SDRPP_SPOTS_HAMQTH_H
