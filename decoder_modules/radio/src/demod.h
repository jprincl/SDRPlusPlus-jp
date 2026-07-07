#pragma once
#include <algorithm>
#include <cmath>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <gui/widgets/waterfall.h>
#include <gui/style.h>
#include <config.h>
#include <utils/event.h>

enum DeemphasisMode {
    DEEMP_MODE_22US,
    DEEMP_MODE_50US,
    DEEMP_MODE_75US,
    DEEMP_MODE_NONE,
    _DEEMP_MODE_COUNT
};

enum IFNRPreset {
    IFNR_PRESET_NOAA_APT,
    IFNR_PRESET_VOICE,
    IFNR_PRESET_NARROW_BAND,
    IFNR_PRESET_BROADCAST
};

namespace demod {
    class Demodulator {
    public:
        virtual ~Demodulator() {}
        virtual void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) = 0;
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual void showMenu() = 0;
        virtual void setBandwidth(double bandwidth) = 0;
        virtual void setInput(dsp::stream<dsp::complex_t>* input) = 0;
        virtual void AFSampRateChanged(double newSR) = 0;
        virtual const char* getName() = 0;
        virtual double getIFSampleRate() = 0;
        virtual double getAFSampleRate() = 0;
        virtual double getDefaultBandwidth() = 0;
        virtual double getMinBandwidth() = 0;
        virtual double getMaxBandwidth() = 0;
        virtual bool getBandwidthLocked() = 0;
        virtual double getDefaultSnapInterval() = 0;
        virtual int getVFOReference() = 0;
        virtual bool getDeempAllowed() = 0;
        virtual bool getPostProcEnabled() = 0;
        virtual int getDefaultDeemphasisMode() = 0;
        virtual bool getFMIFNRAllowed() = 0;
        virtual bool getNBAllowed() = 0;
        virtual dsp::stream<dsp::stereo_t>* getOutput() = 0;

    protected:
        friend struct AGCControls;

        // Load a value from the demodulator's config section if present
        template <class T>
        static void loadConf(const nlohmann::json& j, const char* key, T& out) {
            if (j.contains(key)) { out = j[key]; }
        }

        // Save a value into the demodulator's config section
        template <class T>
        void saveConf(const char* key, const T& value) {
            _config->acquire();
            _config->conf[name][getName()][key] = value;
            _config->release(true);
        }

        ConfigManager* _config = NULL;
        std::string name;
    };

    // AGC state shared by the SSB and CW family demodulators: on/off switch, manual gain
    // and attack/decay, with the matching config load and menu UI. In auto mode the gain
    // slider shows the live AGC gain, in manual mode it sets a fixed gain.
    struct AGCControls {
        bool enabled = true;
        float gain = 0.0f;   // dB
        float attack = 50.0f;
        float decay = 5.0f;

        void load(const nlohmann::json& j) {
            Demodulator::loadConf(j, "agcEnabled", enabled);
            Demodulator::loadConf(j, "agcGain", gain);
            Demodulator::loadConf(j, "agcAttack", attack);
            Demodulator::loadConf(j, "agcDecay", decay);
        }

        // Apply the enabled state and manual gain to a freshly init()ed demodulator
        // (attack and decay are passed to the demodulator's init() directly)
        template <class TDemod>
        void apply(TDemod& demod) {
            demod.setAGCEnabled(enabled);
            demod.setAGCGain(powf(10.0f, gain / 20.0f));
        }

        template <class TDemod>
        void showMenu(TDemod& demod, Demodulator* owner, const char* idPrefix, double ifSamplerate, float menuWidth) {
            std::string id = std::string("##_radio_") + idPrefix;
            if (ImGui::Checkbox(("AGC" + id + "_agc_ena_" + owner->name).c_str(), &enabled)) {
                demod.setAGCEnabled(enabled);
                owner->saveConf("agcEnabled", enabled);
                if (!enabled) {
                    // Keep the last AGC gain as the manual gain
                    owner->saveConf("agcGain", gain);
                }
            }
            if (enabled) {
                gain = std::clamp<float>(20.0f * log10f(demod.getAGCGain()), -10.0f, 90.0f);
                ImGui::BeginDisabled();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat((id + "_gain_" + owner->name).c_str(), &gain, -10.0f, 90.0f, "%.0f dB")) {
                demod.setAGCGain(powf(10.0f, gain / 20.0f));
                owner->saveConf("agcGain", gain);
            }
            if (enabled) { ImGui::EndDisabled(); }
            else { ImGui::BeginDisabled(); }
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat((id + "_agc_attack_" + owner->name).c_str(), &attack, 1.0f, 200.0f)) {
                demod.setAGCAttack(attack / ifSamplerate);
                owner->saveConf("agcAttack", attack);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat((id + "_agc_decay_" + owner->name).c_str(), &decay, 1.0f, 20.0f)) {
                demod.setAGCDecay(decay / ifSamplerate);
                owner->saveConf("agcDecay", decay);
            }
            if (!enabled) { ImGui::EndDisabled(); }
        }
    };
}

#include "demodulators/wfm.h"
#include "demodulators/nfm.h"
#include "demodulators/am.h"
#include "demodulators/usb.h"
#include "demodulators/lsb.h"
#include "demodulators/dsb.h"
#include "demodulators/cw.h"
#include "demodulators/cwr.h"
#include "demodulators/raw.h"