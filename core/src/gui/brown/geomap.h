#pragma once
#ifdef _WIN32
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#endif

#include <json.hpp>
#include <imgui/imgui.h>
#include <stdint.h>
#include "config.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using nlohmann::json;

namespace geomap {

    constexpr double pi = 3.14159265358979323846;
    constexpr double earthRadius = 6371.0; // Earth radius in kilometers

    inline double degToRad(double deg) {
        return deg * pi / 180.0;
    }
    inline double radToDeg(double rad) {
        return rad * 180.0 / pi;
    }

    struct GeoCoordinates {
        double latitude;
        double longitude;
    };

    struct CartesianCoordinates {
        double x;
        double y;

        ImVec2 toImVec2() const {
            return ImVec2(static_cast<float>(x), static_cast<float>(y));
        }
    };

    // One polygon's border plus its earcut-triangulation indices. Used so
    // we can render filled country shapes (and the day/night terminator)
    // instead of bare outlines. `triangles` is empty for polygons too small
    // (< 3 vertices) or where triangulation failed; in that case only the
    // outline is rendered. Indices are into `border`, three per triangle.
    struct Polygon {
        std::vector<CartesianCoordinates> border;
        std::vector<uint32_t> triangles;
    };


    inline CartesianCoordinates geoToCartesian(const GeoCoordinates& geo) {
        double latRad = degToRad(geo.latitude);
        double lngRad = degToRad(geo.longitude);

        double x = lngRad / pi;
        double y = latRad / (pi / 2.0);

        return { x, y };
    }

    // Maidenhead 6-character grid square (e.g. "JN78dq") for the given
    // coordinates. Used by ham operators as a compact location identifier.
    // Returns the empty string if the coordinates are out of range.
    inline std::string geoToMaidenhead(const GeoCoordinates& geo) {
        if (geo.latitude < -90.0 || geo.latitude > 90.0 ||
            geo.longitude < -180.0 || geo.longitude > 180.0) {
            return {};
        }
        double lon = geo.longitude + 180.0;  // 0..360
        double lat = geo.latitude  + 90.0;   // 0..180

        const int lonField = std::min(17, (int)(lon / 20.0));
        const int latField = std::min(17, (int)(lat / 10.0));
        const int lonSquare = (int)((lon - lonField * 20.0) / 2.0);
        const int latSquare = (int)(lat - latField * 10.0);
        const int lonSub = (int)((lon - lonField * 20.0 - lonSquare * 2.0) * 12.0);
        const int latSub = (int)((lat - latField * 10.0 - latSquare * 1.0) * 24.0);

        char buf[7];
        std::snprintf(buf, sizeof buf, "%c%c%d%d%c%c",
                      'A' + lonField,
                      'A' + latField,
                      lonSquare,
                      latSquare,
                      'a' + lonSub,
                      'a' + latSub);
        return std::string(buf);
    }


    struct GeoMap {

        ImVec2 scale = ImVec2(1.0, 1.0);
        ImVec2 translate = ImVec2(0.0, 0.0); // in map coordinates
        bool scaleTranslateDirty = false;

        std::shared_ptr<ImVec2> initialTouchPos;
        ImVec2 initialTranslate;

        std::function <ImVec2(ImVec2)> recentMapToScreen;
        ImVec2 recentCanvasPos;

        void draw(const char* extraButtonLabel = nullptr, std::function<void()> extraButtonAction = {});
        void saveTo(ConfigManager &manager, const char* string);
        void loadFrom(ConfigManager& manager, const char* prefix);
    };

};
