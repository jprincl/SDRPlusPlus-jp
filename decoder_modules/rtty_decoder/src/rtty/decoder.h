#pragma once
#include "../decoder.h"
#include "modem.h"
#include <imgui.h>
#include <config.h>
#include <gui/style.h>
#include <dsp/noise_reduction/power_squelch.h>
#include <dsp/sink/handler_sink.h>
#include <string>
#include <mutex>
#include <vector>

// dsp::complex_t is {float re; float im;} so a complex_t buffer can be read
// by the modem as interleaved floats.
#include <dsp/types.h>

#define RTTY_CONCAT(a, b) ((std::string(a) + b).c_str())

extern ConfigManager config;

// RTTY presets (FLDIGI-style). baud / shift in Hz.
struct RTTYPreset { const char* name; double baud; double shift; };
static const RTTYPreset RTTY_PRESETS[] = {
    { "45.45 baud / 170 Hz", 45.45, 170.0 },
    { "50 baud / 170 Hz",    50.0,  170.0 },
    { "75 baud / 170 Hz",    75.0,  170.0 },
    { "100 baud / 170 Hz",   100.0, 170.0 },
    { "45.45 baud / 85 Hz",  45.45, 85.0  },
    { "75 baud / 850 Hz",    75.0,  850.0 },
    { "100 baud / 425 Hz",   100.0, 425.0 },
    { "50 baud / 450 Hz (DWD meteo)", 50.0, 450.0 },
};
static const int RTTY_PRESET_COUNT = sizeof(RTTY_PRESETS) / sizeof(RTTY_PRESETS[0]);
static const char* RTTY_PRESET_LIST =
    "45.45 baud / 170 Hz\0""50 baud / 170 Hz\0""75 baud / 170 Hz\0"
    "100 baud / 170 Hz\0""45.45 baud / 85 Hz\0""75 baud / 850 Hz\0""100 baud / 425 Hz\0"
    "50 baud / 450 Hz (DWD meteo)\0";

class RTTYDecoder : public Decoder {
public:
    RTTYDecoder(const std::string& name, VFOManager::VFO* vfo) {
        this->name = name;
        this->vfo  = vfo;

        // Load RTTY-specific settings.
        config.acquire();
        if (config.conf[name].contains("reverse")) { reverse = config.conf[name]["reverse"]; }
        if (config.conf[name].contains("usos"))    { usos    = config.conf[name]["usos"]; }
        if (config.conf[name].contains("modeIdx")) { modeIdx = config.conf[name]["modeIdx"]; }
        config.release(false);
        if (modeIdx < 0 || modeIdx >= RTTY_PRESET_COUNT) { modeIdx = 0; }

        modem.onChar = [this](char c) { this->onChar(c); };
        applyModeConfig();
    }

    ~RTTYDecoder() { stop(); }

    void setVFO(VFOManager::VFO* v) override { vfo = v; }

    void start() override {
        if (running || !vfo) { return; }
        squelch.init(vfo->output, squelchEnabled ? squelchLevel : -150.0);
        sink.init(&squelch.out, handler, this);
        squelch.start();
        sink.start();
        running = true;
    }

    void stop() override {
        if (!running) { return; }
        sink.stop();
        squelch.stop();
        running = false;
    }

    // --- shared control surface ---
    void setVFOMode(int mode) override { vfoMode = mode; modem.setSideband(mode); }
    void setMode(int idx) override {
        if (idx < 0 || idx >= RTTY_PRESET_COUNT) { return; }
        modeIdx = idx;
        applyModeConfig();
    }
    void setAFFreq(double f) override { afFreq = f; modem.setAFFreq(f); }
    float getAFFreq() const override { return (float)modem.getAFFreq(); }
    void setSquelchEnabled(bool en) override {
        squelchEnabled = en;
        if (running) { squelch.setLevel(en ? squelchLevel : -150.0); }
    }
    void setSquelchLevel(float dB) override {
        squelchLevel = dB;
        if (running && squelchEnabled) { squelch.setLevel(dB); }
    }
    void setAFCEnabled(bool en) override { afc = en; modem.setAFCEnabled(en); }
    double getTrackedAFFreq() const override { return modem.getTrackedAFFreq(); }

    int getBandSpectrum(float* out, int n) const override { return modem.getBandSpectrum(out, n); }
    double getBandFlo() const override { return modem.getBandFlo(); }
    double getBandFhi() const override { return modem.getBandFhi(); }
    double getSignalBandwidth() const override { return modem.getSignalBandwidth(); }

    // --- RTTY-specific UI (decoded text + reverse / USOS) ---
    void showMenu() override {
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (ImGui::Checkbox(RTTY_CONCAT("Reverse##rtty_rev_", name), &reverse)) {
            applyModeConfig();
            config.acquire(); config.conf[name]["reverse"] = reverse; config.release(true);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(RTTY_CONCAT("USOS##rtty_usos_", name), &usos)) {
            applyModeConfig();
            config.acquire(); config.conf[name]["usos"] = usos; config.release(true);
        }

        // Decoded text area.
        ImGui::BeginChild(RTTY_CONCAT("##rtty_text_", name),
                          ImVec2(menuWidth, 180.0f * style::uiScale), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lck(textMtx);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(decodedText.c_str());
            ImGui::PopTextWrapPos();
        }
        if (scrollDown) { ImGui::SetScrollHereY(1.0f); scrollDown = false; }
        ImGui::EndChild();

        if (ImGui::Button(RTTY_CONCAT("Clear##rtty_clr_", name), ImVec2(menuWidth, 0))) {
            std::lock_guard<std::mutex> lck(textMtx);
            decodedText.clear();
        }
    }

private:
    void applyModeConfig() {
        const RTTYPreset& p = RTTY_PRESETS[modeIdx];
        modem.configure(sampleRate, p.baud, p.shift, 1.5f, reverse, usos);
        modem.setSideband(vfoMode);
        modem.setAFFreq(afFreq);
        modem.setAFCEnabled(afc);
    }

    void onChar(char c) {
        std::lock_guard<std::mutex> lck(textMtx);
        decodedText.push_back(c);
        if (decodedText.size() > 20000) { decodedText.erase(0, decodedText.size() - 20000); }
        scrollDown = true;
    }

    static void handler(dsp::complex_t* data, int count, void* ctx) {
        RTTYDecoder* _this = (RTTYDecoder*)ctx;
        _this->modem.process((const float*)data, count);
    }

    std::string name;
    VFOManager::VFO* vfo = NULL;
    bool running = false;

    dsp::noise_reduction::PowerSquelch squelch;
    dsp::sink::Handler<dsp::complex_t> sink;
    rtty::Modem modem;

    double sampleRate   = 24000.0;
    double afFreq       = 1000.0;
    int    vfoMode      = 0;       // USB
    int    modeIdx      = 0;
    bool   reverse      = false;
    bool   usos         = true;
    bool   afc          = false;
    bool   squelchEnabled = false;
    float  squelchLevel = -50.0f;

    std::mutex  textMtx;
    std::string decodedText;
    bool scrollDown = false;
};
