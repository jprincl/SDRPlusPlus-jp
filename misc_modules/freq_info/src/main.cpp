#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/tuner.h>
#include <core.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "entry.h"
#include "database.h"

// core's utils::formatFreq() deliberately trims trailing zeros (e.g.
// 12.040 MHz -> "12.04MHz") — sensible for a live tuning readout, but not
// for a table column where entries should line up at a consistent
// precision. Kept local to freq_info rather than touching the shared
// core helper, since other UI in the app genuinely wants the trimmed form.
// Precision per band matches the source data's own granularity: EiBi kHz
// values go to at most one decimal (e.g. "5855.6"), so kHz is shown with
// 1 decimal; MHz is shown with 3 (kHz resolution) per Jan's request.
static std::string formatFreqFixed(double freqHz) {
    char buf[64];
    if (freqHz >= 1000000.0) {
        snprintf(buf, sizeof(buf), "%.3f MHz", freqHz / 1000000.0);
    }
    else if (freqHz >= 1000.0) {
        snprintf(buf, sizeof(buf), "%.1f kHz", freqHz / 1000.0);
    }
    else {
        snprintf(buf, sizeof(buf), "%.0f Hz", freqHz);
    }
    return std::string(buf);
}

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
            config.conf[name]["onlyTunedMarkers"] = false;
            config.conf[name]["entriesWindowOpen"] = false;
            config.conf[name]["toleranceHz"] = 1000.0;
            config.conf[name]["targetArea"] = "";
            config.conf[name]["maxRows"] = 6;
            config.conf[name]["topOffsetPx"] = 0.0f;
        }
        auto& instanceConf = config.conf[name];
        eibiPath = instanceConf.value("eibiPath", std::string(""));
        showMarkers = instanceConf.value("showMarkers", true);
        onlyTunedMarkers = instanceConf.value("onlyTunedMarkers", false);
        entriesWindowOpen = instanceConf.value("entriesWindowOpen", false);
        toleranceHz = instanceConf.value("toleranceHz", 1000.0);
        targetArea = instanceConf.value("targetArea", std::string(""));
        maxRows = instanceConf.value("maxRows", 6);
        topOffsetPx = instanceConf.value("topOffsetPx", 0.0f);
        instanceConf["eibiPath"] = eibiPath;
        instanceConf["showMarkers"] = showMarkers;
        instanceConf["onlyTunedMarkers"] = onlyTunedMarkers;
        instanceConf["entriesWindowOpen"] = entriesWindowOpen;
        instanceConf["toleranceHz"] = toleranceHz;
        instanceConf["targetArea"] = targetArea;
        instanceConf["maxRows"] = maxRows;
        instanceConf["topOffsetPx"] = topOffsetPx;
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

            // A specific table pick (see drawEntriesTable) only stays
            // meaningful while still near the frequency it was made at —
            // once you retune away from it (VFO knob, waterfall click,
            // etc.), the table should fall back to highlighting whatever
            // now matches the tuned frequency again.
            if (!_this->selectedEntryKey.empty() && std::abs(curFreq - _this->selectedEntryFreq) > _this->toleranceHz) {
                _this->selectedEntryKey.clear();
            }
        }

        // showMarkers only gates the drawing below — querying above always runs.
        _this->waterfallLabels.clear();
        if (!_this->showMarkers) { return; }

        const float baseY = args.min.y + _this->topOffsetPx; // dodge the Band Plan strip if configured
        const float laneHeight = ImGui::GetTextLineHeight() + 4;
        const int laneLimit = _this->maxRows;
        const ImU32 tunedBg = IM_COL32(0xCF, 0xFD, 0xBC, 255);
        const ImU32 tunedText = IM_COL32(0, 0, 0, 255);
        const ImU32 dimBg = IM_COL32(100, 100, 100, 50);
        const ImU32 dimText = IM_COL32(150, 150, 150, 130);

        auto drawLabel = [&](const ListenInfoEntry& e, float targetY, ImU32 bgColor, ImU32 textColor) {
            double centerXpos = args.min.x + std::round((e.frequency - args.lowFreq) * args.freqToPixelRatio);
            ImVec2 nameSize = ImGui::CalcTextSize(e.name.c_str());

            ImVec2 rectMin((float)centerXpos - nameSize.x / 2 - 5, targetY);
            ImVec2 rectMax((float)centerXpos + nameSize.x / 2 + 5, targetY + nameSize.y);
            ImVec2 clampedMin(std::clamp(rectMin.x, args.min.x, args.max.x), rectMin.y);
            ImVec2 clampedMax(std::clamp(rectMax.x, args.min.x, args.max.x), rectMax.y);
            if (clampedMax.x - clampedMin.x <= 0) { return; }

            args.window->DrawList->AddLine(ImVec2((float)centerXpos, targetY), ImVec2((float)centerXpos, args.max.y), bgColor);
            args.window->DrawList->AddRectFilled(clampedMin, clampedMax, bgColor);
            args.window->DrawList->AddText(ImVec2((float)centerXpos - nameSize.x / 2, targetY), textColor, e.name.c_str());
            _this->waterfallLabels.push_back({e, bgColor, rectMin, rectMax});
        };

        // Split into tuned vs. everything else. Both groups keep viewEntries'
        // original (frequency-ascending) relative order — nothing here
        // reorders based on what's currently tuned, which is exactly what
        // caused the "everything jumps down when I retune" jank before.
        std::vector<const ListenInfoEntry*> tunedHere, others;
        for (const ListenInfoEntry* ePtr : _this->viewEntries) {
            bool isTuned = std::abs(ePtr->frequency - _this->lastTunedFreq) <= _this->toleranceHz;
            (isTuned ? tunedHere : others).push_back(ePtr);
        }

        // Tuned entries get their own dedicated, stable lanes at the very
        // top (0, 1, 2...) — they all sit at ~the same X position anyway
        // (same tuned frequency +/- tolerance), so no horizontal packing
        // needed, and their lane never depends on what else is in view.
        int reservedLanes = 0;
        for (const ListenInfoEntry* ePtr : tunedHere) {
            if (reservedLanes >= laneLimit) { break; }
            drawLabel(*ePtr, baseY + reservedLanes * laneHeight, tunedBg, tunedText);
            reservedLanes++;
        }

        // Everything else: normal horizontal-collision lane packing, but
        // starting below the reserved lanes and walking `others` in its
        // stable frequency order — so a given non-tuned station keeps
        // landing in roughly the same lane frame to frame.
        // Skipped entirely when onlyTunedMarkers is on — that's a deliberate
        // choice, not a space shortage, so no "+N more" hint either.
        if (!_this->onlyTunedMarkers) {
            std::vector<float> lanePositions; // right edge of the last label placed in each lane (relative to reservedLanes)
            int hiddenForSpace = 0;

            for (const ListenInfoEntry* ePtr : others) {
                const ListenInfoEntry& e = *ePtr;
                double centerXpos = args.min.x + std::round((e.frequency - args.lowFreq) * args.freqToPixelRatio);
                ImVec2 nameSize = ImGui::CalcTextSize(e.name.c_str());
                float leftEdge = (float)centerXpos - (nameSize.x / 2) - 5;
                float rightEdge = (float)centerXpos + (nameSize.x / 2) + 5;

                float targetY = -1;
                int lane = 0;
                for (auto laneIt = lanePositions.begin(); laneIt != lanePositions.end(); ++laneIt, ++lane) {
                    if (leftEdge - 2 >= *laneIt) {
                        *laneIt = rightEdge;
                        targetY = baseY + (reservedLanes + lane) * laneHeight;
                        break;
                    }
                }
                if (targetY < 0) {
                    if (reservedLanes + lane >= laneLimit) { hiddenForSpace++; continue; } // no room; still in viewEntries/panel
                    targetY = baseY + (reservedLanes + lane) * laneHeight;
                    lanePositions.push_back(rightEdge);
                }

                drawLabel(e, targetY, dimBg, dimText);
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
    }

    // --- hover tooltip + click-to-tune, mirrors the hit-testing pattern used elsewhere in this codebase ---
    static constexpr double kHoverDelaySeconds = 1.0;
    bool mouseAlreadyDown = false;
    bool mouseClickedInLabel = false;
    std::string wfHoverKey;
    double wfHoverStart = 0.0;
    std::string panelHoverKey;
    double panelHoverStart = 0.0;
    std::string winHoverKey;
    double winHoverStart = 0.0;

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

        if (_this->mouseAlreadyDown || !inALabel) {
            _this->wfHoverKey.clear(); // nothing (relevant) hovered — reset so a later hover starts a fresh timer
            return;
        }

        gui::waterfall.inputHandled = true;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _this->mouseClickedInLabel = true;
            tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, hovered.entry.frequency);
        }

        // Tuning happens immediately above; the detail popup waits for a
        // sustained hover/hold (ImGui 1.87 predates the built-in
        // ImGuiHoveredFlags_Delay* flags, so this is a manual GetTime()
        // timer) — a tap that's just passing through to tune shouldn't also
        // pop up a detail window every time.
        std::string key = hovered.entry.name + "@" + std::to_string(hovered.entry.frequency);
        if (_this->wfHoverKey != key) {
            _this->wfHoverKey = key;
            _this->wfHoverStart = ImGui::GetTime();
        }
        if (ImGui::GetTime() - _this->wfHoverStart >= kHoverDelaySeconds) {
            drawTooltip(hovered.entry);
        }
    }

    static void drawTooltip(const ListenInfoEntry& e) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(e.name.c_str());
        ImGui::Separator();
        ImGui::Text("Frequency: %s", formatFreqFixed(e.frequency).c_str());
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

        if (ImGui::Checkbox(("Show only tuned marker##_li_onlytuned_" + _this->name).c_str(), &_this->onlyTunedMarkers)) {
            config.acquire();
            config.conf[_this->name]["onlyTunedMarkers"] = _this->onlyTunedMarkers;
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

        ImGui::LeftLabel("Max rows on waterfall");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        {
            float mr = (float)_this->maxRows;
            // Small screen (phone) -> keep this low; desktop can afford more.
            // Deliberately per-instance/per-config, not auto-detected: each
            // install (Android vs desktop) already has its own config file,
            // so this just needs to be set once per device, not sensed.
            if (ImGui::SliderFloat(("##_li_maxrows_" + _this->name).c_str(), &mr, 1.0f, 20.0f, "%.0f")) {
                _this->maxRows = (int)mr;
                config.acquire();
                config.conf[_this->name]["maxRows"] = _this->maxRows;
                config.release(true);
            }
        }

        ImGui::LeftLabel("Preferred target area");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputText(("##_li_target_" + _this->name).c_str(), _this->targetAreaBuf, sizeof(_this->targetAreaBuf))) {
            _this->targetArea = _this->targetAreaBuf;
            config.acquire();
            config.conf[_this->name]["targetArea"] = _this->targetArea;
            config.release(true);
        }

        ImGui::LeftLabel("Top offset (px)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        {
            float off = _this->topOffsetPx;
            if (ImGui::SliderFloat(("##_li_topoff_" + _this->name).c_str(), &off, 0.0f, 100.0f, "%.0f")) {
                _this->topOffsetPx = off;
                config.acquire();
                config.conf[_this->name]["topOffsetPx"] = _this->topOffsetPx;
                config.release(true);
            }
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
            float rowH = ImGui::GetTextLineHeightWithSpacing();
            bool doScroll = !almostEqual(_this->lastTunedFreq, _this->lastScrolledFreqPanel);
            drawEntriesTable(_this->viewEntries, "panel_" + _this->name, rowH * 8.0f, // header + ~7 rows
                              _this->lastTunedFreq, _this->toleranceHz, doScroll, true,
                              _this->panelHoverKey, _this->panelHoverStart,
                              _this->selectedEntryKey, _this->selectedEntryFreq);
            if (doScroll) { _this->lastScrolledFreqPanel = _this->lastTunedFreq; }
        }

        if (ImGui::Button(("Browse entries...##_li_browse_" + _this->name).c_str(), ImVec2(menuWidth, 0))) {
            _this->entriesWindowOpen = true;
            config.acquire();
            config.conf[_this->name]["entriesWindowOpen"] = true;
            config.release(true);
        }

        // The detached window is always rendered from here while open — same
        // idiom as the FT8 decoder's "Show Decodes" button/window.
        if (_this->entriesWindowOpen) { _this->drawEntriesWindow(); }
    }

    // Shared by the compact in-panel table and the detached browse window,
    // so both always show the exact same rows in the exact same format.
    // compact=true renders only Freq+Name (for the narrow panel); false
    // renders all four columns (the browse window). tunedFreq/toleranceHz
    // identify which row (if any) is "currently tuned" — that row gets the
    // same highlight color used on the waterfall (with black text pushed
    // on top so it stays readable against the light background), and
    // scrollToTuned (true only on the frame the tuned frequency changed —
    // see the two lastScrolledFreq* trackers at the call sites) brings it
    // into view without fighting manual scrolling. Clicking (or hovering)
    // a row shows the same detail popup as clicking a waterfall marker —
    // same drawTooltip() function, same content, same behavior either way.
    static void drawEntriesTable(const std::vector<const ListenInfoEntry*>& entries, const std::string& idSuffix, float height,
                                   double tunedFreq, double toleranceHz, bool scrollToTuned, bool compact,
                                   std::string& hoverKey, double& hoverStart,
                                   std::string& selectedEntryKey, double& selectedEntryFreq) {
        const ImU32 tunedRowBg = IM_COL32(0xCF, 0xFD, 0xBC, 255); // same style as the waterfall's tuned label
        const ImU32 tunedRowText = IM_COL32(0, 0, 0, 255);
        const int colCount = compact ? 2 : 4;

        // Widths sized from actual content via CalcTextSize (which already
        // accounts for the current font/UI scale) instead of guessed fixed
        // pixel values — those didn't reliably fit real text at every scale.
        // Freq/Time are our own format strings so their worst case is known
        // in advance; Target is free text from the database, so it's
        // measured against what's actually being shown this call.
        // Freq's header now carries the unit ("Freq (kHz)"), so cell values
        // are unitless numbers only — shorter, and unambiguous since every
        // row uses the same fixed unit (unlike formatFreqFixed's MHz/kHz/Hz
        // auto-selection, which needs the per-cell suffix to stay honest).
        float freqColW = ImGui::CalcTextSize("Freq (kHz)").x + 20.0f;
        float timeColW = compact ? 0.0f : ImGui::CalcTextSize("0000-0000").x + 20.0f;
        float targetColW = compact ? 0.0f : ImGui::CalcTextSize("Target").x + 20.0f;
        if (!compact) {
            for (const ListenInfoEntry* ePtr : entries) {
                float w = ImGui::CalcTextSize(ePtr->targetArea.c_str()).x + 20.0f;
                if (w > targetColW) { targetColW = w; }
            }
        }

        // Captured during the row loop, but the actual drawTooltip() call
        // happens only after EndTable() below — opening a nested tooltip
        // window (its own Begin/End pair) in the middle of a table's
        // row/column sequence is a known ImGui footgun: it can disturb the
        // table's internal per-row bookkeeping for whatever comes after,
        // which best explains both the "gray-on-gray" popup and the
        // "wrong row highlighted" reports — neither is a plausibility bug
        // in isTuned itself, both point at the table's state getting
        // stepped on mid-iteration.
        const ListenInfoEntry* tooltipEntry = nullptr;
        bool anyHoveredThisFrame = false;

        if (ImGui::BeginTable(("##_li_table_" + idSuffix).c_str(), colCount,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                ImVec2(0, height))) {
            ImGui::TableSetupScrollFreeze(0, 1); // 0 columns, 1 row (the header) frozen
            // Name is the only column that should grow/shrink with the
            // table; the rest are sized to fit their actual content exactly.
            ImGui::TableSetupColumn("Freq (kHz)", ImGuiTableColumnFlags_WidthFixed, freqColW);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            if (!compact) {
                ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, targetColW);
                ImGui::TableSetupColumn("Time (UTC)", ImGuiTableColumnFlags_WidthFixed, timeColW);
            }
            ImGui::TableHeadersRow();
            for (const ListenInfoEntry* ePtr : entries) {
                const ListenInfoEntry& e = *ePtr;
                bool freqMatches = std::abs(e.frequency - tunedFreq) <= toleranceHz;
                // Until a specific row has been clicked, fall back to the
                // old frequency-match behavior (highlight every co-channel
                // candidate) — matches what the waterfall still does, and
                // is a reasonable default right after tuning in from
                // elsewhere. Once a row IS explicitly clicked, only that
                // exact entry counts as selected, by identity, not just by
                // sharing the same frequency as several other stations.
                bool isSelected = selectedEntryKey.empty() ? freqMatches : (entryKey(e) == selectedEntryKey);

                ImGui::TableNextRow();
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Text, tunedRowText);
                }

                ImGui::TableNextColumn();
                ImGui::Text("%.1f", e.frequency / 1000.0); // unitless — header already says kHz
                ImGui::TableNextColumn();
                // Use ImGui's own selected-row rendering (Selectable's
                // `selected` flag -> ImGuiCol_Header/-Hovered/-Active)
                // instead of a manually painted TableSetBgColor. The two
                // were fighting: Selectable draws its own transient
                // hover/click background on top of whatever the row
                // background already was, which is what produced the
                // reported flicker/gray/revert — using Selectable's own
                // selected state avoids that conflict entirely, since it's
                // the single mechanism now, not two competing ones.
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, tunedRowBg);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tunedRowBg);
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, tunedRowBg);
                }
                bool clicked = ImGui::Selectable((e.name + "##" + idSuffix + "_" + std::to_string(e.frequency)).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns);
                bool rowHovered = ImGui::IsItemHovered(); // must be read right after the item — captured, not acted on yet
                // Only scroll if the row genuinely isn't visible yet — a row
                // that's already one of the ~7 on screen shouldn't get
                // recentered to the middle every time it becomes the tuned
                // one, which is what made every other row visibly jump.
                if (isSelected && scrollToTuned && !ImGui::IsItemVisible()) {
                    ImGui::SetScrollHereY(0.5f);
                }
                if (clicked) {
                    tuner::tune(tuner::TUNER_MODE_NORMAL, gui::waterfall.selectedVFO, e.frequency);
                    selectedEntryKey = entryKey(e);
                    selectedEntryFreq = e.frequency;
                }
                if (isSelected) { ImGui::PopStyleColor(3); }
                if (!compact) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.targetArea.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%04d-%04d", e.startTime, e.endTime);
                }
                if (isSelected) { ImGui::PopStyleColor(); }

                // Tuning (above) is instant; the detail popup waits for a
                // sustained hover/hold, same kHoverDelaySeconds timer as
                // the waterfall — a tap that's just passing through
                // shouldn't also pop up a detail window every time.
                if (rowHovered) {
                    anyHoveredThisFrame = true;
                    std::string key = idSuffix + "_" + std::to_string(e.frequency);
                    if (hoverKey != key) {
                        hoverKey = key;
                        hoverStart = ImGui::GetTime();
                    }
                    if (ImGui::GetTime() - hoverStart >= kHoverDelaySeconds) {
                        tooltipEntry = &e;
                    }
                }
            }
            ImGui::EndTable();
        }

        if (!anyHoveredThisFrame) { hoverKey.clear(); }
        if (tooltipEntry) { drawTooltip(*tooltipEntry); }
    }

    // Detached, non-modal browse window — same pattern as ft8_decoder's
    // drawDecodesWindow(): a plain ImGui::Begin()/End() pair (never
    // BeginPopupModal), so it gets the native title-bar collapse triangle
    // and close button for free, and dragging it never darkens the rest of
    // the GUI or disturbs the VFO. For now it shows exactly the same
    // (already time/frequency-filtered) entries as the panel table — an
    // independent "all records" scope and search/filter come later.
    //
    // gui::mainWindow.lockWaterfallControls is set while this window is
    // hovered or being dragged, for the same reason ft8_decoder and
    // frequency_manager set it: the waterfall's drag handling uses a global
    // GetMouseDragDelta() and can mistake a drag starting on an overlapping
    // floating window for a click on the waterfall itself, which would move
    // the VFO out from under you.
    void drawEntriesWindow() {
        std::string title = "Frequency Info entries (" + name + ")###freqinfo_entries_win_" + name;
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title.c_str(), &entriesWindowOpen)) {
            ImGui::End();
            return;
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                   ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
            (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
             ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
            gui::mainWindow.lockWaterfallControls = true;
        }

        {
            std::lock_guard<std::mutex> lk(dataMutex);
            ImGui::Text("%zu entries", viewEntries.size());
            bool doScroll = !almostEqual(lastTunedFreq, lastScrolledFreqWindow);
            drawEntriesTable(viewEntries, "browsewin_" + name, ImGui::GetContentRegionAvail().y,
                              lastTunedFreq, toleranceHz, doScroll, false,
                              winHoverKey, winHoverStart,
                              selectedEntryKey, selectedEntryFreq);
            if (doScroll) { lastScrolledFreqWindow = lastTunedFreq; }
        }

        ImGui::End();

        if (!entriesWindowOpen) {
            config.acquire();
            config.conf[name]["entriesWindowOpen"] = false;
            config.release(true);
        }
    }

    std::string name;
    bool enabled = true;

    ListenInfoDatabase db;
    std::string eibiPath;
    char eibiPathBuf[1024] = {0};
    std::string lastLoadError;
    bool showMarkers = true;
    bool onlyTunedMarkers = false;
    bool entriesWindowOpen = false;
    double toleranceHz = 1000.0;
    float toleranceHzF = 1000.0f;
    int maxRows = 6;
    float topOffsetPx = 0.0f;
    std::string targetArea;
    char targetAreaBuf[16] = {0};

    double lastTunedFreq = -1;
    double lastScrolledFreqPanel = -1;
    double lastScrolledFreqWindow = -1;
    // The specific entry explicitly clicked in EITHER table (shared, not
    // per-table) — distinct from "isTuned"/frequency matching, which stays
    // frequency-based for the waterfall (multiple co-channel stations are
    // meant to all highlight there) but isn't what a table click means:
    // clicking one row should select that one row, not every row sharing
    // its frequency.
    std::string selectedEntryKey;
    double selectedEntryFreq = -1;
    static std::string entryKey(const ListenInfoEntry& e) {
        return e.name + "@" + std::to_string(e.frequency);
    }
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
