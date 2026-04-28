#pragma once

#include <functional>
#include <string>
#include <vector>

#include "kiwisdr_directory.h"
#include <gui/brown/geomap.h>

class ConfigManager;

struct KiwiSDRMapSelector {
    // How server markers are rendered on the map.
    enum class MarkerStyle {
        HideFull,        // original: full servers hidden, fill from SNR
        StatusColored,   // full = red, extended freq range = violet, else SNR
    };

    KiwiSDRMapSelector(const std::string& root, ConfigManager* config, const std::string& configPrefix);

    void openPopup();
    void drawPopup(std::function<void(const std::string&, const std::string&)> onSelected);

    // Runtime-toggleable. Default keeps the original behavior so existing
    // users see no surprise; a popup button cycles between styles.
    MarkerStyle markerStyle = MarkerStyle::HideFull;

private:
    bool shouldShowServer(const ServerEntry& server) const;
    void drawMarkers();
    void handleHitTest();
    void drawSelectionPanel();

    geomap::GeoMap geoMap;
    std::vector<ServerEntry> servers;
    bool serversReady = false;
    bool showExtApiOnly = false;
    bool mapPopupMaximized = false;
    bool mapPopupRestoreValid = false;
    ImVec2 mapPopupRestorePos = ImVec2(0.0f, 0.0f);
    ImVec2 mapPopupRestoreSize = ImVec2(0.0f, 0.0f);
    const std::string configPrefix;
    ConfigManager* config = nullptr;

    KiwiSDRDirectoryClient directory;
    KiwiSDRTester tester;
};
