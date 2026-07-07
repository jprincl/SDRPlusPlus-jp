#pragma once
#include "../demod.h"
#include <dsp/demod/fm.h>

namespace demod {
    class NFM : public Demodulator {
    public:
        NFM() {}

        NFM(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            init(name, config, input, bandwidth, audioSR);
        }

        ~NFM() { stop(); }

        void init(std::string name, ConfigManager* config, dsp::stream<dsp::complex_t>* input, double bandwidth, double audioSR) {
            this->name = name;
            this->_config = config;

            // Load config
            _config->acquire();
            auto& cfg = config->conf[name][getName()];
            loadConf(cfg, "lowPass", _lowPass);
            loadConf(cfg, "highPass", _highPass);
            _config->release();


            // Define structure
            demod.init(input, getIFSampleRate(), bandwidth, _lowPass, _highPass);
        }

        void start() { demod.start(); }

        void stop() { demod.stop(); }

        void showMenu() {
            if (ImGui::Checkbox(("Low Pass##_radio_wfm_lowpass_" + name).c_str(), &_lowPass)) {
                demod.setLowPass(_lowPass);
                saveConf("lowPass", _lowPass);
            }
            if (ImGui::Checkbox(("High Pass##_radio_wfm_highpass_" + name).c_str(), &_highPass)) {
                demod.setHighPass(_highPass);
                saveConf("highPass", _highPass);
            }
        }

        void setBandwidth(double bandwidth) {
            demod.setBandwidth(bandwidth);
        }

        void setInput(dsp::stream<dsp::complex_t>* input) { demod.setInput(input); }

        void AFSampRateChanged(double newSR) {}

        // ============= INFO =============

        const char* getName() { return "FM"; }
        double getIFSampleRate() { return 50000.0; }
        double getAFSampleRate() { return getIFSampleRate(); }
        double getDefaultBandwidth() { return 12500.0; }
        double getMinBandwidth() { return 1000.0; }
        double getMaxBandwidth() { return getIFSampleRate(); }
        bool getBandwidthLocked() { return false; }
        double getDefaultSnapInterval() { return 2500.0; }
        int getVFOReference() { return ImGui::WaterfallVFO::REF_CENTER; }
        bool getDeempAllowed() { return true; }
        bool getPostProcEnabled() { return true; }
        int getDefaultDeemphasisMode() { return DEEMP_MODE_NONE; }
        bool getFMIFNRAllowed() { return true; }
        bool getNBAllowed() { return false; }
        dsp::stream<dsp::stereo_t>* getOutput() { return &demod.out; }

    private:
        dsp::demod::FM<dsp::stereo_t> demod;

        bool _lowPass = true;
        bool _highPass = false;
    };
}