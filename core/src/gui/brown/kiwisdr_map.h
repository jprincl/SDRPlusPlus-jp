#pragma once

#include <functional>
#include <string>
#include <vector>

#include "kiwisdr_directory.h"
#include <gui/brown/geomap.h>

class ConfigManager;

struct KiwiSDRMapSelector {
    KiwiSDRMapSelector(const std::string& root, ConfigManager* config, const std::string& configPrefix);

    void openPopup();
    void drawPopup(std::function<void(const std::string&, const std::string&)> onSelected);

private:
    bool shouldShowServer(const ServerEntry& server) const;
    void drawMarkers();
    void handleHitTest();
    void drawSelectionPanel();

    geomap::GeoMap geoMap;
    std::vector<ServerEntry> servers;
    bool serversReady = false;
    bool showExtApiOnly = false;
    const std::string configPrefix;
    ConfigManager* config = nullptr;

    KiwiSDRDirectoryClient directory;
    KiwiSDRTester tester;
};
