#define IMGUI_DEFINE_MATH_OPERATORS
#ifdef _WIN32
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#endif

#include "kiwisdr_map.h"

#include <cmath>

#include <core.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <gui/style.h>
#include "gui/widgets/simple_widgets.h"


namespace {

    ImColor markerFillForSnr(float maxSnr) {
        if (maxSnr > 22) return ImColor(0.0f, 1.0f, 0.0f);
        if (maxSnr > 12) return ImColor(0.6f, 0.6f, 0.6f);
        return ImColor(0.3f, 0.3f, 0.3f);
    }

}


KiwiSDRMapSelector::KiwiSDRMapSelector(const std::string& root, ConfigManager* config, const std::string& configPrefix)
    : configPrefix(configPrefix), config(config), directory(root) {
    json def = json({});
    config->load(def);
    geoMap.loadFrom(*config, configPrefix.c_str()); // configPrefix is like "mapselector1_"
}

void KiwiSDRMapSelector::openPopup() {
    ImGui::OpenPopup((configPrefix + ": The KiwiSDR Map").c_str());
}

void KiwiSDRMapSelector::drawPopup(std::function<void(const std::string&, const std::string&)> onSelected) {
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.125f);
    if (!ImGui::BeginPopupModal((configPrefix + ": The KiwiSDR Map").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const ImVec2 ws = ImGui::GetIO().DisplaySize * 0.75f;
    ImGui::SetWindowSize(ws);
    ImGui::BeginChild("##geomap-kiwisdr", ws - ImVec2(0, 50), true, 0);
    geoMap.draw();
    if (geoMap.scaleTranslateDirty) {
        geoMap.saveTo(*config, configPrefix.c_str());
        geoMap.scaleTranslateDirty = false;
    }

    directory.requestRefresh();
    if (auto fresh = directory.takeIfReady()) {
        servers = std::move(*fresh);
        serversReady = true;
    }

    const std::string error = directory.errorMessage();
    if (!serversReady) {
        if (!error.empty()) {
            ImGui::Text("%s", error.c_str());
        }
        else {
            ImGui::Text("Loading KiwiSDR servers list..");
        }
    }
    else {
        ImGui::Text("Loaded servers list");
        ImGui::SameLine();
        if (doFingerButton(showExtApiOnly ? "Show all stations" : "Show EXT API only")) {
            showExtApiOnly = !showExtApiOnly;
        }
        drawMarkers();
        handleHitTest();
        drawSelectionPanel();

        const std::string testStatus = tester.statusText();
        const std::string testError = tester.errorText();
        if (!testStatus.empty()) {
            ImGui::Text("%s", testStatus.c_str());
        }
        if (!testError.empty()) {
            ImGui::Text("Server test error: %s", testError.c_str());
        }
    }

    ImGui::EndChild();

    if (doFingerButton("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    if (auto ok = tester.lastOk()) {
        ImGui::SameLine();
        if (doFingerButton("Use tested server: " + ok->hostPort)) {
            onSelected(ok->hostPort, ok->loc);
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

bool KiwiSDRMapSelector::shouldShowServer(const ServerEntry& server) const {
    return !showExtApiOnly || server.extApi > 0;
}

void KiwiSDRMapSelector::drawMarkers() {
    const auto sz = style::baseFont->FontSize;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const auto& s : servers) {
        if (!shouldShowServer(s) || s.users >= s.usersmax) continue;
        const auto dest = geoMap.recentMapToScreen(s.gps);
        const auto p0 = geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2);
        const auto p1 = geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2);
        drawList->AddRectFilled(p0, p1, markerFillForSnr(s.maxSnr), sz / 4.0f);
        if (s.selected) {
            drawList->AddRect(p0, p1, ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
            drawList->AddRect(p0 + ImVec2(1, 1), p1 - ImVec2(1, 1), ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
        }
        else {
            drawList->AddRect(p0, p1, ImColor(0.0f, 0.0f, 0.0f), sz / 4.0f);
        }
    }
}

void KiwiSDRMapSelector::handleHitTest() {
    if (!(ImGui::IsMouseClicked(0) && ImGui::GetMouseClickedCount(0) == 1)) return;
    const auto sz = style::baseFont->FontSize;
    const auto clickPos = ImGui::GetMousePos() - geoMap.recentCanvasPos;
    auto radius = sz / 2;
    for (int q = 0; q < 2; q++) {
        bool found = false;
        for (int i = (int)servers.size() - 1; i >= 0; i--) {
            auto& it = servers[i];
            if (!shouldShowServer(it)) continue;
            const auto dest = geoMap.recentMapToScreen(it.gps);
            const auto dist = std::sqrt(std::pow(dest.x - clickPos.x, 2) + std::pow(dest.y - clickPos.y, 2));
            if (dist < radius) {
                found = true;
                for (auto& s : servers) s.selected = false;
                auto chosen = it;
                chosen.selected = true;
                servers.emplace_back(chosen);
                servers.erase(servers.begin() + i);
                break;
            }
        }
        if (found) break;
        radius *= 5;
    }
}

void KiwiSDRMapSelector::drawSelectionPanel() {
    for (const auto& s : servers) {
        if (!s.selected || !shouldShowServer(s)) continue;
        ImGui::Text("%s", s.name.c_str());
        ImGui::Text("%s", s.loc.c_str());
        if (s.band) {
            const std::string bandText = "Band: " + std::to_string(s.band->startHz) + "-" + std::to_string(s.band->endHz) + " Hz";
            ImGui::Text("%s", bandText.c_str());
        }
        if (!s.antenna.empty()) {
            ImGui::Text("ANT: %s", s.antenna.c_str());
        }
        if (s.maxSnr > 0) {
            ImGui::Text("SNR: %d", (int)s.maxSnr);
        }
        if (s.usersmax > 0) {
            ImGui::Text("USR: %d/%d", s.users, s.usersmax);
        }
        ImGui::Text("EXT API: %d", s.extApi);
        ImGui::Text("URL: %s", s.url.c_str());

        ImGui::BeginDisabled(tester.isInProgress());
        const bool doTest = doFingerButton("Test server");
        ImGui::EndDisabled();
        if (doTest) {
            tester.start(s.url, s.loc, s.band);
        }
    }
}
