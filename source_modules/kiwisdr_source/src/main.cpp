#define IMGUI_DEFINE_MATH_OPERATORS
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#endif

#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/widgets/simple_widgets.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <config.h>
#include "utils/proto/kiwisdr.h"
#include "gui/smgui.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <fstream>
#include <thread>
#include <gui/brown/kiwisdr_map.h>


SDRPP_MOD_INFO{
    /* Name:            */ "kiwisdr_source",
    /* Description:     */ "KiwiSDR WebSDR source module for SDR++",
    /* Author:          */ "san",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};


ConfigManager config;

struct KiwiSDRSourceModule : public ModuleManager::Instance {
    using Clock = std::chrono::steady_clock;

    std::string kiwisdrSite = "sk6ag1.ddns.net:8071";
    std::string kiwisdrLoc = "";
    //    std::string kiwisdrSite = "kiwi-iva.aprs.fi";
    KiwiSDRClient kiwiSdrClient;
    std::string root;
    KiwiSDRMapSelector selector;

    KiwiSDRSourceModule(std::string name, const std::string &root) : kiwiSdrClient(), selector(root, &config, "KiwiSDR Source") {
        this->name = name;
        this->root = root;

        config.acquire();
        if (config.conf.contains("kiwisdr_site")) {
            kiwisdrSite = config.conf["kiwisdr_site"];
        }
        if (config.conf.contains("kiwisdr_loc")) {
            kiwisdrLoc = config.conf["kiwisdr_loc"];
        }
        config.release(false);


        kiwiSdrClient.init(kiwisdrSite);

        // Yeah no server-ception, sorry...
        // Initialize lists
        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        kiwiSdrClient.onConnected = [&]() {
            connected = true;
            tune(lastTuneFrequency, this);
        };

        kiwiSdrClient.onDisconnected = [&]() {
            connected = false;
            running = false;
            gui::mainWindow.setPlayState(false);
        };



        // Load config

        sigpath::sourceManager.registerSource("KiwiSDR", &handler);
    }

    ~KiwiSDRSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("KiwiSDR");
    }

    void postInit() {
    }

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    static void menuSelected(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        core::setInputSampleRate(12000); // fixed for kiwisdr
        flog::info("KiwiSDRSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        flog::info("KiwiSDRSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        if (_this->running) { return; }
        _this->running = true;
        _this->kiwiSdrClient.start();
        _this->nextSend = {};
        _this->timeSet = false;
        std::thread feeder([=]() {
            Clock::time_point nextSend{};
            while (_this->running) {
                _this->kiwiSdrClient.iqDataLock.lock();
                auto bufsize = _this->kiwiSdrClient.iqData.size();
                _this->kiwiSdrClient.iqDataLock.unlock();
                auto now = Clock::now();
                if (nextSend == Clock::time_point{}) {
                    if (bufsize < 200) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // some sleep
                        continue;      // waiting for initial batch
                    }
                    nextSend = now;
                }
                else if (now < nextSend) {
                    std::this_thread::sleep_for(nextSend - now);
                }
                std::vector<std::complex<float>> toSend;
                int bufferSize = 0;
                _this->kiwiSdrClient.iqDataLock.lock();
                if (_this->kiwiSdrClient.iqData.size() >= 200) {
                    for (int i = 0; i < 200; i++) {
                        toSend.emplace_back(_this->kiwiSdrClient.iqData[i]);
                    }
                    _this->kiwiSdrClient.iqData.erase(_this->kiwiSdrClient.iqData.begin(), _this->kiwiSdrClient.iqData.begin() + 200);
                    bufferSize = _this->kiwiSdrClient.iqData.size();
                }
                _this->kiwiSdrClient.iqDataLock.unlock();
                if (bufferSize > _this->kiwiSdrClient.NETWORK_BUFFER_SIZE) {
                    nextSend += std::chrono::microseconds(1000000 / 120);
                }
                else {
                    nextSend += std::chrono::microseconds(1000000 / 60);
                }
                if (!toSend.empty()) {
                    memcpy(_this->stream.writeBuf, toSend.data(), toSend.size() * sizeof(dsp::complex_t));
                    _this->stream.swap((int)toSend.size());
                }
                else {
                    nextSend = {};
                }
            }
        });
        feeder.detach();

        _this->running = true;
        flog::info("KiwiSDRSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->kiwiSdrClient.stop();

        _this->running = false;
        flog::info("KiwiSDRSourceModule '{0}': Stop!", _this->name);
    }

    std::vector<dsp::complex_t> incomingBuffer;

    Clock::time_point nextSend{};

    void incomingSample(double i, double q) {
        incomingBuffer.emplace_back(dsp::complex_t{ (float)q, (float)i });
        if (incomingBuffer.size() >= 200) { // 60 times per second
            auto now = Clock::now();
            if (nextSend == Clock::time_point{}) {
                nextSend = now;
            }
            else if (now < nextSend) {
                std::this_thread::sleep_for(nextSend - now);
            }
            nextSend += std::chrono::microseconds(1000000 / 60);
            incomingBuffer.clear();
        }
    }


    double lastTuneFrequency = 14.100;

    static void tune(double freq, void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        _this->lastTuneFrequency = freq;
        if (_this->running && _this->connected) {
            _this->kiwiSdrClient.tune(freq, KiwiSDRClient::TUNE_IQ);
        }
        flog::info("KiwiSDRSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }


    static void menuHandler(void* ctx) {

        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;

        if (core::args["server"].b()) {

        } else {
            // local ui
            ImGui::BeginDisabled(gui::mainWindow.isPlaying());
            if (doFingerButton("Choose on map...")) {
                _this->selector.openPopup();
            }
            ImGui::EndDisabled();

            _this->selector.drawPopup([=](const std::string &hostPort, const std::string &loc) {
                _this->kiwisdrSite = hostPort;
                _this->kiwisdrLoc = loc;
                config.acquire();
                config.conf["kiwisdr_site"] = _this->kiwisdrSite;
                config.conf["kiwisdr_loc"] = _this->kiwisdrLoc;
                config.release(true);
                _this->kiwiSdrClient.init(_this->kiwisdrSite);
            });
        }




        SmGui::Text(("KiwiSDR site: " + _this->kiwisdrSite).c_str());
        SmGui::Text(("Loc: " + _this->kiwisdrLoc).c_str());
        SmGui::Text(("Status: " + std::string(_this->kiwiSdrClient.connectionStatus)).c_str());

        std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tmm = std::localtime(&t);
        char streamTime[64];
        strftime(streamTime, sizeof(streamTime), "%Y-%m-%d %H:%M:%S", tmm);
        SmGui::Text(("Stream pos: " + std::string(streamTime)).c_str());
    }


    std::string name;
    bool enabled = true;
    bool running = false;
    bool connected = false;
    bool timeSet = false;

    double freq;
    bool serverBusy = false;

    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;

    std::shared_ptr<KiwiSDRClient> client;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/kiwisdr_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    auto root = core::args["root"].s();
    return new KiwiSDRSourceModule(name, root);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (KiwiSDRSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
