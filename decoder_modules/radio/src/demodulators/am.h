#pragma once
#include "../demod.h"
#include <dsp/demod/am.h>

namespace demod {
    class AM : public Demodulator {
    public:
        AM() {}

        AM(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~AM() { stop(); }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;
            _config = config;

            // Load config
            config->acquire();
            if (config->conf[name][getName()].contains("agcAttack")) {
                agcAttack = config->conf[name][getName()]["agcAttack"];
            }
            if (config->conf[name][getName()].contains("agcDecay")) {
                agcDecay = config->conf[name][getName()]["agcDecay"];
            }
            if (config->conf[name][getName()].contains("agcMode")) {
                agcMode = std::clamp<int>(config->conf[name][getName()]["agcMode"], 0, 2);
            }
            else if (config->conf[name][getName()].contains("carrierAgc")) {
                // Legacy config format with a carrier AGC on/off flag
                bool carrierAgc = config->conf[name][getName()]["carrierAgc"];
                agcMode = carrierAgc ? dsp::demod::AM<dsp::stereo_t>::AGCMode::CARRIER : dsp::demod::AM<dsp::stereo_t>::AGCMode::AUDIO;
            }
            if (config->conf[name][getName()].contains("agcGain")) {
                agcGain = config->conf[name][getName()]["agcGain"];
            }
            config->release();

            // Define structure
            demod.init(input, (dsp::demod::AM<dsp::stereo_t>::AGCMode)agcMode, bandwidth, agcAttack / getIFSampleRate(), agcDecay / getIFSampleRate(), 100.0 / getIFSampleRate(), getIFSampleRate());
            demod.setAGCGain(powf(10.0f, agcGain / 20.0f));
        }

        void start() { demod.start(); }

        void stop() { demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            ImGui::LeftLabel("AGC Mode");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::Combo(("##_radio_am_agc_mode_" + name).c_str(), &agcMode, "Off\0Carrier\0Audio\0")) {
                demod.setAGCMode((dsp::demod::AM<dsp::stereo_t>::AGCMode)agcMode);
                agcGain = std::clamp<float>(20.0f * log10f(demod.getAGCGain()), -10.0f, 90.0f);
                _config->acquire();
                _config->conf[name][getName()]["agcMode"] = agcMode;
                if (agcMode == dsp::demod::AM<dsp::stereo_t>::AGCMode::OFF) {
                    // Keep the last AGC gain as the manual gain
                    _config->conf[name][getName()]["agcGain"] = agcGain;
                }
                _config->release(true);
            }
            bool agcEnabled = (agcMode != dsp::demod::AM<dsp::stereo_t>::AGCMode::OFF);
            if (agcEnabled) {
                agcGain = std::clamp<float>(20.0f * log10f(demod.getAGCGain()), -10.0f, 90.0f);
                ImGui::BeginDisabled();
            }
            ImGui::LeftLabel("Gain");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_gain_" + name).c_str(), &agcGain, -10.0f, 90.0f, "%.0f dB")) {
                demod.setAGCGain(powf(10.0f, agcGain / 20.0f));
                _config->acquire();
                _config->conf[name][getName()]["agcGain"] = agcGain;
                _config->release(true);
            }
            if (agcEnabled) { ImGui::EndDisabled(); }
            else { ImGui::BeginDisabled(); }
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_agc_attack_" + name).c_str(), &agcAttack, 1.0f, 200.0f)) {
                demod.setAGCAttack(agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcAttack"] = agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_am_agc_decay_" + name).c_str(), &agcDecay, 1.0f, 20.0f)) {
                demod.setAGCDecay(agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcDecay"] = agcDecay;
                _config->release(true);
            }
            if (!agcEnabled) { ImGui::EndDisabled(); }
        }

        void setBandwidth(double bandwidth) { demod.setBandwidth(bandwidth); }

        void setInput(dsp::stream<dsp::complex_t>* input) { demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "AM"; }
        double getIFSampleRate() { return 15000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 10000.0; }
        double getMinBandwidth() { return 1000.0; }
        double getMaxBandwidth() { return getIFSampleRate(); }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 1000.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &demod.out; }

    private:
        dsp::demod::AM<dsp::stereo_t> demod;

        ConfigManager* _config = NULL;

        int agcMode = dsp::demod::AM<dsp::stereo_t>::AGCMode::AUDIO;
        float agcGain = 0.0f;
        float agcAttack = 50.0f;
        float agcDecay = 5.0f;

        std::string name;
    };
}