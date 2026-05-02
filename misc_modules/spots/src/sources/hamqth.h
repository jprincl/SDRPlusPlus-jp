#ifndef __SDRPP_SPOTS_HAMQTH_H
#define __SDRPP_SPOTS_HAMQTH_H

#include "http_poller.h"

class HamQTHProvider : public HTTPPoller {
public:
    HamQTHProvider() {
        strcpy(url, "https://www.hamqth.com/dxc_csv.php?limit=200");
    }

protected:
    virtual void processResponse(std::string responseBody) {
        std::vector<std::string> lines = split(responseBody, '\n');
        for(const auto& line : lines) {
            std::vector<std::string> parts = split(line, '^');
            if(parts.size() < 10) {
                flog::error("got invalid response line from hamqth (parts length) {0}", line);
                break;
            }

            // frequency comes in kHz
            double frequency = std::stod(parts[1]);
            if(frequency <= 0) {
                flog::error("got invalid response line from hamqth (frequency) {0}", line);
                break;
            }
            frequency *= 1000;

            // time is HHMM YYYY-MM-DD
            std::chrono::time_point<std::chrono::system_clock> spotTime;
            if(parseTime(parts[4], &spotTime) != 0) {
                flog::error("got invalid response line from hamqth (spot time) {0}", line);
                break;
            }

            //everything is ok with input, we have a spot
            //lastUpdate = std::chrono::system_clock::now();

            std::string label = parts[2];
            std::string spotter = parts[0];
            std::string comment = parts[3];
            std::string location = parts[9];

            // the spot we'll insert into the list, even if it already
            // exists
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
    }
};

#endif //__SDRPP_SPOTS_HAMQTH_H
