#pragma once
#include "../demod.h"
#include <dsp/demod/cw.h>

namespace demod {
    class CWR : public Demodulator {
    public:
        CWR() {}

        CWR(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~CWR() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;
            this->_config = config;
            this->afbwChangeHandler = afbwChangeHandler;

            config->acquire();
            if (config->conf[name][getName()].contains("agcEnabled")) {
                agcEnabled = config->conf[name][getName()]["agcEnabled"];
            }
            if (config->conf[name][getName()].contains("agcGain")) {
                agcGain = config->conf[name][getName()]["agcGain"];
            }
            if (config->conf[name][getName()].contains("agcAttack")) {
                agcAttack = config->conf[name][getName()]["agcAttack"];
            }
            if (config->conf[name][getName()].contains("agcDecay")) {
                agcDecay = config->conf[name][getName()]["agcDecay"];
            }
            if (config->conf[name][getName()].contains("tone")) {
                tone = config->conf[name][getName()]["tone"];
            }
            config->release();

            demod.init(input, -tone, agcAttack / getIFSampleRate(), agcDecay / getIFSampleRate(), getIFSampleRate());
            demod.setAGCEnabled(agcEnabled);
            demod.setAGCGain(powf(10.0f, agcGain / 20.0f));
        }

        void start() { demod.start(); }

        void stop() { demod.stop(); }

        void showMenu() {
            float menuWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Checkbox(("AGC##_radio_cwr_agc_ena_" + name).c_str(), &agcEnabled)) {
                demod.setAGCEnabled(agcEnabled);
                _config->acquire();
                _config->conf[name][getName()]["agcEnabled"] = agcEnabled;
                if (!agcEnabled) {
                    // Keep the last AGC gain as the manual gain
                    _config->conf[name][getName()]["agcGain"] = agcGain;
                }
                _config->release(true);
            }
            if (agcEnabled) {
                agcGain = std::clamp<float>(20.0f * log10f(demod.getAGCGain()), -10.0f, 90.0f);
                ImGui::BeginDisabled();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_cwr_gain_" + name).c_str(), &agcGain, -10.0f, 90.0f, "%.0f dB")) {
                demod.setAGCGain(powf(10.0f, agcGain / 20.0f));
                _config->acquire();
                _config->conf[name][getName()]["agcGain"] = agcGain;
                _config->release(true);
            }
            if (agcEnabled) { ImGui::EndDisabled(); }
            else { ImGui::BeginDisabled(); }
            ImGui::LeftLabel("AGC Attack");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_cwr_agc_attack_" + name).c_str(), &agcAttack, 1.0f, 200.0f)) {
                demod.setAGCAttack(agcAttack / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcAttack"] = agcAttack;
                _config->release(true);
            }
            ImGui::LeftLabel("AGC Decay");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::SliderFloat(("##_radio_cwr_agc_decay_" + name).c_str(), &agcDecay, 1.0f, 20.0f)) {
                demod.setAGCDecay(agcDecay / getIFSampleRate());
                _config->acquire();
                _config->conf[name][getName()]["agcDecay"] = agcDecay;
                _config->release(true);
            }
            if (!agcEnabled) { ImGui::EndDisabled(); }
            ImGui::LeftLabel("Tone Frequency");
            ImGui::FillWidth();
            if (ImGui::InputInt(("Stereo##_radio_cwr_tone_" + name).c_str(), &tone, 10, 100)) {
                tone = std::clamp<int>(tone, 250, 1250);
                demod.setTone(-tone);
                _config->acquire();
                _config->conf[name][getName()]["tone"] = tone;
                _config->release(true);
            }
        }

        void setBandwidth(double bandwidth) {}

        void setInput(dsp::stream<dsp::complex_t>* input) { demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        const char* getName() { return "CW-R"; }
        double getIFSampleRate() { return 3000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 200.0; }
        double getMinBandwidth() { return 50.0; }
        double getMaxBandwidth() { return 500.0; }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 10.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return false; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return false; }
        bool getNBAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &demod.out; }

    private:
        ConfigManager* _config = NULL;
        dsp::demod::CW<dsp::stereo_t> demod;

        std::string name;

        bool agcEnabled = true;
        float agcGain = 0.0f;
        float agcAttack = 100.0f;
        float agcDecay = 5.0f;
        int tone = 800;

        EventHandler<float> afbwChangeHandler;
    };
}