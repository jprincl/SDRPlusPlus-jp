#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/tuner.h>
#include <core.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <utils/freq_formatting.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "entry.h"
#include "database.h"

SDRPP_MOD_INFO{
    /* Name:            */ "freq_info",
    /* Description:     */ "Station identification from imported schedule databases (EiBi, ...)",
    /* Author:          */ "jprincl",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

ConfigManager config;

class ListenInfoModule : public ModuleManager::Instance {
public:
    ListenInfoModule(std::string name) {
        this->name = name;

        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name]["eibiPath"] = "";
            config.conf[name]["showMarkers"] = true;
            config.conf[name]["toleranceHz"] = 1000.0;
            config.conf[name]["targetArea"] = "";
        }
        auto& instanceConf = config.conf[name];
        eibiPath = instanceConf.value("eibiPath", std::string(""));
        showMarkers = instanceConf.value("showMarkers", true);
        toleranceHz = instanceConf.value("toleranceHz", 1000.0);
        targetArea = instanceConf.value("targetArea", std::string(""));
        instanceConf["eibiPath"] = eibiPath;
        instanceConf["showMarkers"] = showMarkers;
        instanceConf["toleranceHz"] = toleranceHz;
        instanceConf["targetArea"] = targetArea;
        config.release(true);

        strncpy(eibiPathBuf, eibiPath.c_str(), sizeof(eibiPathBuf) - 1);
        eibiPathBuf[sizeof(eibiPathBuf) - 1] = 0;
        strncpy(targetAreaBuf, targetArea.c_str(), sizeof(targetAreaBuf) - 1);
        targetAreaBuf[sizeof(targetAreaBuf) - 1] = 0;
        toleranceHzF = (float)toleranceHz;

        if (!eibiPath.empty()) { reload(); }

        fftRedrawHandler.ctx = this;
        fftRedrawHandler.handler = fftRedraw;
        inputHandler.ctx = this;
        inputHandler.handler = fftInput;

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        gui::waterfall.onFFTRedraw.bindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.bindHandler(&inputHandler);
    }

    ~ListenInfoModule() {
        gui::waterfall.onFFTRedraw.unbindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.unbindHandler(&inputHandler);
        gui::menu.removeEntry(name);
    }

    void postInit() {}
    void enable() { enabled = true; }
    void disable() { enabled = false; }
    bool isEnabled() { return enabled; }

private:
    void reload() {
        std::lock_guard<std::mutex> lk(dataMutex);
        db.loadEibi(eibiPath);
        lastTunedFreq = -1; // force the "now tuned" block to refresh next redraw
        // flog output isn't visible without logcat/adb access, so surface
        // a failed/empty load on screen instead of only in the log.
        lastLoadError = (db.size() == 0) ? "No entries loaded — check the file path" : "";
    }

    static bool almostEqual(double a, double b, double eps = 1.0) {
        return std::abs(a - b) < eps;
    }

    // --- drawing + querying, runs every waterfall redraw regardless of showMarkers ---
    static void fftRedraw(ImGui::WaterFall::FFTRedrawArgs args, void* ctx) {
        ListenInfoModule* _this = (ListenInfoModule*)ctx;
        auto now = std::chrono::system_clock::now();

        double curFreq = (gui::waterfall.selectedVFO == "")
            ? gui::waterfall.getCenterFrequency()
            : gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);

        std::lock_guard<std::mutex> lk(_this->dataMutex);

        // Always kept current — this is what backs the "entries in view"
        // panel table whether or not markers are drawn.
        _this->viewEntries = _this->db.queryRange(args.lowFreq, args.highFreq, now);

        // Only requery the tuned-frequency block when the frequency actually
        // moved, not on every redraw.
        if (!almostEqual(curFreq, _this->lastTunedFreq)) {
            _this->lastTunedFreq = curFreq;
            _this->tunedEntries = _this->db.queryFrequency(curFreq, _this->toleranceHz, now, _this->targetArea);
        }

        // showMarkers only gates the drawing below — querying above always runs.
        _this->waterfallLabels.clear();
        if (!_this->showMarkers) { return; }

        std::vector<float> lanePositions; // right edge of the last label placed in each lane
        const int laneLimit = 6;
        const float laneHeight = ImGui::GetTextLineHeight() + 4;
        const ImU32 tunedBg = IM_COL32(0xCF, 0xFD, 0xBC, 255);
        const ImU32 tunedText = IM_COL32(0, 0, 0, 255);
        const ImU32 dimBg = IM_COL32(140, 140, 140, 90);
        const ImU32 dimText = IM_COL32(210, 210, 210, 190);

        // Entries within tolerance of the tuned frequency go first, so they
        // claim lanes before anything else — the tuned station should never
        // be the one that gets dropped when a frequency is crowded.
        std::vector<const ListenInfoEntry*> drawOrder = _this->viewEntries;
        std::stable_partition(drawOrder.begin(), drawOrder.end(),
            [_this](const ListenInfoEntry* e) {
                return std::abs(e->frequency - _this->lastTunedFreq) <= _this->toleranceHz;
            });

        int hiddenForSpace = 0;

        for (const ListenInfoEntry* ePtr : drawOrder) {
            const ListenInfoEntry& e = *ePtr;
            bool isTuned = std::abs(e.frequency - _this->lastTunedFreq) <= _this->toleranceHz;
            ImU32 bgColor = isTuned ? tunedBg : dimBg;
            ImU32 textColor = isTuned ? tunedText : dimText;

            double centerXpos = args.min.x + std::round((e.frequency - args.lowFreq) * args.freqToPixelRatio);

            ImVec2 nameSize = ImGui::CalcTextSize(e.name.c_str());
            float leftEdge = (float)centerXpos - (nameSize.x / 2) - 5;
            float rightEdge = (float)centerXpos + (nameSize.x / 2) + 5;

            float targetY = -1;
            int lane = 0;
            for (auto laneIt = lanePositions.begin(); laneIt != lanePositions.end(); ++laneIt, ++lane) {
                if (leftEdge - 2 >= *laneIt) {
                    *laneIt = rightEdge;
                    targetY = args.min.y + lane * laneHeight;
                    break;
                }
            }
            if (targetY < 0) {
                if (lane >= laneLimit) { hiddenForSpace++; continue; } // no room this frame; still in viewEntries/panel
                targetY = args.min.y + lane * laneHeight;
                lanePositions.push_back(rightEdge);
            }

            // Unclamped rect is what we store for hit-testing (mirrors the
            // pattern used elsewhere in this codebase); clamped copy is
            // only used for the actual draw calls below.
            ImVec2 rectMin((float)centerXpos - nameSize.x / 2 - 5, targetY);
            ImVec2 rectMax((float)centerXpos + nameSize.x / 2 + 5, targetY + nameSize.y);
            ImVec2 clampedMin(std::clamp(rectMin.x, args.min.x, args.max.x), rectMin.y);
            ImVec2 clampedMax(std::clamp(rectMax.x, args.min.x, args.max.x), rectMax.y);

            if (clampedMax.x - clampedMin.x <= 0) { continue; }

            args.window->DrawList->AddLine(ImVec2((float)centerXpos, targetY), ImVec2((float)centerXpos, args.max.y), bgColor);
            args.window->DrawList->AddRectFilled(clampedMin, clampedMax, bgColor);
            args.window->DrawList->AddText(ImVec2((float)centerXpos - nameSize.x / 2, targetY), textColor, e.name.c_str());

            _this->waterfallLabels.push_back({e, bgColor, rectMin, rectMax});
        }

        // Nothing should vanish silently — if lanes ran out, say so instead
        // of just dropping entries with no on-screen trace. Full list is
        // always in the panel regardless.
        if (hiddenForSpace > 0) {
            char hint[64];
            snprintf(hint, sizeof(hint), "+%d more (see panel)", hiddenForSpace);
            ImVec2 hintSize = ImGui::CalcTextSize(hint);
            ImVec2 hintPos(args.max.x - hintSize.x - 6, args.min.y + 2);
            args.window->DrawList->AddRectFilled(hintPos, ImVec2(hintPos.x + hintSize.x + 4, hintPos.y + hintSize.y + 2), IM_COL32(0, 0, 0, 160));
            args.window->DrawList->AddText(ImVec2(hintPos.x + 2, hintPos.y + 1), IM_COL32(255, 255, 255, 255), hint);
        }
    }

    // --- hover tooltip + click-to-tune, mirrors the hit-testing pattern used elsewhere in this codebase ---
    bool mouseAlreadyDown = false;
    bool mouseClickedInLabel = false;

    static void fftInput(ImGui::WaterFall::InputHandlerArgs args, void* ctx) {
        ListenInfoModule* _this = (ListenInfoModule*)ctx;
        if (_this->mouseClickedInLabel) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { _this->mouseClickedInLabel = false; }
            gui::waterfall.inputHandled = true;
            return;
        }

        bool inALabel = false;
        WaterfallListenInfoLabel hovered;
        ImVec2 clampedMin, clampedMax;

        {
            std::lock_guard<std::mutex> lk(_this->dataMutex);
            for (const auto& label : _this->waterfallLabels) {
                ImVec2 cMin(std::clamp<double>(label.rectMin.x, args.fftRectMin.x, args.fftRectMax.x), label.rectMin.y);
                ImVec2 cMax(std::clamp<double>(label.rectMax.x, args.fftRectMin.x, args.fftRectMax.x), label.rectMax.y);
                if (ImGui::IsMouseHoveringRect(cMin, cMax)) {
                    inALabel = true;
                    hovered = label;
                    clampedMin = cMin;
                    clampedMax = cMax;
                    break;
                }
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inALabel) {
            _this->mouseAlreadyDown = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            _this->mouseAlreadyDown = false;
            _this->mouseClickedInLabel = false;
        }

        if (_this->mouseAlreadyDown || !inALabel) { return; }

        gui::waterfall.inputHandled = true;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _this->mouseClickedInLabel = true;
            tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, hovered.entry.frequency);
        }

        drawTooltip(hovered.entry);
    }

    static void drawTooltip(const ListenInfoEntry& e) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(e.name.c_str());
        ImGui::Separator();
        ImGui::Text("Frequency: %s", utils::formatFreq(e.frequency).c_str());
        ImGui::Text("Target: %s   Country: %s", e.targetArea.c_str(), e.ituCountry.c_str());
        ImGui::Text("Language: %s", e.language.c_str());
        ImGui::Text("Time: %04d-%04d UTC", e.startTime, e.endTime);
        if (!e.daysKnown) {
            ImGui::Text("Schedule: %s (irregular, shown daily)", e.scheduleRaw.c_str());
        }
        ImGui::EndTooltip();
    }

    // --- module panel ---
    static void menuHandler(void* ctx) {
        ListenInfoModule* _this = (ListenInfoModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        ImGui::LeftLabel("EiBi file path");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        ImGui::InputText(("##_li_eibipath_" + _this->name).c_str(), _this->eibiPathBuf, sizeof(_this->eibiPathBuf));

        if (ImGui::Button(("Load / Reimport##_li_load_" + _this->name).c_str(), ImVec2(menuWidth, 0))) {
            _this->eibiPath = _this->eibiPathBuf;
            config.acquire();
            config.conf[_this->name]["eibiPath"] = _this->eibiPath;
            config.release(true);
            _this->reload();
        }

        if (ImGui::Checkbox(("Show markers on waterfall##_li_show_" + _this->name).c_str(), &_this->showMarkers)) {
            config.acquire();
            config.conf[_this->name]["showMarkers"] = _this->showMarkers;
            config.release(true);
        }

        ImGui::LeftLabel("Match tolerance (Hz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat(("##_li_tol_" + _this->name).c_str(), &_this->toleranceHzF, 100.0f, 10000.0f, "%.0f")) {
            _this->toleranceHz = _this->toleranceHzF;
            config.acquire();
            config.conf[_this->name]["toleranceHz"] = _this->toleranceHz;
            config.release(true);
        }

        ImGui::LeftLabel("Preferred target area");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputText(("##_li_target_" + _this->name).c_str(), _this->targetAreaBuf, sizeof(_this->targetAreaBuf))) {
            _this->targetArea = _this->targetAreaBuf;
            config.acquire();
            config.conf[_this->name]["targetArea"] = _this->targetArea;
            config.release(true);
        }

        ImGui::Text("Database: %zu entries", _this->db.size());
        if (!_this->lastLoadError.empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", _this->lastLoadError.c_str());
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Now tuned:");
        {
            std::lock_guard<std::mutex> lk(_this->dataMutex);
            if (_this->tunedEntries.empty()) {
                ImGui::TextDisabled("(no match)");
            }
            for (const ListenInfoEntry* ePtr : _this->tunedEntries) {
                const ListenInfoEntry& e = *ePtr;
                ImGui::BulletText("%s (%s, %s)", e.name.c_str(), e.targetArea.c_str(), e.language.c_str());
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Entries in view:");
        {
            std::lock_guard<std::mutex> lk(_this->dataMutex);
            if (ImGui::BeginTable(("##_li_view_table_" + _this->name).c_str(), 4,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                    ImVec2(0, 200))) {
                ImGui::TableSetupColumn("Freq");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Target");
                ImGui::TableSetupColumn("Time (UTC)");
                ImGui::TableHeadersRow();
                for (const ListenInfoEntry* ePtr : _this->viewEntries) {
                    const ListenInfoEntry& e = *ePtr;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(utils::formatFreq(e.frequency).c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(e.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                        tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, e.frequency);
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.targetArea.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%04d-%04d", e.startTime, e.endTime);
                }
                ImGui::EndTable();
            }
        }
    }

    std::string name;
    bool enabled = true;

    ListenInfoDatabase db;
    std::string eibiPath;
    char eibiPathBuf[1024] = {0};
    std::string lastLoadError;
    bool showMarkers = true;
    double toleranceHz = 1000.0;
    float toleranceHzF = 1000.0f;
    std::string targetArea;
    char targetAreaBuf[16] = {0};

    double lastTunedFreq = -1;
    std::vector<const ListenInfoEntry*> viewEntries;
    std::vector<const ListenInfoEntry*> tunedEntries;
    std::vector<WaterfallListenInfoLabel> waterfallLabels;
    std::mutex dataMutex;

    EventHandler<ImGui::WaterFall::FFTRedrawArgs> fftRedrawHandler;
    EventHandler<ImGui::WaterFall::InputHandlerArgs> inputHandler;
};

MOD_EXPORT void _INIT_() {
    config.setPath(core::args["root"].s() + "/freq_info_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new ListenInfoModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (ListenInfoModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
