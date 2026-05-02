#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <list>
#include <utils/freq_formatting.h>
#include <signal_path/signal_path.h>
#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <core.h>
#include <config.h>
#include "main.h"
#include "sources/hamqth.h"
#include "sources/pota.h"
#include "sources/sota.h"
#include "sources/wwff.h"
//#include "sources/server.h"
#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "spots",
    /* Description:     */ "Display spots on the band chart",
    /* Author:          */ "gerner",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

bool almost_equal(double a, double b, double epsilon=1e-3) {
    return abs(a-b) < epsilon;
}

/**********************************************
 * Two main functionalities:
 * 1. get/manage spots: frequency, label, spot time
 *    cop-out: just have a socket we listen for other stuff to shove in spots
 *    de-dup on label and update spot time
 *    "periodically" clean up "expired" spots
 * 2. draw spots: place on waterfall, similar to frequency_manager
 **********************************************/

std::string format_duration(std::chrono::system_clock::duration duration) {
    // 1:00
    const size_t bufsize=128;
    char buf[bufsize];
    int64_t sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    if(sec < 0) {
        strcpy(buf, "neg");
    } else if(sec < 60) {
        snprintf(buf, bufsize, "%ld sec", sec);
    } else if(sec < 3600) {
        snprintf(buf, bufsize, "%ld:%.2ld min", sec/60, sec%60);
    } else if(sec < 24*3600) {
        snprintf(buf, bufsize, "%ld:.2%ld hrs", sec/3600, sec%3600/60);
    } else {
        strcpy(buf, "days");
    }
    return std::string(buf);
}

struct SpotSource {
    SpotSource(std::string n, std::string l, bool e, ImU32 c) : name(n), label(l), enabled(e), color(c) {}
    SpotSource(std::string n, std::string l, bool e, ImU32 c, std::unique_ptr<SpotProvider> p, AddSpot a, void* ctx) : name(n), label(l), enabled(e), color(c), provider(std::move(p)) {
        provider->registerAddSpot(a, this, ctx);
    }
    SpotSource(SpotSource&& rhs) : name(rhs.name), label(rhs.label), enabled(rhs.enabled), color(rhs.color), provider(std::move(rhs.provider)) {
        // need to re-register as this since the old this is gone
        provider->registerAddSpot(this);
    }

    std::string name;
    std::string label;
    bool enabled;
    ImU32 color;
    std::unique_ptr<SpotProvider> provider;
};

struct WaterfallSpot {
    Spot spot;
    SpotSource* source;
};

// actual spots we're keeping track of
// info about how we draw spots on the waterfall so we can figure out clicks
struct WaterfallLabel {
    WaterfallSpot* spot;
    ImVec2 rectMin;
    ImVec2 rectMax;
};

ConfigManager config;

class SpotsModule : public ModuleManager::Instance {
public:
    SpotsModule(std::string name) {
        this->name = name;

        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name]["host"] = "localhost";
            config.conf[name]["port"] = 6214;
            config.conf[name]["autoStart"] = false;
            config.conf[name]["spotLifetime"] = 30;
            config.conf[name]["maxSpotLifetime"] = 240;
            config.conf[name]["sources"] = json();
        }

        // config initialization
        std::string hostname = config.conf[name]["host"];
        strcpy(host, hostname.c_str());
        port = config.conf[name]["port"];
        autoStart = config.conf[name]["autoStart"];
        spotLifetime = config.conf[name]["spotLifetime"];
        maxSpotLifetime = config.conf[name]["maxSpotLifetime"];
        config.release(true);

        fftRedrawHandler.ctx = this;
        fftRedrawHandler.handler = fftRedraw;
        inputHandler.ctx = this;
        inputHandler.handler = fftInput;

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        gui::waterfall.onFFTRedraw.bindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.bindHandler(&inputHandler);
    }

    ~SpotsModule() {
        stop();
        gui::menu.removeEntry(name);
        gui::waterfall.onFFTRedraw.unbindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.unbindHandler(&inputHandler);
    }

    void postInit() {
        ImU32 color;
        config.acquire();
        addSource("hamqth", "HamQTH ClusterDX", false, IM_COL32(0x9F, 0xBB, 0xCC, 255), std::make_unique<HamQTHProvider>());
        addSource("pota", "POTA.app spots", false, IM_COL32(0xCF, 0xFD, 0xBC, 255), std::make_unique<POTAProvider>());
        addSource("sota", "SOTAwatch spots", false, IM_COL32(0xF9, 0x57, 0x38, 255), std::make_unique<SOTAProvider>());
        addSource("wwff", "WWFF spots", false, IM_COL32(0x29, 0x73, 0x73, 255), std::make_unique<WWFFProvider>());
        config.release(true);
    }

    void start() {
        if (running) { return; }
        for (auto& source : spotSources) {
            if (source.enabled) {
                flog::info("starting provider {0}", source.name);
                source.provider->start();
            }
        }
        running = true;
    }

    void stop() {
        if (!running) { return; }
        for (auto& source : spotSources) {
            flog::info("stopping provider {0}", source.name);
            source.provider->stop();
        }
        running = false;
    }

    void enable() {
        enabled = true;
    }

    void disable() {
        stop();
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void menuHandler(void* ctx) {
        SpotsModule* _this = (SpotsModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        /*if (_this->running) { style::beginDisabled(); }
        if (ImGui::InputText(CONCAT("##_spots_host_", _this->name), _this->host, 1023)) {
            config.acquire();
            config.conf[_this->name]["host"] = std::string(_this->host);
            config.release(true);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(CONCAT("##_spots_port_", _this->name), &_this->port, 0, 0)) {
            config.acquire();
            config.conf[_this->name]["port"] = _this->port;
            config.release(true);
        }
        if (_this->running) { style::endDisabled(); }*/

        if (ImGui::Checkbox(CONCAT("Listen on startup##_spots_auto_lst_", _this->name), &_this->autoStart)) {
            config.acquire();
            config.conf[_this->name]["autoStart"] = _this->autoStart;
            config.release(true);
        }

        ImGui::LeftLabel("Spot Lifetime");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt(("##_spots_spotlifetime_" + _this->name).c_str(), &_this->spotLifetime, 1, _this->maxSpotLifetime)) {
            config.acquire();
            config.conf[_this->name]["spotLifetime"] = _this->spotLifetime;
            config.release(true);
        }

        // compute enable button size
        ImVec2 cellpad = ImGui::GetStyle().CellPadding;
        float lheight = ImGui::GetTextLineHeight();
        float cellWidth = lheight;// - (2.0f * cellpad.y);

        if (ImGui::BeginTable("Spots Source Table", 3)) {
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Color");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, cellWidth + cellpad.x);
            ImGui::TableSetupScrollFreeze(3, 1);
            ImGui::TableHeadersRow();

            for(auto& source : _this->spotSources) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(source.label.c_str());

                ImGui::TableSetColumnIndex(1);
                ImVec4 color = ImGui::ColorConvertU32ToFloat4(source.color);
                if (ImGui::ColorEdit4(CONCAT("##_spots_color_", source.name + _this->name), (float*)&color, ImGuiColorEditFlags_NoInputs)) {
                    source.color = ImGui::ColorConvertFloat4ToU32(color);
                    config.acquire();
                    config.conf[_this->name]["sources"][source.name]["color"] = source.color;
                    config.release(true);
                }

                ImGui::TableSetColumnIndex(2);
                if(ImGui::Checkbox(CONCAT("##_spots_", source.name + _this->name), &(source.enabled))) {
                    config.acquire();
                    config.conf[_this->name]["sources"][source.name]["enabled"] = source.enabled;
                    config.release(true);
                    if (source.enabled && _this->running) {
                        source.provider->start();
                    } else {
                        source.provider->stop();

                        // remove any spots from that source
                        std::lock_guard lk(_this->waterfallMutex);
                        for (auto it = _this->waterfallSpots.begin(); it != _this->waterfallSpots.end();) {
                            if (it->source == &source) {
                                it = _this->waterfallSpots.erase(it);
                            } else {
                                it++;
                            }
                        }
                    }
                }
            }
            ImGui::EndTable();
        }

        ImGui::FillWidth();

        //start/stop server
        ImGui::FillWidth();
        if (_this->running && ImGui::Button(CONCAT("Stop##_spots_stop_", _this->name), ImVec2(menuWidth, 0))) {
            _this->stop();
        }
        else if (!_this->running && ImGui::Button(CONCAT("Start##_spots_stop_", _this->name), ImVec2(menuWidth, 0))) {
            _this->start();
        }

        ImGui::TextUnformatted("Status:");
        ImGui::SameLine();

        if(_this->running) {
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Running");
        } else {
            ImGui::TextUnformatted("Idle");
        }
    }

    static void fftRedraw(ImGui::WaterFall::FFTRedrawArgs args, void* ctx) {
        SpotsModule* _this = (SpotsModule*)ctx;

        std::lock_guard lk(_this->waterfallMutex);
        auto expirationTime = std::chrono::system_clock::now() - std::chrono::minutes(_this->maxSpotLifetime);
        auto displayTime = std::chrono::system_clock::now() - std::chrono::minutes(_this->spotLifetime);

        std::vector<float> lanePositions;
        float laneHeight = ImGui::CalcTextSize("TEST").y + 2;
        int laneLimit = 8;
        _this->waterfallLabels.clear();
        double waterfallFreq = gui::waterfall.getCenterFrequency();
        waterfallFreq += sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
        for (auto it = _this->waterfallSpots.begin(); it != _this->waterfallSpots.end();) {

            // handle expiration of spots
            if(it->spot.spotTime < displayTime) {
                if(it->spot.spotTime < expirationTime) {
                    it = _this->waterfallSpots.erase(it);
                } else {
                    ++it;
                }
                continue;
            }

            // skip spots outside waterfall frequency range
            if (it->spot.frequency < args.lowFreq && it->spot.frequency < args.highFreq) {
                ++it;
                continue;
            }

            double centerXpos = args.min.x + std::round((it->spot.frequency - args.lowFreq) * args.freqToPixelRatio);

            ImVec2 nameSize = ImGui::CalcTextSize(it->spot.label.c_str());
            float leftEdge = centerXpos - (nameSize.x/2) - 5;
            float rightEdge = centerXpos + (nameSize.x/2) + 5;

            // choose a "lane" for the label to go in
            // highest lane that it'll fit
            // if none, add a lane
            float targetY = -1;
            int i = 0;
            for(auto laneIt = lanePositions.begin(); laneIt != lanePositions.end(); laneIt++) {
                if(leftEdge - 2 >= *laneIt) {
                    *laneIt = rightEdge;
                    targetY = args.min.y + i * laneHeight;
                    break;
                }
                i++;
            }
            if(targetY < 0) {
                if(i < laneLimit) {
                    targetY = args.min.y + i * laneHeight;
                    lanePositions.push_back(rightEdge);
                } else {
                    // sorry, no space
                    ++it;
                    continue;
                }
            }

            ImU32 bgColor = it->source->color;

            if (it->spot.frequency >= args.lowFreq && it->spot.frequency <= args.highFreq) {
                args.window->DrawList->AddLine(ImVec2(centerXpos, targetY), ImVec2(centerXpos, args.max.y), bgColor);
            }

            ImVec2 rectMin = ImVec2(centerXpos - (nameSize.x / 2) - 5, targetY);
            ImVec2 rectMax = ImVec2(centerXpos + (nameSize.x / 2) + 5, targetY + nameSize.y);
            ImVec2 clampedRectMin = ImVec2(std::clamp<double>(rectMin.x, args.min.x, args.max.x), rectMin.y);
            ImVec2 clampedRectMax = ImVec2(std::clamp<double>(rectMax.x, args.min.x, args.max.x), rectMax.y);

            if (clampedRectMax.x - clampedRectMin.x > 0) {
                _this->waterfallLabels.push_back({&(*it), rectMin, rectMax});
                if (almost_equal(waterfallFreq, it->spot.frequency)) {
                    args.window->DrawList->AddRectFilledMultiColor(clampedRectMin, clampedRectMax, bgColor, bgColor, _this->spotBgColorSelected, bgColor);
                } else {
                    args.window->DrawList->AddRectFilled(clampedRectMin, clampedRectMax, bgColor);
                }
                args.window->DrawList->AddText(ImVec2(centerXpos - (nameSize.x / 2), targetY), _this->spotTextColor, it->spot.label.c_str());
            }

            // make sure to get the next element in the spot list!
            ++it;
        }
    }

    // stuff to check if we click on a label on the waterfall
    // inspired by freuqency_manager module
    bool mouseAlreadyDown = false;
    bool mouseClickedInLabel = false;
    static void fftInput(ImGui::WaterFall::InputHandlerArgs args, void* ctx) {
        SpotsModule* _this = (SpotsModule*)ctx;
        if (_this->mouseClickedInLabel) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                _this->mouseClickedInLabel = false;
            }
            gui::waterfall.inputHandled = true;
            return;
        }

        // First check that the mouse clicked outside of any label. Also get the bookmark that's hovered
        bool inALabel = false;
        WaterfallLabel hoveredLabel;

        for(auto label : _this->waterfallLabels) {
            ImVec2 clampedRectMin = ImVec2(std::clamp<double>(label.rectMin.x, args.fftRectMin.x, args.fftRectMax.x), label.rectMin.y);
            ImVec2 clampedRectMax = ImVec2(std::clamp<double>(label.rectMax.x, args.fftRectMin.x, args.fftRectMax.x), label.rectMax.y);

            if (ImGui::IsMouseHoveringRect(clampedRectMin, clampedRectMax)) {
                inALabel = true;
                hoveredLabel = label;
                break;
            }
        }

        // Check if mouse was already down
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inALabel) {
            _this->mouseAlreadyDown = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            _this->mouseAlreadyDown = false;
            _this->mouseClickedInLabel = false;
        }

        // If yes, cancel
        if (_this->mouseAlreadyDown || !inALabel) { return; }

        gui::waterfall.inputHandled = true;

        ImVec2 clampedRectMin = ImVec2(std::clamp<double>(hoveredLabel.rectMin.x, args.fftRectMin.x, args.fftRectMax.x), hoveredLabel.rectMin.y);
        ImVec2 clampedRectMax = ImVec2(std::clamp<double>(hoveredLabel.rectMax.x, args.fftRectMin.x, args.fftRectMax.x), hoveredLabel.rectMax.y);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _this->mouseClickedInLabel = true;
            tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, hoveredLabel.spot->spot.frequency);
        }

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(hoveredLabel.spot->spot.label.c_str());
        ImGui::Separator();
        ImGui::Text("Frequency: %s", utils::formatFreq(hoveredLabel.spot->spot.frequency).c_str());
        ImGui::Text("Location: %s", hoveredLabel.spot->spot.location.c_str());
        ImGui::Text("Spotter: %s", hoveredLabel.spot->spot.spotter.c_str());
        std::string lastSpotted = format_duration(std::chrono::system_clock::now() - hoveredLabel.spot->spot.spotTime) + " ago";
        ImGui::Text("Last spotted: %s", lastSpotted.c_str());
        ImGui::Text("Comment: %s", hoveredLabel.spot->spot.comment.c_str());
        ImGui::EndTooltip();
    }

    static void addSpot(Spot providedSpot, void* sourceCtx, void* ctx) {
        SpotSource* source = (SpotSource*) sourceCtx;
        SpotsModule* _this = (SpotsModule*) ctx;
        std::lock_guard lk(_this->waterfallMutex);

        if(providedSpot.spotTime < std::chrono::system_clock::now() - std::chrono::minutes(_this->maxSpotLifetime)) {
            // silently drop already expired spots
            return;
        }
        WaterfallSpot spot = {providedSpot, source};
        // find a spot with a matching label (callsign)
        // we'll re-add it to the list in case the frequency changed
        // so waterfallSpots always stays in frequency order
        auto it = std::find_if(
                _this->waterfallSpots.begin(),
                _this->waterfallSpots.end(),
                [&spot](const WaterfallSpot& s) { return s.spot.label == spot.spot.label; }
        );
        if (it != _this->waterfallSpots.end()) {
            if(it->spot.spotTime > spot.spot.spotTime) {
                // more recent spot takes precedence
                spot = *it;
            }
            _this->waterfallSpots.erase(it);
        }
        _this->waterfallSpots.insert(
            std::lower_bound(
                _this->waterfallSpots.begin(),
                _this->waterfallSpots.end(),
                spot.spot.frequency,
                [](const WaterfallSpot &lhs, double f) { return lhs.spot.frequency < f; }
            ),
            spot
        );
    }

    void addSource(std::string sourceName, std::string label, bool defaultEnabled, ImU32 defaultColor, std::unique_ptr<SpotProvider>&& provider) {
        flog::info("initializing source {0}", sourceName);
        if (!config.conf[name]["sources"].contains(sourceName)) {
            config.conf[name]["sources"][sourceName] = json(json::value_t::object);
        }
        json sourceConf = config.conf[name]["sources"][sourceName];

        flog::info("getting params");
        ImU32 color = sourceConf.value("color", defaultColor);
        sourceConf["color"] = color;
        bool enabled = sourceConf.value("enabled", defaultEnabled);
        sourceConf["enabled"] = enabled;

        flog::info("emplacing");
        spotSources.emplace_back(sourceName, label, enabled, color,
                std::move(provider), &SpotsModule::addSpot, this);
    }


    char host[1024];
    int port = 6214;

    std::string name;
    bool enabled = true;
    bool running = false;

    int spotLifetime = 30; // don't display stuff older than this in minutes
    int maxSpotLifetime = 240; // drop spots older than this in minutes
    ImU32 spotBgColor = IM_COL32(0xCF, 0xFD, 0xBC ,255);
    ImU32 spotBgColorSelected = IM_COL32(0xFB, 0xAF, 0x00, 255);
    ImU32 spotTextColor = IM_COL32(0, 0, 0, 255);

    bool autoStart = false;

    EventHandler<ImGui::WaterFall::FFTRedrawArgs> fftRedrawHandler;
    EventHandler<ImGui::WaterFall::InputHandlerArgs> inputHandler;

    std::vector<SpotSource> spotSources;

    std::list<WaterfallSpot> waterfallSpots;
    std::list<WaterfallLabel> waterfallLabels;
    std::mutex waterfallMutex;
};

MOD_EXPORT void _INIT_() {
    config.setPath(core::args["root"].s() + "/spots_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SpotsModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (SpotsModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
