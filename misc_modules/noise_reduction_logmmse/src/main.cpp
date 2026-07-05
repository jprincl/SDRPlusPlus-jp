#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <config.h>
#include <core.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include "signal_path/signal_path.h"
#include <radio_interface.h>
#include "if_nr.h"
#include "af_nr.h"

// Ported from SDR++Brown (https://github.com/sannysanoff/SDRPlusPlusBrown),
// misc_modules/noise_reduction_logmmse. Trimmed relative to the original:
//  - SNR chart widget removed (needs Brown-only SNR meter extension points).
//  - Per-VFO IF-chain LogMMSE (AFNRLogMMSE) removed: dead code upstream,
//    its radio IFCHAIN interface commands are intentionally not ported.
// Kept: wideband baseband NR (LogMMSE IQ preprocessor) and per-radio
// OMLSA-MCRA audio NR ("Audio NR2").

ConfigManager config;

SDRPP_MOD_INFO{
    /* Name:            */ "noise_reduction_logmmse",
    /* Description:     */ "LOGMMSE noise reduction",
    /* Author:          */ "sannysanoff",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

class NRModule : public ModuleManager::Instance {

    dsp::IFNRLogMMSE ifnrProcessor;

    std::unordered_map<std::string, std::shared_ptr<dsp::AFNR_OMLSA_MCRA>> afnrProcessors2; // instance by radio name.

public:
    NRModule(std::string name) {
        this->name = name;
        config.acquire();
        if (config.conf.contains("IFNR")) ifnr = config.conf["IFNR"];
        if (config.conf.contains("DisableCpuDeactivation")) disableCpuDeactivation = config.conf["DisableCpuDeactivation"];
        config.release(true);

        ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        updateBindings();
        actuateIFNR();
    }

    ~NRModule() {
        disable(); // detach from IQ chain, radios and events before dying
        gui::menu.removeEntry(name);
    }

    void postInit() {}

    void enable() {
        if (!enabled) {
            enabled = true;
            ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);
            updateBindings();
            actuateIFNR();
        }
    }

    void disable() {
        if (enabled) {
            enabled = false;
            actuateIFNR();
            updateBindings();
        }
    }

    bool isEnabled() {
        return enabled;
    }


private:
    bool ifnr = false;
    bool disableCpuDeactivation = false;

    void attachAFToRadio(const std::string& instanceName) {
        const std::shared_ptr<dsp::AFNR_OMLSA_MCRA> afnromlsa = std::make_shared<dsp::AFNR_OMLSA_MCRA>();
        afnromlsa->init(nullptr);
        afnrProcessors2[instanceName] = afnromlsa;
        core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_ADD_TO_AFCHAIN, afnromlsa.get(), NULL);
        core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_ENABLE_IN_AFCHAIN, afnromlsa.get(), NULL);
        config.acquire();

        bool afnr2 = false;
        if (config.conf.contains("AF_NR2_" + instanceName)) afnr2 = config.conf["AF_NR2_" + instanceName];

        config.release(true);

        afnromlsa->allowed = afnr2;
    }

    void detachAFFromRadio(const std::string& instanceName) {
        if (afnrProcessors2.find(instanceName) != afnrProcessors2.end()) {
            core::modComManager.callInterface(instanceName, RADIO_IFACE_CMD_REMOVE_FROM_AFCHAIN,
                                              afnrProcessors2[instanceName].get(), NULL);
            afnrProcessors2.erase(instanceName);
        }
    }

    void updateBindings() {
        if (enabled) {
            flog::info("Enabling noise reduction things");

            sigpath::iqFrontEnd.addPreprocessor(&ifnrProcessor, false);

            currentFrequencyChangedHandler.ctx = this;
            currentFrequencyChangedHandler.handler = [](double v, void* ctx) {
                auto _this = (NRModule*)ctx;
                _this->ifnrProcessor.reset(); // reset noise profile
            };
            sigpath::sourceManager.onRetune.bindHandler(&currentFrequencyChangedHandler);

            auto names = core::modComManager.findInterfaces("radio");
            for (auto& name : names) {
                attachAFToRadio(name);
            }
            instanceCreatedHandler.ctx = this;
            instanceCreatedHandler.handler = [](std::string v, void* ctx) {
                auto _this = (NRModule*)ctx;
                auto modname = core::moduleManager.getInstanceModuleName(v);
                if (modname == "radio") {
                    // radio created after the NR module.
                    _this->attachAFToRadio(v);
                }
            };
            core::moduleManager.onInstanceCreated.bindHandler(&instanceCreatedHandler);
        }
        else {
            core::moduleManager.onInstanceCreated.unbindHandler(&instanceCreatedHandler);
            sigpath::sourceManager.onRetune.unbindHandler(&currentFrequencyChangedHandler);

            // Detach OMLSA processors from the radios' AF chains. Without this,
            // re-enabling would attach fresh processors while the old (freed)
            // ones are still wired into the chains.
            std::vector<std::string> attached;
            for (auto& [k, v] : afnrProcessors2) { attached.push_back(k); }
            for (auto& k : attached) { detachAFFromRadio(k); }

            sigpath::iqFrontEnd.removePreprocessor(&ifnrProcessor);
        }
    }

    void actuateIFNR() {
        bool shouldRun = enabled && ifnr;
        if (ifnrProcessor.bypass != !shouldRun) {
            ifnrProcessor.bypass = !shouldRun;
            sigpath::iqFrontEnd.togglePreprocessor(&ifnrProcessor, shouldRun);
        }
    }

    void menuHandler() {
        if (ImGui::Checkbox("Baseband NR##_sdrpp_if_nr", &ifnr)) {
            config.acquire();
            config.conf["IFNR"] = ifnr;
            config.release(true);
            if (ifnr) { // toggled on - attempt to run.
                ifnrProcessor.stopReason = nullptr;
            }
            actuateIFNR();
        }
        ImGui::SameLine();
        const char* stopReason = ifnrProcessor.stopReason;
        if (stopReason && ifnr) { // wants to stop -> stop it.
            ifnr = false;
            config.acquire();
            config.conf["IFNR"] = ifnr;
            config.release(true);
            actuateIFNR();
        }
        if (stopReason) { // stopped because reason -> show it.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
            ImGui::Text("%s", stopReason);
            ImGui::PopStyleColor(1);
        }
        else {
            // show cpu usage
            if (ifnrProcessor.percentUsage >= 0) {
                if (ifnrProcessor.percentUsage > 80) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0, 0, 1.0f));
                }
                std::string cpuText = std::to_string((int)ifnrProcessor.percentUsage) + "% cpu";
                ImVec2 textSize = ImGui::CalcTextSize(cpuText.c_str());
                bool clicked = ImGui::Selectable(cpuText.c_str(), false, ImGuiSelectableFlags_None, textSize);
                if (clicked) {
                    disableCpuDeactivation = !disableCpuDeactivation;
                    ifnrProcessor.setDisableCpuDeactivation(disableCpuDeactivation);
                    config.acquire();
                    config.conf["DisableCpuDeactivation"] = disableCpuDeactivation;
                    config.release(true);
                }
                if (disableCpuDeactivation) {
                    auto drawList = ImGui::GetWindowDrawList();
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();
                    drawList->AddLine(ImVec2(min.x, (min.y + max.y) / 2), ImVec2(max.x, (min.y + max.y) / 2), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
                }
                if (ifnrProcessor.percentUsage > 80) {
                    ImGui::PopStyleColor(1);
                }
            }
        }

        for (auto [k, v] : afnrProcessors2) {
            if (ImGui::Checkbox(("Audio NR2 " + k + "##_radio_omlsa_nr_" + k).c_str(), &v->allowed)) {
                config.acquire();
                config.conf["AF_NR2_" + k] = v->allowed;
                config.release(true);
            }
            ImGui::SameLine();
            ImGui::Text("%0.01f", 32767.0 / v->scaled);
        }
    }

    static void menuHandler(void* ctx) {
        NRModule* _this = (NRModule*)ctx;
        _this->menuHandler();
    }

    std::string name;
    bool enabled = true;
    EventHandler<double> currentFrequencyChangedHandler;
    EventHandler<std::string> instanceCreatedHandler;
};


MOD_EXPORT void _INIT_() {
    config.setPath(core::args["root"].s() + "/noise_reduction_logmmse_config.json");
    config.load(json::object());
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new NRModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (NRModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
