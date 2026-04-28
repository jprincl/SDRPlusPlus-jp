
#define IMGUI_DEFINE_MATH_OPERATORS
#include <gui/brown/geomap.h>
#include <gui/brown/earcut.hpp>
#include <cmath>
#include <ctime>
#include <fstream>
#include <core.h>
#include <utils/flog.h>
#include <gui/widgets/simple_widgets.h>
#include <filesystem>
#include <map>
#include <random>
#include <utility>
#include <imgui/imgui_internal.h>

// earcut adapter so the triangulator can read x/y from CartesianCoordinates.
namespace mapbox { namespace util {
    template <std::size_t I> struct nth<I, geomap::CartesianCoordinates> {
        static auto get(const geomap::CartesianCoordinates& t) {
            if constexpr (I == 0) return t.x;
            else return t.y;
        }
    };
}}

namespace geomap {


    // Converts degrees to radians

    nlohmann::json geoJSON;
    // [country][polygon] → border + triangles. Triangulated once at load.
    std::vector<std::vector<Polygon>> countriesGeo;
    std::vector<std::string> countryNames;

    // Day/night terminator overlay. Recomputed roughly once per minute from
    // the current UTC time; rendered as a translucent dark fill over the
    // night side of the world.
    Polygon terminatorPolygon;
    std::tm  terminatorPolygonTime{};

    static std::tm getTimeUtc() {
        std::time_t now = std::time(nullptr);
        std::tm utc{};
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        return utc;
    }

    // Day-of-year (1..366); used by the solar-declination approximation.
    static int dayOfYear(int year, int month, int day) {
        static const int monthDays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        int doy = 0;
        for (int m = 0; m < month - 1; ++m) doy += monthDays[m];
        doy += day;
        if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) doy += 1;
        return doy;
    }

    // Solar declination in radians (Spencer 1971 approximation).
    static double solarDeclination(int year, int month, int day) {
        const int N = dayOfYear(year, month, day);
        const double gamma = 2.0 * pi / 365.0 * (N - 1);
        return 0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma)
             - 0.006758 * std::cos(2 * gamma) + 0.000907 * std::sin(2 * gamma)
             - 0.002697 * std::cos(3 * gamma) + 0.001480 * std::sin(3 * gamma);
    }

    // Builds the closed polygon describing the night side of the planet for
    // the given UTC time: the terminator curve in longitude steps, closed
    // along whichever pole is in shadow.
    static std::vector<GeoCoordinates> buildNightPolygon(const std::tm& utc) {
        std::vector<GeoCoordinates> polygon;
        const double decl = solarDeclination(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
        const int lonStep = 2;
        const double utcHours = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;

        for (int lon = -180; lon <= 180; lon += lonStep) {
            const double lst = utcHours + lon / 15.0;
            const double H = degToRad((lst - 12.0) * 15.0);
            double lat;
            if (std::fabs(std::cos(decl)) < 1e-6) {
                lat = (decl > 0) ? 90.0 : -90.0;
            }
            else {
                lat = radToDeg(std::atan(-std::cos(H) / std::tan(decl)));
            }
            if (lat > 90.0) lat = 90.0;
            if (lat < -90.0) lat = -90.0;
            polygon.push_back({ lat, double(lon) });
        }
        // Close along the pole that is currently in shadow.
        const bool northInShadow = !(decl > 0);
        const double poleLat = northInShadow ? 90.0 : -90.0;
        polygon.push_back({ poleLat,  180.0 });
        polygon.push_back({ poleLat, -180.0 });
        polygon.push_back(polygon.front());
        return polygon;
    }

    static void updateTerminatorPolygon(const std::tm& utc) {
        const auto geo = buildNightPolygon(utc);
        Polygon poly;
        poly.border.reserve(geo.size());
        for (const auto& g : geo) {
            poly.border.emplace_back(geoToCartesian(g));
        }
        if (poly.border.size() >= 3) {
            std::vector<std::vector<CartesianCoordinates>> rings = { poly.border };
            poly.triangles = mapbox::earcut<uint32_t>(rings);
        }
        terminatorPolygon = std::move(poly);
        terminatorPolygonTime = utc;
    }

    static void checkTerminatorPolygon() {
        const std::tm utc = getTimeUtc();
        const std::tm& old = terminatorPolygonTime;
        // Update once per minute — the terminator moves at ~0.25°/min so
        // sub-minute updates would be wasted recomputation.
        const bool sameMinute =
            utc.tm_year == old.tm_year && utc.tm_mon == old.tm_mon && utc.tm_mday == old.tm_mday &&
            utc.tm_hour == old.tm_hour && utc.tm_min == old.tm_min;
        if (sameMinute && !terminatorPolygon.border.empty()) {
            return;
        }
        updateTerminatorPolygon(utc);
    }

    nlohmann::json readGeoJSONFile(const std::string& filePath) {
        std::ifstream fileStream(filePath);

        if (!fileStream.is_open()) {
            flog::error("Failed to open the file {}", filePath);
            return nullptr;
        }

        nlohmann::json geoJSON;
        fileStream >> geoJSON;

        return geoJSON;
    }

    static bool pointInPolygon(const CartesianCoordinates& p, const std::vector<CartesianCoordinates>& polygon) {
        bool inside = false;
        const size_t count = polygon.size();
        if (count < 3) {
            return false;
        }
        for (size_t i = 0, j = count - 1; i < count; j = i++) {
            const auto& a = polygon[i];
            const auto& b = polygon[j];
            const bool crosses = ((a.y > p.y) != (b.y > p.y)) &&
                (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x);
            if (crosses) {
                inside = !inside;
            }
        }
        return inside;
    }

    void maybeInit() {
        if (geoJSON.empty()) {
            std::string resDir = core::configManager.conf["resourcesDirectory"];
            const std::string filePath = resDir + "/cty/map.json";
            geoJSON = readGeoJSONFile(filePath);


            for (const auto& feature : geoJSON["features"]) {
                countriesGeo.emplace_back();
                std::string countryName;
                if (feature.contains("properties") && feature["properties"].contains("name")) {
                    countryName = feature["properties"]["name"].get<std::string>();
                }
                countryNames.push_back(std::move(countryName));

                for (const auto& coordinates : feature["geometry"]["coordinates"]) {
                    countriesGeo.back().emplace_back();
                    Polygon& dest = countriesGeo.back().back();
                    for (const auto& coord0 : coordinates) {
                        if (coord0.is_array() && !coord0.empty() && coord0[0].is_array()) {
                            // MultiPolygon-style: coord0 is a ring of [lon,lat] pairs
                            for (const auto& coord1 : coord0) {
                                double longitude = coord1[0].get<double>();
                                double latitude = coord1[1].get<double>();
                                dest.border.emplace_back(geoToCartesian({ latitude, longitude }));
                            }
                        }
                        else if (coord0.is_array() && !coord0.empty() && !coord0[0].is_array() && coord0.size() == 2) {
                            // Polygon-style: coordinates is the ring directly; iterate the parent
                            for (const auto& coord1 : coordinates) {
                                double longitude = coord1[0].get<double>();
                                double latitude = coord1[1].get<double>();
                                dest.border.emplace_back(geoToCartesian({ latitude, longitude }));
                            }
                            break;
                        }
                        else {
                            break;
                        }
                    }
                    // Triangulate the just-built polygon. Earcut wants outer
                    // ring + optional holes; we don't model holes here, so
                    // pass a single-element rings list.
                    if (dest.border.size() >= 3) {
                        std::vector<std::vector<CartesianCoordinates>> rings = { dest.border };
                        dest.triangles = mapbox::earcut<uint32_t>(rings);
                    }
                }
            }
        }
    }

    // Create a color map to store a color for each country
    std::map<std::string, ImVec4> countryColors;

    ImVec4 randomColor() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0, 1);

        return ImVec4(dis(gen), dis(gen), dis(gen), 1.0f);
    }

    std::vector<ImVec4> colors = {
        ImVec4(1.0f, 0.0f, 0.0f, 1.0f),   // Colors.red
        ImVec4(0.0f, 1.0f, 0.0f, 1.0f),   // Colors.green
        ImVec4(1.0f, 0.08f, 0.58f, 1.0f), // Colors.pink
        ImVec4(1.0f, 0.76f, 0.03f, 1.0f), // Colors.amber
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),   // Colors.grey
        ImVec4(0.6f, 0.32f, 0.17f, 1.0f), // Colors.brown
        ImVec4(1.0f, 0.65f, 0.0f, 1.0f),  // Colors.orange
        ImVec4(0.56f, 0.93f, 0.56f, 1.0f) // Colors.lightGreen
    };


    void GeoMap::draw(const char* extraButtonLabel, std::function<void()> extraButtonAction) {

        maybeInit();

        const ImVec2 curpos = ImGui::GetCursorPos();


        ImDrawList* drawList = ImGui::GetWindowDrawList();
        recentCanvasPos = ImGui::GetCursorScreenPos();
        auto windowWidth = ImGui::GetContentRegionAvail().x;
        auto windowHeight = ImGui::GetContentRegionAvail().y;
        ImVec2 w2 = ImVec2(windowWidth / 2, windowHeight / 2);
        auto toView = [=](ImVec2 c) {
            c *= scale;
            auto x1 = static_cast<float>(c.x * w2.x + w2.x);
            auto y1 = static_cast<float>(windowHeight - (c.y * w2.y + w2.y));
            return ImVec2(x1, y1) + translate * w2 * scale;
        };
        auto toMap = [=](ImVec2 p) {
            return CartesianCoordinates{
                (p.x - w2.x) / (w2.x * scale.x) - translate.x,
                (w2.y - p.y) / (w2.y * scale.y) + translate.y
            };
        };
        recentMapToScreen = toView;

        if (windowWidth == 0) {
            return;
        }
        int count = 0;
        const bool mapHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const CartesianCoordinates mouseMapPos = toMap(ImGui::GetMousePos() - recentCanvasPos);
        const std::string* hoveredCountry = nullptr;

        drawList->AddRectFilled(recentCanvasPos + ImVec2(0, 0), recentCanvasPos + ImGui::GetContentRegionAvail(), ImColor(0, 0, 0));

        // Disable AA fill while triangles are rasterized: each
        // AddTriangleFilled() with AA on emits a fringe along its edges,
        // and at every interior triangulation edge the two adjacent
        // triangles' fringes overlap, double-blending the translucent
        // fill into a visible seam. AA lines stay enabled, so AddLine()
        // outlines below remain smooth.
        const auto savedDrawFlags = drawList->Flags;
        drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

        for (size_t countryIndex = 0; countryIndex < countriesGeo.size(); countryIndex++) {
            auto& country = countriesGeo[countryIndex];
            const ImColor lineColor = ImColor(colors[(++count) % colors.size()]);
            // Fill is the same hue as the outline but heavily diluted so the
            // map stays readable under a busy marker layer.
            const ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(
                ImVec4(lineColor.Value.x, lineColor.Value.y, lineColor.Value.z, 0.18f));
            const ImU32 lineCol = ImColor(lineColor);
            for (auto& polygon : country) {
                // Filled triangles first, outline on top.
                for (size_t t = 0; t + 2 < polygon.triangles.size(); t += 3) {
                    const auto& a = polygon.border[polygon.triangles[t]];
                    const auto& b = polygon.border[polygon.triangles[t + 1]];
                    const auto& c = polygon.border[polygon.triangles[t + 2]];
                    drawList->AddTriangleFilled(
                        recentCanvasPos + toView(a.toImVec2()),
                        recentCanvasPos + toView(b.toImVec2()),
                        recentCanvasPos + toView(c.toImVec2()),
                        fillCol);
                }
                if (polygon.border.size() > 1) {
                    if (mapHovered && !hoveredCountry && countryIndex < countryNames.size() &&
                        !countryNames[countryIndex].empty() && pointInPolygon(mouseMapPos, polygon.border)) {
                        hoveredCountry = &countryNames[countryIndex];
                    }
                    for (size_t i = 0; i + 1 < polygon.border.size(); i++) {
                        drawList->AddLine(
                            recentCanvasPos + toView(polygon.border[i].toImVec2()),
                            recentCanvasPos + toView(polygon.border[i + 1].toImVec2()),
                            lineCol, 1.0f);
                    }
                }
            }
        }

        // Day/night terminator: shade the night side. Drawn after countries
        // so it tints the land on the dark side of the planet.
        checkTerminatorPolygon();
        const ImU32 nightFill = ImColor(0, 0, 0, 110);
        for (size_t t = 0; t + 2 < terminatorPolygon.triangles.size(); t += 3) {
            const auto& a = terminatorPolygon.border[terminatorPolygon.triangles[t]];
            const auto& b = terminatorPolygon.border[terminatorPolygon.triangles[t + 1]];
            const auto& c = terminatorPolygon.border[terminatorPolygon.triangles[t + 2]];
            drawList->AddTriangleFilled(
                recentCanvasPos + toView(a.toImVec2()),
                recentCanvasPos + toView(b.toImVec2()),
                recentCanvasPos + toView(c.toImVec2()),
                nightFill);
        }
        drawList->Flags = savedDrawFlags;

        if (hoveredCountry) {
            const GeoCoordinates geoPos = cartesianToGeo(mouseMapPos);
            const std::string tooltip = *hoveredCountry + "\n" + geoToMaidenhead(geoPos) + "\n" + geoPos.toString();
            ImGui::SetTooltip("%s", tooltip.c_str());
        }

        if (ImGui::IsMouseDown(0)) {
            if (!initialTouchPos) {
                initialTouchPos = std::make_shared<ImVec2>(ImGui::GetMousePos());
                initialTranslate = translate;
            }
            translate = initialTranslate + (ImGui::GetMousePos() - *initialTouchPos) / w2 / scale;
            scaleTranslateDirty = true;
        }
        else {
            initialTouchPos.reset();
        }

        ImGui::SetCursorPos(curpos);
        if (doFingerButton("Zoom In##geomap-zoom-in")) {
            scale = scale * 2;
            scaleTranslateDirty = true;
        }
        ImGui::SameLine();
        if (doFingerButton("Zoom Out##geomap-zoom-out")) {
            scale = scale / 2;
            scaleTranslateDirty = true;
        }
        ImGui::SameLine();
        if (doFingerButton("Reset Map##reset-map")) {
            scale = ImVec2(1.0, 1.0);
            translate = ImVec2(0.0, 0.0);
            scaleTranslateDirty = true;
        }
        if (extraButtonLabel && extraButtonAction) {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
            if (doFingerButton(extraButtonLabel)) {
                extraButtonAction();
            }
        }

    }
    void GeoMap::saveTo(ConfigManager& manager, const char* prefix){
        auto pref = std::string(prefix);
        manager.acquire();
        manager.conf[pref+"_scale_x"] = scale.x;
        manager.conf[pref+"_scale_y"] = scale.y;
        manager.conf[pref+"_translate_x"] = translate.x;
        manager.conf[pref+"_translate_y"] = translate.y;
        manager.release(true);
    };

    void GeoMap::loadFrom(ConfigManager& manager, const char* prefix) {
        auto pref = std::string(prefix);
        manager.acquire();
        if (manager.conf.contains(pref+"_scale_x")) {
            scale.x = manager.conf[pref + "_scale_x"];
            scale.y = manager.conf[pref + "_scale_y"];
            translate.x = manager.conf[pref + "_translate_x"];
            translate.y = manager.conf[pref + "_translate_y"];
        }
        manager.release(false);
    };
};
