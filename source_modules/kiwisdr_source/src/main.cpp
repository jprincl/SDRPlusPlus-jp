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
#include "utils/url.h"
#include "gui/smgui.h"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <chrono>
#include <ctime>
#include <cstring>
#include <fstream>
#include <thread>
#include <atomic>
#include <gui/brown/kiwisdr_map.h>


SDRPP_MOD_INFO{
    /* Name:            */ "kiwisdr_source",
    /* Description:     */ "KiwiSDR WebSDR source module for SDR++",
    /* Author:          */ "san",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};


ConfigManager config;

static std::tm safeLocalTime(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

struct KiwiSDRSourceModule : public ModuleManager::Instance {
    using Clock = std::chrono::steady_clock;

    static constexpr int CHUNK_SAMPLES = 200; // ~16.7 ms at 12 kHz, sent 60x/s
    static constexpr int BUFFER_MS_MIN = 100;
    static constexpr int BUFFER_MS_MAX = 2000;

    // Fallback tuning range for a plain (converter-less) KiwiSDR when the
    // server's served band is unknown (manually typed host, no directory
    // entry). KiwiSDR/KiwiSDR 2 are specified as 10 kHz - 30 MHz receivers,
    // which covers the overwhelming majority of servers.
    static constexpr uint64_t DEFAULT_MIN_FREQ = 10000ull;     // 10 kHz
    static constexpr uint64_t DEFAULT_MAX_FREQ = 30000000ull;  // 30 MHz

    char kiwisdrHost[1024] = "";
    int kiwisdrPort = 8073;
    std::string kiwisdrLoc = "";
    // Served frequency range of the selected server, if known (from the
    // directory 'bands' field). Used to constrain the tuning UI.
    std::optional<ServerEntry::FrequencyBand> selectedBand;
    KiwiSDRClient kiwiSdrClient;
    std::string root;
    KiwiSDRMapSelector selector;

    std::string kiwisdrHostPort() const {
        return std::string(kiwisdrHost) + ":" + std::to_string(kiwisdrPort);
    }

    KiwiSDRSourceModule(std::string name, const std::string &root) : kiwiSdrClient(), selector(root, &config, "KiwiSDR Source") {
        this->name = name;
        this->root = root;

        config.acquire();
        if (config.conf.contains("kiwisdr_host")) {
            std::string host = config.conf["kiwisdr_host"];
            std::strncpy(kiwisdrHost, host.c_str(), sizeof(kiwisdrHost) - 1);
            kiwisdrHost[sizeof(kiwisdrHost) - 1] = '\0';
        }
        if (config.conf.contains("kiwisdr_port")) {
            kiwisdrPort = config.conf["kiwisdr_port"];
        }
        if (config.conf.contains("kiwisdr_loc")) {
            kiwisdrLoc = config.conf["kiwisdr_loc"];
        }
        if (config.conf.contains("kiwisdr_band_start") && config.conf.contains("kiwisdr_band_end")) {
            ServerEntry::FrequencyBand b;
            b.startHz = config.conf["kiwisdr_band_start"];
            b.endHz = config.conf["kiwisdr_band_end"];
            if (b.startHz <= b.endHz) {
                selectedBand = b;
            }
        }
        if (config.conf.contains("kiwisdr_buffer_ms")) {
            bufferMs = std::clamp<int>(config.conf["kiwisdr_buffer_ms"], BUFFER_MS_MIN, BUFFER_MS_MAX);
        }
        config.release(false);

        kiwiSdrClient.init(kiwisdrHostPort());

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

    // Constrain the main-window frequency selector to the server's served
    // range: the known band if we have one, otherwise the plain-KiwiSDR
    // default. Also trims the number of visible digits in the display.
    void applyFreqLimits() {
        if (selectedBand) {
            // The directory 'bands' field often reports a start of 0; a plain
            // KiwiSDR isn't usefully tunable below 10 kHz, so keep that floor.
            gui::freqSelect.minFreq = std::max<uint64_t>(selectedBand->startHz, DEFAULT_MIN_FREQ);
            gui::freqSelect.maxFreq = selectedBand->endHz;
        }
        else {
            gui::freqSelect.minFreq = DEFAULT_MIN_FREQ;
            gui::freqSelect.maxFreq = DEFAULT_MAX_FREQ;
        }
        gui::freqSelect.limitFreq = true;
    }

    static void menuSelected(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        core::setInputSampleRate(12000); // fixed for kiwisdr
        _this->applyFreqLimits();
        flog::info("KiwiSDRSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        // Release the range constraint so a stale KiwiSDR band doesn't leak
        // into whatever source is selected next.
        gui::freqSelect.limitFreq = false;
        flog::info("KiwiSDRSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        if (_this->running.load()) { return; }
        if (_this->feederThread.joinable()) {
            _this->feederThread.join();
        }
        if (_this->running.exchange(true)) { return; }
        _this->kiwiSdrClient.start();
        _this->nextSend = {};
        _this->timeSet = false;
        _this->feederThread = std::thread([=]() {
            Clock::time_point nextSend{};
            while (_this->running.load()) {
                // Prebuffer: don't start (and don't resume after an underrun)
                // until bufferMs worth of samples are queued, so network
                // jitter up to that size doesn't interrupt the stream.
                const size_t primeSamples = std::max<size_t>(CHUNK_SAMPLES,
                    (size_t)_this->bufferMs.load() * _this->kiwiSdrClient.IQDATA_FREQUENCY / 1000);
                _this->kiwiSdrClient.iqDataLock.lock();
                auto bufsize = _this->kiwiSdrClient.iqData.size();
                _this->kiwiSdrClient.iqDataLock.unlock();
                auto now = Clock::now();
                if (nextSend == Clock::time_point{}) {
                    if (bufsize < primeSamples) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // some sleep
                        continue;      // priming the prebuffer
                    }
                    nextSend = now;
                }
                else if (now < nextSend) {
                    std::this_thread::sleep_for(nextSend - now);
                }
                std::vector<std::complex<float>> toSend;
                {
                    std::lock_guard<std::mutex> lock(_this->kiwiSdrClient.iqDataLock);
                    auto& iq = _this->kiwiSdrClient.iqData;
                    // After a network stall the server bursts its backlog;
                    // playing it out at 1x would keep that stall as added
                    // latency forever. Allow 1 s of slack over the prebuffer
                    // target, then drop the oldest samples back down to the
                    // target: one IQ discontinuity instead of the
                    // time-compressed signal a catch-up drain would produce.
                    const size_t highWater = primeSamples + _this->kiwiSdrClient.IQDATA_FREQUENCY;
                    if (iq.size() > highWater) {
                        iq.erase(iq.begin(), iq.begin() + (iq.size() - primeSamples));
                    }
                    if (iq.size() >= CHUNK_SAMPLES) {
                        toSend.assign(iq.begin(), iq.begin() + CHUNK_SAMPLES);
                        iq.erase(iq.begin(), iq.begin() + CHUNK_SAMPLES);
                    }
                }
                nextSend += std::chrono::microseconds(1000000 / 60);
                if (!toSend.empty()) {
                    memcpy(_this->stream.writeBuf, toSend.data(), toSend.size() * sizeof(dsp::complex_t));
                    _this->stream.swap((int)toSend.size());
                }
                else {
                    nextSend = {}; // underrun: go back to priming
                }
            }
        });
        flog::info("KiwiSDRSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;
        bool wasRunning = _this->running.exchange(false);
        if (!wasRunning && !_this->kiwiSdrClient.running.load() && !_this->feederThread.joinable()) { return; }
        _this->kiwiSdrClient.stop();

        if (_this->kiwiSdrClient.looperThread.get_id() == std::this_thread::get_id()) {
            // Reached from the client's own network thread (onDisconnected ->
            // setPlayState(false) -> SourceManager::stop). Joining the feeder
            // here can race a concurrent GUI-thread stop() joining the same
            // std::thread, which is UB. The feeder exits on running==false;
            // the next start(), a later stop() or the destructor joins it.
            flog::info("KiwiSDRSourceModule '{0}': Stop (from network thread)!", _this->name);
            return;
        }
        if (_this->feederThread.joinable()) {
            _this->feederThread.join();
        }
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
        if (_this->running.load() && _this->connected.load()) {
            _this->kiwiSdrClient.tune(freq, KiwiSDRClient::TUNE_IQ);
        }
        flog::info("KiwiSDRSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }


    static void menuHandler(void* ctx) {

        KiwiSDRSourceModule* _this = (KiwiSDRSourceModule*)ctx;

        // While connected, refine the tuning range to the exact span the
        // server reports (freq_offset + bandwidth). This supersedes the
        // directory-band / default estimate set on select, and corrects for
        // converter-equipped Kiwis and 32 MHz wideband units. Runs on the GUI
        // thread, so it's the safe place to touch gui::freqSelect.
        if (_this->connected.load()) {
            if (auto range = _this->kiwiSdrClient.getServedRange()) {
                const uint64_t lo = std::max<int64_t>(range->minHz, (int64_t)DEFAULT_MIN_FREQ);
                const uint64_t hi = std::max<int64_t>(range->maxHz, range->minHz);
                if (gui::freqSelect.minFreq != lo || gui::freqSelect.maxFreq != hi || !gui::freqSelect.limitFreq) {
                    gui::freqSelect.minFreq = lo;
                    gui::freqSelect.maxFreq = hi;
                    gui::freqSelect.limitFreq = true;
                }
            }
        }

        if (core::args["server"].b()) {

        } else {
            // local ui
            ImGui::BeginDisabled(gui::mainWindow.isPlaying());
            if (doFingerButton("Choose on map...")) {
                _this->selector.openPopup();
            }
            ImGui::EndDisabled();

            _this->selector.drawPopup([=](const std::string &hostPort, const std::string &loc, const std::optional<ServerEntry::FrequencyBand> &band) {
                auto parsed = url::splitHostPort(hostPort);
                std::string host = parsed ? parsed->host : hostPort;
                int port = parsed ? parsed->port : 8073;
                std::strncpy(_this->kiwisdrHost, host.c_str(), sizeof(_this->kiwisdrHost) - 1);
                _this->kiwisdrHost[sizeof(_this->kiwisdrHost) - 1] = '\0';
                _this->kiwisdrPort = port;
                _this->kiwisdrLoc = loc;
                _this->selectedBand = band;
                config.acquire();
                config.conf["kiwisdr_host"] = host;
                config.conf["kiwisdr_port"] = port;
                config.conf["kiwisdr_loc"] = _this->kiwisdrLoc;
                if (band) {
                    config.conf["kiwisdr_band_start"] = band->startHz;
                    config.conf["kiwisdr_band_end"] = band->endHz;
                }
                else {
                    config.conf.erase("kiwisdr_band_start");
                    config.conf.erase("kiwisdr_band_end");
                }
                config.release(true);
                // The KiwiSDR source is the active source while this popup is
                // open, so retune the digit limits to the newly chosen server.
                _this->applyFreqLimits();
                _this->kiwiSdrClient.init(_this->kiwisdrHostPort());
            });

            ImGui::BeginDisabled(gui::mainWindow.isPlaying());
            if (SmGui::InputText(("##_kiwisdr_host_" + _this->name).c_str(),
                                 _this->kiwisdrHost, sizeof(_this->kiwisdrHost))) {
                config.acquire();
                config.conf["kiwisdr_host"] = std::string(_this->kiwisdrHost);
                config.release(true);
            }
            bool hostCommitted = ImGui::IsItemDeactivatedAfterEdit();
            SmGui::SameLine();
            SmGui::FillWidth();
            if (SmGui::InputInt(("##_kiwisdr_port_" + _this->name).c_str(), &_this->kiwisdrPort, 0, 0)) {
                config.acquire();
                config.conf["kiwisdr_port"] = _this->kiwisdrPort;
                config.release(true);
            }
            bool portCommitted = ImGui::IsItemDeactivatedAfterEdit();
            if (hostCommitted || portCommitted) {
                _this->kiwisdrLoc.clear();
                // A hand-typed host has no directory band; fall back to the
                // default range rather than keeping the previous server's.
                _this->selectedBand.reset();
                config.acquire();
                config.conf["kiwisdr_loc"] = _this->kiwisdrLoc;
                config.conf.erase("kiwisdr_band_start");
                config.conf.erase("kiwisdr_band_end");
                config.release(true);
                _this->applyFreqLimits();
                _this->kiwiSdrClient.init(_this->kiwisdrHostPort());
            }
            ImGui::EndDisabled();
        }

        if (!_this->kiwisdrLoc.empty())
            SmGui::Text(("Loc: " + _this->kiwisdrLoc).c_str());
        SmGui::Text(("Status: " + _this->kiwiSdrClient.getConnectionStatus()).c_str());

        std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tmm = safeLocalTime(t);
        char streamTime[64];
        strftime(streamTime, sizeof(streamTime), "%Y-%m-%d %H:%M:%S", &tmm);
        SmGui::Text(("Stream pos: " + std::string(streamTime)).c_str());

        // Network prebuffer: trades latency for immunity against connection
        // jitter (mobile links). Takes effect live, including while playing.
        int bufMs = _this->bufferMs.load();
        SmGui::LeftLabel("Buffer (ms)");
        SmGui::FillWidth();
        if (SmGui::SliderInt(("##_kiwisdr_buffer_ms_" + _this->name).c_str(), &bufMs, BUFFER_MS_MIN, BUFFER_MS_MAX)) {
            _this->bufferMs.store(bufMs);
            config.acquire();
            config.conf["kiwisdr_buffer_ms"] = bufMs;
            config.release(true);
        }

        KiwiSDRClient::AgcSettings agc = _this->kiwiSdrClient.getAgc();
        bool agcChanged = false;
        const std::string agcId = "AGC##_kiwisdr_agc_" + _this->name;
        agcChanged |= SmGui::Checkbox(agcId.c_str(), &agc.enabled);
        SmGui::SameLine();
        const std::string hangId = "Hang##_kiwisdr_agc_hang_" + _this->name;
        agcChanged |= SmGui::Checkbox(hangId.c_str(), &agc.hang);

        auto slider = [&](const char* label, const char* id, int* value, int min, int max, SmGui::FormatString format) {
            SmGui::LeftLabel(label);
            SmGui::FillWidth();
            const std::string sliderId = std::string(id) + _this->name;
            return SmGui::SliderInt(sliderId.c_str(), value, min, max, format);
        };

        if (agc.enabled) {
            agcChanged |= slider("Threshold", "##_kiwisdr_agc_threshold_", &agc.thresholdDb, -130, 0, SmGui::FMT_STR_INT_DB);
            agcChanged |= slider("Slope", "##_kiwisdr_agc_slope_", &agc.slopeDb, 0, 10, SmGui::FMT_STR_INT_DB);
            agcChanged |= slider("Decay", "##_kiwisdr_agc_decay_", &agc.decayMs, 20, 5000, SmGui::FMT_STR_INT_DEFAULT);
        }
        else {
            agcChanged |= slider("Gain", "##_kiwisdr_agc_gain_", &agc.manualGainDb, 0, 120, SmGui::FMT_STR_INT_DB);
        }
        if (agcChanged) {
            _this->kiwiSdrClient.setAgc(agc);
        }
    }


    std::string name;
    bool enabled = true;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    // Prebuffer target in milliseconds; read by the feeder thread each
    // iteration so UI changes take effect live.
    std::atomic<int> bufferMs{250};
    bool timeSet = false;

    double freq;
    bool serverBusy = false;

    dsp::stream<dsp::complex_t> stream;
    std::thread feederThread;
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
