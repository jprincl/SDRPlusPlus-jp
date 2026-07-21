#include <spyserver_vfo_client.h>
#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/widgets/stepped_slider.h>
#include <gui/smgui.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>


#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "spyserver_vfo_source",
    /* Description:     */ "SpyServer source for SDR++ using VFO+FFT mode (like SDR#) instead of Full IQ - narrowband IQ for demod, server-side FFT for the waterfall, much lower bandwidth use.",
    /* Author:          */ "Community patch (see spyserver_source for the original Full-IQ module)",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

const char* svfoDeviceTypesStr[] = {
    "Unknown",
    "Airspy One",
    "Airspy HF+",
    "RTL-SDR"
};

// FFT stream is always requested as UINT8 - see the big comment in
// spyserver_vfo_client.cpp::handleFFTFrame() for why DINT4 isn't handled.
const char* svfoIqFormatStr = "UInt8\0"
                               "Int16\0"
                               "Float32\0";

const SpyServerStreamFormat svfoIqFormats[] = {
    SPYSERVER_STREAM_FORMAT_UINT8,
    SPYSERVER_STREAM_FORMAT_INT16,
    SPYSERVER_STREAM_FORMAT_FLOAT
};

const int svfoIqFormatsBitCount[] = {
    8,
    16,
    32
};

// Number of FFT bins requested from the server. Doesn't need to match
// SDR++'s locally configured FFT size - IQFrontEnd::pushExternalFFT()
// nearest-neighbour resamples if they differ.
#define SVFO_FFT_PIXELS 2048

// Max real retunes/sec sent to the server while dragging the VFO. A single
// click/tap is always sent right away regardless of this - it only caps
// the rate during a continuous drag (see the retune thread in start()).
#define SVFO_MAX_RETUNE_RATE 8

ConfigManager svfoConfig;

class SpyServerVFOSourceModule : public ModuleManager::Instance {
public:
    SpyServerVFOSourceModule(std::string name) {
        this->name = name;

        svfoConfig.acquire();
        std::string host = svfoConfig.conf["hostname"];
        port = svfoConfig.conf["port"];
        svfoConfig.release();

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &iqStream;

        strcpy(hostname, host.c_str());

        sigpath::sourceManager.registerSource("SpyServer VFO+FFT", &handler);
    }

    ~SpyServerVFOSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("SpyServer VFO+FFT");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    std::string getBandwdithScaled(double bw) {
        char buf[1024];
        if (bw >= 1000000.0) {
            sprintf(buf, "%.1lfMHz", bw / 1000000.0);
        }
        else if (bw >= 1000.0) {
            sprintf(buf, "%.1lfKHz", bw / 1000.0);
        }
        else {
            sprintf(buf, "%.1lfHz", bw);
        }
        return std::string(buf);
    }

    static void menuSelected(void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;
        core::setInputSampleRate(_this->iqSampleRate);
        // setInputSampleRate() always resets the waterfall's displayed
        // bandwidth to match the IQ rate. If we're already running (wide
        // FFT actively being pushed), immediately widen it back out -
        // otherwise re-selecting this source's menu after start() silently
        // shrinks the tunable/displayed range down to the narrow IQ span.
        if (_this->running) { core::setDisplayBandwidth(_this->fftSampleRate); }
        gui::mainWindow.playButtonLocked = !(_this->client && _this->client->isOpen());
        flog::info("SpyServerVFOSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;
        // Belt and braces: make sure we never leave the waterfall stuck
        // waiting for external pushes if the source gets swapped away
        // without going through stop() first.
        sigpath::iqFrontEnd.setExternalFFTMode(false);
        gui::mainWindow.playButtonLocked = false;
        flog::info("SpyServerVFOSourceModule '{0}': Menu Deselect!", _this->name);
    }

    // Called by the client (on its network thread) whenever a full FFT
    // frame has been decoded. Forwards it straight to the waterfall.
    static void fftFrameHandler(const float* data, int count, void* ctx) {
        sigpath::iqFrontEnd.pushExternalFFT(data, count);
    }

    void applyFFTSettings() {
        if (!client) { return; }
        client->fftDbOffset = fftDbOffset;
        client->fftDbRange = fftDbRange;
        client->setSetting(SPYSERVER_SETTING_FFT_FORMAT, SPYSERVER_STREAM_FORMAT_UINT8);
        client->setSetting(SPYSERVER_SETTING_FFT_DB_OFFSET, (uint32_t)fftDbOffset);
        client->setSetting(SPYSERVER_SETTING_FFT_DB_RANGE, (uint32_t)fftDbRange);
        client->setSetting(SPYSERVER_SETTING_FFT_DISPLAY_PIXELS, SVFO_FFT_PIXELS);
    }

    static void start(void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;
        if (_this->running) { return; }

        // Try to connect if not already connected
        if (!_this->client) {
            _this->tryConnect();
            if (!_this->client) { return; }
        }

        int srvBits = svfoIqFormatsBitCount[_this->iqType];

        // Narrowband IQ, for demodulation only - tuned to the current VFO
        // frequency, same as the original spyserver_source.
        _this->client->setSetting(SPYSERVER_SETTING_IQ_FORMAT, svfoIqFormats[_this->iqType]);
        _this->client->setSetting(SPYSERVER_SETTING_IQ_DECIMATION, _this->iqDecimId + _this->client->devInfo.MinimumIQDecimation);
        _this->client->setSetting(SPYSERVER_SETTING_IQ_FREQUENCY, _this->freq);
        _this->client->setSetting(SPYSERVER_SETTING_GAIN, _this->gain);
        _this->client->setSetting(SPYSERVER_SETTING_IQ_DIGITAL_GAIN, _this->client->computeDigitalGain(srvBits, _this->gain, _this->iqDecimId + _this->client->devInfo.MinimumIQDecimation));

        // Wide FFT, for the waterfall only - independently tuned/decimated,
        // but re-centred on the same frequency as the IQ on every retune
        // (see tune() below) to keep things simple: one frequency to think
        // about instead of a fixed wide view + a moving cursor within it.
        _this->applyFFTSettings();
        _this->client->setSetting(SPYSERVER_SETTING_FFT_FREQUENCY, _this->freq);
        _this->client->setSetting(SPYSERVER_SETTING_FFT_DECIMATION, _this->fftDecimId + _this->client->devInfo.MinimumIQDecimation);

        _this->client->setSetting(SPYSERVER_SETTING_STREAMING_MODE, SPYSERVER_STREAM_MODE_FFT_IQ);
        _this->client->startStream();

        // IQFrontEnd's own IQ->FFT computation is meaningless here (it
        // would just be computing an FFT of the narrowband demod IQ) -
        // switch it off and feed the waterfall from the server's FFT
        // stream instead.
        core::setInputSampleRate(_this->iqSampleRate);
        sigpath::iqFrontEnd.setExternalFFTMode(true);
        core::setDisplayBandwidth(_this->fftSampleRate);

        // No tuning-mode forcing needed (see tune() and the poll thread
        // below for why): this now works natively in both "normal" tuning
        // (FFT stays put, only IQ_FREQUENCY tracks the VFO cursor - SDR#
        // VFO+FFT behaviour) and "center tuning" (both move together, VFO
        // offset always 0 - degenerates to the same thing).
        _this->lastSentIqFreq = _this->freq;
        _this->lastSentFftFreq = _this->freq;
        _this->fftDirty = false;

        // Background thread. Two independent jobs, both rate-limited to
        // SVFO_MAX_RETUNE_RATE sends/sec each:
        //  - FFT_FREQUENCY: only when tune() marks it dirty (the "device
        //    center" genuinely changed - rare, e.g. VFO nearing the edge of
        //    the current view in normal tuning mode, or every move in
        //    center tuning mode).
        //  - IQ_FREQUENCY: polls the VFO's live offset from the FFT center
        //    (sigpath::vfoManager.getOffset) every cycle and sends whenever
        //    it has actually moved - this is what makes tuning within the
        //    displayed FFT work in normal mode, where the GUI never calls
        //    tune() at all for in-view VFO moves.
        // The poll itself is cheap (local reads only); only the actual
        // network sends are rate-limited.
        _this->tuneThreadRunning = true;
        _this->tuneThread = std::thread([_this]() {
            using namespace std::chrono_literals;
            const auto pollInterval = 40ms;
            const auto minSendInterval = std::chrono::milliseconds(1000 / SVFO_MAX_RETUNE_RATE);
            auto lastIqSend = std::chrono::steady_clock::now() - minSendInterval;
            auto lastFftSend = lastIqSend;

            while (_this->tuneThreadRunning) {
                std::this_thread::sleep_for(pollInterval);
                if (!_this->tuneThreadRunning || !_this->client) { continue; }

                auto now = std::chrono::steady_clock::now();

                // IQ_FREQUENCY: track the VFO's live position.
                double vfoOffset = 0.0;
                std::string vfoName = gui::waterfall.selectedVFO;
                if (vfoName != "") {
                    vfoOffset = sigpath::vfoManager.getOffset(vfoName);
                    // The IQ we receive is already retuned server-side to
                    // sit at this exact absolute frequency, so the local
                    // DSP mixer must not shift by vfoOffset again - but for
                    // asymmetric modes (USB/LSB) the filter's actual
                    // passband center isn't the same point as the tuned/
                    // clicked frequency (it's offset by half the VFO
                    // bandwidth). getCenterOffset() - getOffset() is
                    // exactly that residual: 0 for symmetric modes
                    // (AM/NFM/WFM), +-bandwidth/2 for USB/LSB.
                    double residual = sigpath::vfoManager.getCenterOffset(vfoName) - vfoOffset;
                    sigpath::vfoManager.setDspOffset(vfoName, residual);
                }
                double targetIq = gui::waterfall.getCenterFrequency() + vfoOffset;

                // DIAGNOSTIC: record every poll's computed values, not just
                // ones that get sent, so the UI can show what the module
                // itself thinks is going on even between sends.
                {
                    std::lock_guard lck(_this->dbgMtx);
                    _this->dbgVfoName = vfoName;
                }
                _this->dbgVfoOffset = vfoOffset;
                _this->dbgCenterFreq = gui::waterfall.getCenterFrequency();
                _this->dbgTargetIq = targetIq;

                if (targetIq != _this->lastSentIqFreq && (now - lastIqSend) >= minSendInterval) {
                    _this->client->setSetting(SPYSERVER_SETTING_IQ_FREQUENCY, targetIq);
                    _this->lastSentIqFreq = targetIq;
                    lastIqSend = now;
                    _this->dbgIqSendCount++;
                }

                // FFT_FREQUENCY: only on an actual device retune event.
                if (_this->fftDirty.exchange(false) && (now - lastFftSend) >= minSendInterval) {
                    double f = _this->pendingFftFreq;
                    _this->client->setSetting(SPYSERVER_SETTING_FFT_FREQUENCY, f);
                    _this->lastSentFftFreq = f;
                    lastFftSend = now;
                    _this->dbgFftSendCount++;
                }
            }
        });

        _this->running = true;
        flog::info("SpyServerVFOSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;
        if (!_this->running) { return; }

        _this->tuneThreadRunning = false;
        if (_this->tuneThread.joinable()) { _this->tuneThread.join(); }

        sigpath::iqFrontEnd.setExternalFFTMode(false);

        if (_this->client) { _this->client->stopStream(); }

        _this->running = false;
        flog::info("SpyServerVFOSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;
        _this->freq = freq;
        if (_this->running) {
            _this->pendingFftFreq = freq;
            _this->fftDirty = true;
        }
    }

    static void menuHandler(void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;

        bool connected = (_this->client && _this->client->isOpen());
        gui::mainWindow.playButtonLocked = !connected;

        if (connected) { SmGui::BeginDisabled(); }
        if (SmGui::InputText(CONCAT("##_spyserver_vfo_srv_host_", _this->name), _this->hostname, 1023)) {
            svfoConfig.acquire();
            svfoConfig.conf["hostname"] = _this->hostname;
            svfoConfig.release(true);
        }
        SmGui::SameLine();
        SmGui::FillWidth();
        if (SmGui::InputInt(CONCAT("##_spyserver_vfo_srv_port_", _this->name), &_this->port, 0, 0)) {
            svfoConfig.acquire();
            svfoConfig.conf["port"] = _this->port;
            svfoConfig.release(true);
        }
        if (connected) { SmGui::EndDisabled(); }

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (!connected && SmGui::Button("Connect##spyserver_vfo_source")) {
            _this->tryConnect();
        }
        else if (connected && SmGui::Button("Disconnect##spyserver_vfo_source")) {
            _this->client->close();
        }
        if (_this->running) { SmGui::EndDisabled(); }

        if (connected) {
            if (_this->running) { style::beginDisabled(); }

            SmGui::LeftLabel("IQ (audio) bandwidth");
            SmGui::FillWidth();
            if (SmGui::Combo("##spyserver_vfo_source_iqbw", &_this->iqDecimId, _this->iqRatesTxt.c_str())) {
                _this->iqSampleRate = _this->iqRates[_this->iqDecimId];
                svfoConfig.acquire();
                svfoConfig.conf["devices"][_this->devRef]["iqDecimId"] = _this->iqDecimId;
                svfoConfig.release(true);
            }

            SmGui::LeftLabel("FFT (waterfall) bandwidth");
            SmGui::FillWidth();
            if (SmGui::Combo("##spyserver_vfo_source_fftbw", &_this->fftDecimId, _this->fftRatesTxt.c_str())) {
                _this->fftSampleRate = _this->fftRates[_this->fftDecimId];
                svfoConfig.acquire();
                svfoConfig.conf["devices"][_this->devRef]["fftDecimId"] = _this->fftDecimId;
                svfoConfig.release(true);
            }

            if (_this->running) { style::endDisabled(); }

            SmGui::LeftLabel("IQ sample bit depth");
            SmGui::FillWidth();
            if (SmGui::Combo("##spyserver_vfo_source_type", &_this->iqType, svfoIqFormatStr)) {
                int srvBits = svfoIqFormatsBitCount[_this->iqType];
                _this->client->setSetting(SPYSERVER_SETTING_IQ_FORMAT, svfoIqFormats[_this->iqType]);
                _this->client->setSetting(SPYSERVER_SETTING_IQ_DIGITAL_GAIN, _this->client->computeDigitalGain(srvBits, _this->gain, _this->iqDecimId + _this->client->devInfo.MinimumIQDecimation));
                svfoConfig.acquire();
                svfoConfig.conf["devices"][_this->devRef]["sampleBitDepthId"] = _this->iqType;
                svfoConfig.release(true);
            }

            if (_this->client->devInfo.MaximumGainIndex) {
                SmGui::LeftLabel("Gain");
                SmGui::FillWidth();
                if (SmGui::SliderInt("##spyserver_vfo_source_gain", (int*)&_this->gain, 0, _this->client->devInfo.MaximumGainIndex)) {
                    int srvBits = svfoIqFormatsBitCount[_this->iqType];
                    _this->client->setSetting(SPYSERVER_SETTING_GAIN, _this->gain);
                    _this->client->setSetting(SPYSERVER_SETTING_IQ_DIGITAL_GAIN, _this->client->computeDigitalGain(srvBits, _this->gain, _this->iqDecimId + _this->client->devInfo.MinimumIQDecimation));
                    svfoConfig.acquire();
                    svfoConfig.conf["devices"][_this->devRef]["gainId"] = _this->gain;
                    svfoConfig.release(true);
                }
            }

            // FFT byte->dB mapping isn't officially documented (see the
            // comment in spyserver_vfo_client.cpp) - these let you
            // calibrate it visually against your own server if the
            // waterfall looks too hot/cold or clipped.
            SmGui::LeftLabel("FFT dB Offset");
            SmGui::FillWidth();
            if (SmGui::SliderInt("##spyserver_vfo_source_dboffset", &_this->fftDbOffset, -100, (int)SPYSERVER_MAX_FFT_DB_OFFSET)) {
                _this->applyFFTSettings();
                svfoConfig.acquire();
                svfoConfig.conf["devices"][_this->devRef]["fftDbOffset"] = _this->fftDbOffset;
                svfoConfig.release(true);
            }
            SmGui::LeftLabel("FFT dB Range");
            SmGui::FillWidth();
            if (SmGui::SliderInt("##spyserver_vfo_source_dbrange", &_this->fftDbRange, (int)SPYSERVER_MIN_FFT_DB_RANGE, (int)SPYSERVER_MAX_FFT_DB_RANGE)) {
                _this->applyFFTSettings();
                svfoConfig.acquire();
                svfoConfig.conf["devices"][_this->devRef]["fftDbRange"] = _this->fftDbRange;
                svfoConfig.release(true);
            }

            SmGui::Text("Status:");
            SmGui::SameLine();
            SmGui::TextColoredF(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected (%s)", svfoDeviceTypesStr[_this->client->devInfo.DeviceType]);

            // DIAGNOSTIC: what the poll thread itself last computed/sent.
            // Remove once the non-center tuning bug is tracked down.
            {
                std::string vfoName;
                {
                    std::lock_guard lck(_this->dbgMtx);
                    vfoName = _this->dbgVfoName;
                }
                SmGui::TextColoredF(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "DBG VFO='%s' offset=%.0f", vfoName.c_str(), _this->dbgVfoOffset.load());
                SmGui::TextColoredF(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "DBG center=%.0f targetIQ=%.0f", _this->dbgCenterFreq.load(), _this->dbgTargetIq.load());
                SmGui::TextColoredF(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "DBG lastSentIQ=%.0f lastSentFFT=%.0f", _this->lastSentIqFreq, _this->lastSentFftFreq);
                SmGui::TextColoredF(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "DBG sendCount IQ=%d FFT=%d", _this->dbgIqSendCount.load(), _this->dbgFftSendCount.load());
                SmGui::TextColoredF(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "DBG devInfo: MaxSR=%u MaxBW=%u DecStages=%u MinDec=%u",
                    _this->client->devInfo.MaximumSampleRate, _this->client->devInfo.MaximumBandwidth,
                    _this->client->devInfo.DecimationStageCount, _this->client->devInfo.MinimumIQDecimation);
                SmGui::TextColoredF(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "DBG fftDecimId=%d fftSampleRate=%.0f", _this->fftDecimId, _this->fftSampleRate);
            }
        }
        else {
            SmGui::Text("Status:");
            SmGui::SameLine();
            SmGui::Text("Not connected");
        }
    }

    void tryConnect() {
        try {
            if (client) { client.reset(); }
            client = spyservervfo::connect(hostname, port, &iqStream, fftFrameHandler, this);

            if (!client->waitForDevInfo(3000)) {
                flog::error("SpyServer didn't respond with device information");
                return;
            }

            char buf[1024];
            sprintf(buf, "%s [%08X]", svfoDeviceTypesStr[client->devInfo.DeviceType], client->devInfo.DeviceSerial);
            devRef = std::string(buf);

            svfoConfig.acquire();
            if (!svfoConfig.conf["devices"].contains(devRef)) {
                // Default to a narrow IQ decimation stage (near the end of
                // the list = most decimated = narrowest/lowest rate) since
                // this module's whole point is not pulling a wide IQ
                // stream, and a wide-ish FFT decimation stage for the
                // waterfall context.
                svfoConfig.conf["devices"][devRef]["iqDecimId"] = 0;
                svfoConfig.conf["devices"][devRef]["fftDecimId"] = 0;
                svfoConfig.conf["devices"][devRef]["sampleBitDepthId"] = 1;
                svfoConfig.conf["devices"][devRef]["gainId"] = 0;
                svfoConfig.conf["devices"][devRef]["fftDbOffset"] = -10;
                svfoConfig.conf["devices"][devRef]["fftDbRange"] = 100;
            }
            iqDecimId = svfoConfig.conf["devices"][devRef]["iqDecimId"];
            fftDecimId = svfoConfig.conf["devices"][devRef]["fftDecimId"];
            iqType = svfoConfig.conf["devices"][devRef]["sampleBitDepthId"];
            gain = svfoConfig.conf["devices"][devRef]["gainId"];
            fftDbOffset = svfoConfig.conf["devices"][devRef]["fftDbOffset"];
            fftDbRange = svfoConfig.conf["devices"][devRef]["fftDbRange"];
            svfoConfig.release(true);

            gain = std::clamp<int>(gain, 0, client->devInfo.MaximumGainIndex);

            // Build both decimation-stage lists from the same device info.
            // IQ uses MaximumSampleRate (the raw decimated rate the server
            // actually delivers - this is what IQFrontEnd needs to match
            // for correct demod, and audio has sounded correct throughout
            // testing, so this one's fine as-is).
            // FFT uses MaximumBandwidth instead - a separate DeviceInfo
            // field for the receiver's *usable* bandwidth (typically less
            // than the raw sample rate due to front-end filter rolloff,
            // especially undecimated). This is what the display/click-to-
            // frequency math needs to match, and was the actual bug: at
            // MaximumSampleRate=768kHz with no decimation, clicks were
            // landing ~16% off from the real frequency, consistent with
            // this exact usable-vs-raw-bandwidth distinction.
            iqRates.clear();
            iqRatesTxt.clear();
            fftRates.clear();
            fftRatesTxt.clear();
            for (int i = client->devInfo.MinimumIQDecimation; i <= client->devInfo.DecimationStageCount; i++) {
                double iqSr = (double)client->devInfo.MaximumSampleRate / ((double)(1 << i));
                iqRates.push_back(iqSr);
                iqRatesTxt += getBandwdithScaled(iqSr);
                iqRatesTxt += '\0';

                // MaximumBandwidth is the analog front-end's alias-free
                // limit, which only binds at full (undecimated) rate. Once
                // actually decimated, the digital decimation filter (much
                // sharper than the analog front-end) is the real
                // constraint, and its Nyquist limit is just
                // MaximumSampleRate/2^i - NOT that same analog fraction
                // shrunk again at every stage. Take whichever is smaller.
                double fftSr = std::min((double)client->devInfo.MaximumBandwidth,
                                         (double)client->devInfo.MaximumSampleRate / ((double)(1 << i)));
                fftRates.push_back(fftSr);
                fftRatesTxt += getBandwdithScaled(fftSr);
                fftRatesTxt += '\0';
            }

            iqDecimId = std::clamp<int>(iqDecimId, 0, iqRates.size() - 1);
            fftDecimId = std::clamp<int>(fftDecimId, 0, fftRates.size() - 1);

            iqSampleRate = iqRates[iqDecimId];
            fftSampleRate = fftRates[fftDecimId];
            core::setInputSampleRate(iqSampleRate);
            flog::info("Connected to server (VFO+FFT mode)");
        }
        catch (const std::exception& e) {
            flog::error("Could not connect to spyserver {}", e.what());
        }
    }

    std::string name;
    bool enabled = true;
    bool running = false;

    double iqSampleRate = 1000000;
    double fftSampleRate = 1000000;
    double freq = 100000000;

    char hostname[1024];
    int port = 5555;
    int iqType = 1;

    int iqDecimId = 0;
    std::vector<double> iqRates;
    std::string iqRatesTxt;

    int fftDecimId = 0;
    std::vector<double> fftRates;
    std::string fftRatesTxt;

    int fftDbOffset = -10;
    int fftDbRange = 100;

    // FFT_FREQUENCY: only updated on real device-retune events, set by
    // tune(). IQ_FREQUENCY: continuously tracked from the VFO's live
    // offset by the poll thread. Both rate-limited independently. See
    // start()/stop()/tune() for the full picture.
    std::atomic<double> pendingFftFreq{0.0};
    std::atomic<bool> fftDirty{false};
    double lastSentIqFreq = 0.0;  // poll-thread-only, no lock needed
    double lastSentFftFreq = 0.0; // poll-thread-only, no lock needed
    std::atomic<bool> tuneThreadRunning{false};
    std::thread tuneThread;

    // DIAGNOSTIC: what the poll thread itself computes/sends, shown in the
    // UI. Remove once the non-center tuning bug is tracked down.
    std::mutex dbgMtx;
    std::string dbgVfoName;
    std::atomic<double> dbgVfoOffset{0.0};
    std::atomic<double> dbgCenterFreq{0.0};
    std::atomic<double> dbgTargetIq{0.0};
    std::atomic<int> dbgIqSendCount{0};
    std::atomic<int> dbgFftSendCount{0};

    uint32_t gain = 0;

    std::string devRef = "";

    dsp::stream<dsp::complex_t> iqStream;
    SourceManager::SourceHandler handler;

    spyservervfo::SpyServerVFOClient client;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["hostname"] = "localhost";
    def["port"] = 5555;
    def["devices"] = json::object();
    svfoConfig.setPath(core::args["root"].s() + "/spyserver_vfo_source_config.json");
    svfoConfig.load(def);
    svfoConfig.enableAutoSave();

    svfoConfig.acquire();
    bool corrected = false;
    if (!svfoConfig.conf.contains("hostname") || !svfoConfig.conf.contains("port") || !svfoConfig.conf.contains("devices")) {
        svfoConfig.conf = def;
        corrected = true;
    }
    svfoConfig.release(corrected);
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SpyServerVFOSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (SpyServerVFOSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    svfoConfig.disableAutoSave();
    svfoConfig.save();
}
