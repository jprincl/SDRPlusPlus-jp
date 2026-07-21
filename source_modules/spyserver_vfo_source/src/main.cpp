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

        _this->running = true;
        flog::info("SpyServerVFOSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;
        if (!_this->running) { return; }

        sigpath::iqFrontEnd.setExternalFFTMode(false);

        if (_this->client) { _this->client->stopStream(); }

        _this->running = false;
        flog::info("SpyServerVFOSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        SpyServerVFOSourceModule* _this = (SpyServerVFOSourceModule*)ctx;
        if (_this->running && _this->client) {
            _this->client->setSetting(SPYSERVER_SETTING_IQ_FREQUENCY, freq);
            _this->client->setSetting(SPYSERVER_SETTING_FFT_FREQUENCY, freq);
        }
        _this->freq = freq;
        flog::info("SpyServerVFOSourceModule '{0}': Tune: {1}!", _this->name, freq);
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

            if (_this->running) { style::endDisabled(); }
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

            // Build both decimation-stage lists from the same device info -
            // the server exposes one shared set of decimation stages, used
            // independently for the IQ and FFT settings.
            iqRates.clear();
            iqRatesTxt.clear();
            fftRates.clear();
            fftRatesTxt.clear();
            for (int i = client->devInfo.MinimumIQDecimation; i <= client->devInfo.DecimationStageCount; i++) {
                double sr = (double)client->devInfo.MaximumSampleRate / ((double)(1 << i));
                iqRates.push_back(sr);
                iqRatesTxt += getBandwdithScaled(sr);
                iqRatesTxt += '\0';
                fftRates.push_back(sr);
                fftRatesTxt += getBandwdithScaled(sr);
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
