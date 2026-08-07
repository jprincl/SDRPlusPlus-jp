#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "decoder.h"
#include "rtty/decoder.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "rtty_decoder",
    /* Description:     */ "FLDIGI-style RTTY (Baudot/ITA2) decoder",
    /* Author:          */ "F4JTV",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ -1
};

ConfigManager config;

class RTTYDecoderModule : public ModuleManager::Instance {
public:
    RTTYDecoderModule(std::string name) {
        this->name = name;

        // Load shared settings.
        config.acquire();
        if (config.conf[name].contains("afFreq"))         { afFreq = config.conf[name]["afFreq"]; }
        if (config.conf[name].contains("vfoMode"))        { vfoMode = config.conf[name]["vfoMode"]; }
        if (config.conf[name].contains("modeIdx"))        { modeIdx = config.conf[name]["modeIdx"]; }
        if (config.conf[name].contains("afcEnabled"))     { afcEnabled = config.conf[name]["afcEnabled"]; }
        if (config.conf[name].contains("squelchEnabled")) { squelchEnabled = config.conf[name]["squelchEnabled"]; }
        if (config.conf[name].contains("squelchLevel"))   { squelchLevel = config.conf[name]["squelchLevel"]; }
        config.release(false);
        if (vfoMode < 0 || vfoMode > 2) { vfoMode = 0; }

        // Create the VFO (USB defaults; sideband() applies the right reference).
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_LOWER, 0,
                                            3000, 24000, 200, 15000, false);
        vfo->setSnapInterval(1);

        createDecoder();
        enabled = true;
        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~RTTYDecoderModule() {
        gui::menu.removeEntry(name);
        if (enabled) {
            decoder->stop();
            decoder.reset();
            sigpath::vfoManager.deleteVFO(vfo);
        }
    }

    void postInit() {}

    void enable() {
        double bw = gui::waterfall.getBandwidth();
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_LOWER,
                                            std::clamp<double>(0, -bw / 2.0, bw / 2.0),
                                            3000, 24000, 200, 15000, false);
        vfo->setSnapInterval(1);
        createDecoder();
        enabled = true;
    }

    void disable() {
        if (decoder) { decoder->stop(); decoder.reset(); }
        sigpath::vfoManager.deleteVFO(vfo);
        vfo = NULL;
        enabled = false;
    }

    bool isEnabled() { return enabled; }

private:
    // Build a fresh decoder (one DSP init per object) and push current state.
    void createDecoder() {
        decoder = std::make_unique<RTTYDecoder>(name, vfo);
        decoder->setVFO(vfo);
        applyVFOMode(vfoMode, true);
        decoder->setMode(modeIdx);
        decoder->setAFFreq(afFreq);
        decoder->setAFCEnabled(afcEnabled);
        decoder->setSquelchEnabled(squelchEnabled);
        decoder->setSquelchLevel(squelchLevel);
        decoder->start();
    }

    void applyVFOMode(int mode, bool silent) {
        vfoMode = mode;
        if (vfo) {
            if (mode == 0) {        // USB
                vfo->setReference(ImGui::WaterfallVFO::REF_LOWER);
                vfo->setBandwidth(3000);
                vfo->setBandwidthLimits(200, 15000, false);
            }
            else if (mode == 1) {   // LSB
                vfo->setReference(ImGui::WaterfallVFO::REF_UPPER);
                vfo->setBandwidth(3000);
                vfo->setBandwidthLimits(200, 15000, false);
            }
            else {                  // NFM
                vfo->setReference(ImGui::WaterfallVFO::REF_CENTER);
                vfo->setBandwidth(12500);
                vfo->setBandwidthLimits(5000, 25000, false);
            }
        }
        if (decoder) { decoder->setVFOMode(mode); }
        if (!silent) {
            config.acquire(); config.conf[name]["vfoMode"] = mode; config.release(true);
        }
    }

    static void menuHandler(void* ctx) {
        RTTYDecoderModule* _this = (RTTYDecoderModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (!_this->enabled) { style::beginDisabled(); }

        // --- Mode (RTTY preset) ---
        ImGui::LeftLabel("Mode");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo(CONCAT("##rtty_mode_", _this->name), &_this->modeIdx, RTTY_PRESET_LIST)) {
            _this->decoder->setMode(_this->modeIdx);
            config.acquire(); config.conf[_this->name]["modeIdx"] = _this->modeIdx; config.release(true);
        }

        // --- Sideband ---
        ImGui::LeftLabel("Sideband");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo(CONCAT("##rtty_sb_", _this->name), &_this->vfoMode, "USB\0LSB\0NFM\0")) {
            _this->applyVFOMode(_this->vfoMode, false);
        }

        // --- Band view: smoothed audio-passband spectrum with a marker at the
        //     RTTY centre (AF) and a shaded mark/space footprint. Click or
        //     drag to set the AF. Same widget as the PSK/MFSK modules. ---
        if (_this->decoder) {
            static float spec[256];
            int nb = _this->decoder->getBandSpectrum(spec, 256);
            if (nb > 0) {
                double flo = _this->decoder->getBandFlo();
                double fhi = _this->decoder->getBandFhi();
                double bw  = _this->decoder->getSignalBandwidth();
                double afc = (double)_this->decoder->getAFFreq();

                float w = menuWidth;
                float h = 56.0f * style::uiScale;
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 cBg     = IM_COL32(20, 22, 28, 255);
                ImU32 cBar    = IM_COL32(90, 160, 230, 255);
                ImU32 cBand   = IM_COL32(90, 160, 230, 60);
                ImU32 cMarker = IM_COL32(250, 200, 80, 255);
                dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), cBg, 3.0f);

                float mx = 1e-6f;
                for (int i = 0; i < nb; i++) { if (spec[i] > mx) { mx = spec[i]; } }

                auto f2x = [&](double f) { return p0.x + w * (float)((f - flo) / (fhi - flo)); };

                // Shaded mark/space footprint around the AF centre.
                if (bw > 0) {
                    float xa = f2x(afc - bw * 0.5);
                    float xb = f2x(afc + bw * 0.5);
                    if (xa < p0.x)     { xa = p0.x; }
                    if (xb > p0.x + w) { xb = p0.x + w; }
                    if (xb > xa) { dl->AddRectFilled(ImVec2(xa, p0.y), ImVec2(xb, p0.y + h), cBand); }
                }

                // Spectrum bars.
                for (int i = 0; i < nb; i++) {
                    float x  = p0.x + w * (i / (float)(nb - 1));
                    float bh = (spec[i] / mx) * (h - 4);
                    dl->AddLine(ImVec2(x, p0.y + h - 2), ImVec2(x, p0.y + h - 2 - bh), cBar, 1.0f);
                }

                // AF marker line.
                float xe = f2x(afc);
                dl->AddLine(ImVec2(xe, p0.y), ImVec2(xe, p0.y + h), cMarker, 2.0f);

                // Click / drag to set AF (disabled while AFC owns the AF).
                ImGui::InvisibleButton(CONCAT("##rtty_band_", _this->name), ImVec2(w, h));
                if (ImGui::IsItemActive() && !_this->afcEnabled) {
                    float mxs = ImGui::GetIO().MousePos.x;
                    double f = flo + (double)((mxs - p0.x) / w) * (fhi - flo);
                    f = std::round(f);
                    if (f < 700.0)  { f = 700.0; }
                    if (f > 2500.0) { f = 2500.0; }
                    _this->afFreq = (float)f;
                    _this->decoder->setAFFreq(_this->afFreq);
                }
                if (ImGui::IsItemDeactivatedAfterEdit() ||
                    (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))) {
                    config.acquire(); config.conf[_this->name]["afFreq"] = _this->afFreq; config.release(true);
                }
            }
        }

        // --- AFC ---
        if (ImGui::Checkbox(CONCAT("AFC##rtty_afc_", _this->name), &_this->afcEnabled)) {
            _this->decoder->setAFCEnabled(_this->afcEnabled);
            config.acquire(); config.conf[_this->name]["afcEnabled"] = _this->afcEnabled; config.release(true);
        }
        if (_this->afcEnabled) {
            ImGui::SameLine();
            ImGui::TextDisabled("(auto: %.0f Hz)", _this->decoder->getTrackedAFFreq());
        }

        // --- AF frequency (manual; disabled while AFC is on) ---
        ImGui::LeftLabel("AF freq");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        ImGui::BeginDisabled(_this->afcEnabled);
        if (ImGui::SliderFloat(CONCAT("##rtty_af_", _this->name), &_this->afFreq, 700.0f, 2500.0f, "%.0f Hz")) {
            _this->decoder->setAFFreq(_this->afFreq);
            config.acquire(); config.conf[_this->name]["afFreq"] = _this->afFreq; config.release(true);
        }
        ImGui::EndDisabled();

        // --- Squelch ---
        if (ImGui::Checkbox(CONCAT("Squelch##rtty_sq_en_", _this->name), &_this->squelchEnabled)) {
            _this->decoder->setSquelchEnabled(_this->squelchEnabled);
            config.acquire(); config.conf[_this->name]["squelchEnabled"] = _this->squelchEnabled; config.release(true);
        }
        if (!_this->squelchEnabled) { style::beginDisabled(); }
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat(CONCAT("##rtty_sq_lvl_", _this->name), &_this->squelchLevel, -100.0f, 0.0f, "%.1f dB")) {
            _this->decoder->setSquelchLevel(_this->squelchLevel);
            config.acquire(); config.conf[_this->name]["squelchLevel"] = _this->squelchLevel; config.release(true);
        }
        if (!_this->squelchEnabled) { style::endDisabled(); }

        // --- Mode-specific UI (Reverse / USOS + decoded text) ---
        if (_this->decoder) { _this->decoder->showMenu(); }

        if (!_this->enabled) { style::endDisabled(); }
    }

    std::string name;
    bool enabled = false;
    VFOManager::VFO* vfo = NULL;
    std::unique_ptr<RTTYDecoder> decoder;

    float afFreq = 1000.0f;
    int   vfoMode = 0;
    int   modeIdx = 0;
    bool  afcEnabled = false;
    bool  squelchEnabled = false;
    float squelchLevel = -50.0f;
};

MOD_EXPORT void _INIT_() {
    std::string root = (std::string)core::args["root"];
    json def = json({});
    config.setPath(root + "/rtty_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new RTTYDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (RTTYDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
