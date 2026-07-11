#pragma once
#include "../demod.h"
#include <dsp/demod/cw.h>

namespace demod {
    class CW : public Demodulator {
    public:
        CW() {}

        CW(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~CW() {
            stop();
        }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;
            this->_config = config;
            this->afbwChangeHandler = afbwChangeHandler;

            // Load config
            config->acquire();
            agc.load(config->conf[name][getName()]);
            loadConf(config->conf[name][getName()], "tone", tone);
            config->release();

            // Define structure
            demod.init(input, tone, agc.attack / getIFSampleRate(), agc.decay / getIFSampleRate(), getIFSampleRate());
            agc.apply(demod);
        }

        void start() { demod.start(); }

        void stop() { demod.stop(); }

        void showMenu() {
            agc.showMenu(demod, this, "cw", getIFSampleRate(), ImGui::GetContentRegionAvail().x);
            ImGui::LeftLabel("Tone Frequency");
            ImGui::FillWidth();
            if (ImGui::InputInt(("Stereo##_radio_cw_tone_" + name).c_str(), &tone, 10, 100)) {
                tone = std::clamp<int>(tone, 250, 1250);
                demod.setTone(tone);
                saveConf("tone", tone);
            }
        }

        void setBandwidth(double bandwidth) {}

        void setInput(dsp::stream<dsp::complex_t>* input) { demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "CW"; }
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
        bool getHighPassAllowed() { return false; }
        bool getSquelchAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &demod.out; }

    private:
        dsp::demod::CW<dsp::stereo_t> demod;

        AGCControls agc = { true, 0.0f, 100.0f, 5.0f };
        int tone = 800;

        EventHandler<float> afbwChangeHandler;
    };
}