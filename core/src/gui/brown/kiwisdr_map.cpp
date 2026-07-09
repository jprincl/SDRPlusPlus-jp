#define IMGUI_DEFINE_MATH_OPERATORS
#ifdef _WIN32
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#endif

#include "kiwisdr_map.h"

#include <cmath>
#include <sstream>

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

    bool shouldUseFullscreenPopup(ImVec2 displaySize) {
        const float fontSize = ImGui::GetFontSize();
        return displaySize.x < fontSize * 56.0f || displaySize.y < fontSize * 36.0f;
    }

    ImVec2 desktopPopupMinSize(ImVec2 displaySize) {
        const float fontSize = ImGui::GetFontSize();
        return ImVec2(
            ImMin(displaySize.x, ImMax(320.0f, fontSize * 24.0f)),
            ImMin(displaySize.y, ImMax(240.0f, fontSize * 18.0f))
        );
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

void KiwiSDRMapSelector::drawPopup(std::function<void(const std::string&, const std::string&, const std::optional<ServerEntry::FrequencyBand>&)> onSelected) {
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
#ifdef __ANDROID__
    const bool desktopPopup = false;
    const bool fullscreenPopup = shouldUseFullscreenPopup(displaySize);
#else
    const bool desktopPopup = true;
    const bool fullscreenPopup = false;
#endif
    const std::string popupTitle = configPrefix + ": The KiwiSDR Map";
    const bool lockedFullscreenPopup = fullscreenPopup || (desktopPopup && mapPopupMaximized);
    const ImVec2 popupPos = lockedFullscreenPopup ? ImVec2(0.0f, 0.0f) : displaySize * 0.125f;
    const ImVec2 popupSize = lockedFullscreenPopup ? displaySize : displaySize * 0.75f;
    ImGuiWindowFlags popupFlags = 0;
    if (!desktopPopup) {
        popupFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    }
    if (lockedFullscreenPopup) {
        popupFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    }
    if (fullscreenPopup) {
        popupFlags |= ImGuiWindowFlags_NoTitleBar;
    }

    if (desktopPopup && !mapPopupMaximized) {
        ImGui::SetNextWindowSizeConstraints(desktopPopupMinSize(displaySize), displaySize);
    }

    const ImGuiCond popupPlacementCond = lockedFullscreenPopup ? ImGuiCond_Always : (desktopPopup ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
    ImGui::SetNextWindowPos(popupPos, popupPlacementCond);
    ImGui::SetNextWindowSize(popupSize, popupPlacementCond);
    if (!ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, popupFlags)) {
        return;
    }

    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float footerHeight = getFingerButtonHeight() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##geomap-kiwisdr", ImVec2(contentSize.x, ImMax(1.0f, contentSize.y - footerHeight)), true, 0);
    const char* filterButtonLabel = showExtApiOnly ? "Show all stations" : "Show EXT API only";
    // Bump button alpha for the whole map control row (Zoom/Reset/filter
    // submitted inside geoMap.draw, plus the "Hide full" button on the
    // same line below) — those buttons sit over the map and the default
    // theme alpha makes them hard to read against the country fills.
    pushOverlayButtonStyle();
    // Markers go through geoMap.draw's overlay slot so they paint above
    // the country fills but below the Zoom/Reset/filter button row in
    // the same child window. Drawing them after geoMap.draw returns
    // would put marker rectangles on top of the buttons.
    geoMap.draw(
        filterButtonLabel,
        [this]() { showExtApiOnly = !showExtApiOnly; },
        [this]() {
            if (serversReady) drawMarkers();
        });
    if (geoMap.scaleTranslateDirty) {
        geoMap.saveTo(*config, configPrefix.c_str());
        geoMap.scaleTranslateDirty = false;
    }

    // Marker rendering toggle. Label shows the action — clicking switches
    // to the other mode.
    ImGui::SameLine();
    const char* markerStyleLabel = (markerStyle == MarkerStyle::StatusColored)
        ? "Hide full servers" : "Show full as red";
    if (doFingerButton(markerStyleLabel)) {
        markerStyle = (markerStyle == MarkerStyle::StatusColored)
            ? MarkerStyle::HideFull : MarkerStyle::StatusColored;
    }
    popOverlayButtonStyle();

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
        // Markers are drawn from inside geoMap.draw via the overlay slot
        // so they sit under the button row. handleHitTest is input-only
        // and runs here.
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

    if (desktopPopup) {
        if (doFingerButton(mapPopupMaximized ? "Restore" : "Maximize")) {
            if (mapPopupMaximized) {
                mapPopupMaximized = false;
                if (mapPopupRestoreValid) {
                    ImGui::SetWindowPos(mapPopupRestorePos, ImGuiCond_Always);
                    ImGui::SetWindowSize(mapPopupRestoreSize, ImGuiCond_Always);
                }
            }
            else {
                mapPopupRestorePos = ImGui::GetWindowPos();
                mapPopupRestoreSize = ImGui::GetWindowSize();
                mapPopupRestoreValid = true;
                mapPopupMaximized = true;
                ImGui::SetWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
                ImGui::SetWindowSize(displaySize, ImGuiCond_Always);
            }
        }
        ImGui::SameLine();
    }
    if (doFingerButton("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    if (auto ok = tester.lastOk()) {
        ImGui::SameLine();
        if (doFingerButton("Use tested server: " + ok->hostPort)) {
            onSelected(ok->hostPort, ok->loc, ok->band);
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
#ifndef __ANDROID__
    // Hover-tooltip is desktop-only: on a touch screen the cursor only
    // exists while a finger is down, the finger occludes the tooltip,
    // and the tap is already routed to the click-to-select handler.
    // Touch users discover server details through the selection panel.
    //
    // Gate: cursor in this child window AND not over a registered widget.
    // Without the !IsAnyItemHovered() check, the marker tooltip would
    // show through the Zoom/Reset/filter buttons sitting in the same
    // child window.
    const bool windowHovered = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
    const ImVec2 mousePos = ImGui::GetMousePos();
    const ServerEntry* hovered = nullptr;
#endif

    for (const auto& s : servers) {
        if (!shouldShowServer(s)) continue;
        const bool isFull = s.users >= s.usersmax;
        if (markerStyle == MarkerStyle::HideFull && isFull) continue;

        // Fill: SNR-based by default; in StatusColored mode, extended-freq
        // servers (>32 MHz, i.e. usable for VHF/FM) are violet, and full
        // servers are red — overrides applied in priority order.
        ImColor fill = markerFillForSnr(s.maxSnr);
        if (markerStyle == MarkerStyle::StatusColored) {
            if (s.band && s.band->endHz > 32000000ull) {
                fill = ImColor(0.6f, 0.4f, 1.0f);
            }
            if (isFull) {
                fill = ImColor(0.8f, 0.0f, 0.0f);
            }
        }

        const auto dest = geoMap.recentMapToScreen(s.gps);
        const auto p0 = geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2);
        const auto p1 = geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2);
        drawList->AddRectFilled(p0, p1, fill, sz / 4.0f);
        if (s.selected) {
            drawList->AddRect(p0, p1, ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
            drawList->AddRect(p0 + ImVec2(1, 1), p1 - ImVec2(1, 1), ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
        }
        else {
            drawList->AddRect(p0, p1, ImColor(0.0f, 0.0f, 0.0f), sz / 4.0f);
        }
#ifndef __ANDROID__
        // Track the topmost (last-drawn) marker under the cursor so the
        // tooltip below picks the same server the click handler would.
        if (windowHovered &&
            mousePos.x >= p0.x && mousePos.x <= p1.x &&
            mousePos.y >= p0.y && mousePos.y <= p1.y) {
            hovered = &s;
        }
#endif
    }

#ifndef __ANDROID__
    if (hovered) {
        // Brief tooltip — full details are in the selection panel after click.
        std::ostringstream ss;
        ss << hovered->name << "\n"
           << "Loc: " << hovered->loc;
        if (hovered->band) {
            ss << "\nBand: " << hovered->band->startHz << "-" << hovered->band->endHz << " Hz";
        }
        ImGui::SetTooltip("%s", ss.str().c_str());
    }
#endif
}

void KiwiSDRMapSelector::handleHitTest() {
    if (!(ImGui::IsMouseClicked(0) && ImGui::GetMouseClickedCount(0) == 1)) return;
    // IsMouseClicked is a global query — without this gate, clicking the
    // Zoom/Reset/filter buttons sitting in the same child window would
    // also pick whatever marker happens to lie under the button rect.
    // AllowWhenBlockedByActiveItem is required because the geomap's drag
    // handler (running in geoMap.draw() just before us) takes ActiveID on
    // this same click frame; without the flag, IsWindowHovered would be
    // suppressed and we'd never pick anything. The IsAnyItemHovered check
    // still rejects clicks that landed on a real widget.
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
        ImGui::IsAnyItemHovered()) return;
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
        // doOverlayText puts a translucent dark backdrop behind each label
        // so the text stays legible over the colored country fills.
        doOverlayText("%s", s.name.c_str());
        doOverlayText("%s", s.loc.c_str());
        if (s.band) {
            doOverlayText("Band: %llu-%llu Hz",
                          (unsigned long long)s.band->startHz,
                          (unsigned long long)s.band->endHz);
        }
        if (!s.antenna.empty()) {
            doOverlayText("ANT: %s", s.antenna.c_str());
        }
        if (!s.sdrHardware.empty()) {
            doOverlayText("HW: %s", s.sdrHardware.c_str());
        }
        if (!s.swVersion.empty()) {
            doOverlayText("VER: %s", s.swVersion.c_str());
        }
        if (!s.qth.empty()) {
            doOverlayText("QTH: %s", s.qth.c_str());
        }
        if (s.maxSnr > 0) {
            doOverlayText("SNR: %d", (int)s.maxSnr);
        }
        if (s.usersmax > 0) {
            doOverlayText("USR: %d/%d", s.users, s.usersmax);
        }
        doOverlayText("EXT API: %d", s.extApi);
        doOverlayText("URL: %s", s.url.c_str());

        ImGui::BeginDisabled(tester.isInProgress());
        const bool doTest = doFingerButton("Test server");
        ImGui::EndDisabled();
        if (doTest) {
            tester.start(s.url, s.loc, s.band);
        }
    }
}
