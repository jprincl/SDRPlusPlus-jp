#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <gui/widgets/folder_select.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <utils/optionlist.h>
#include <utils/flog.h>
#include <dsp/stream.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/demod/quadrature.h>
#include <dsp/demod/fm.h>
#include <dsp/convert/real_to_complex.h>
#include <dsp/channel/frequency_xlator.h>
#include <dsp/filter/fir.h>
#include <dsp/taps/low_pass.h>
#include <dsp/taps/band_pass.h>

#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <type_traits>
#include <mutex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "sstv_modes.h"
#include "sstv_decoder.h"

// stb_image_write: PNG/JPEG writers (vendored, public domain, single-header)
// Include the implementation here (only once across the whole project).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO   // we want only the *_to_func variants? no, keep them
#undef STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "sstv_decoder",
    /* Description:     */ "Analog SSTV Decoder (Martin/Scottie/Robot/PD)",
    /* Author:          */ "SSTV Decoder Contributors",
    /* Version:         */ 1, 0, 1,
    /* Max instances    */ -1
};

ConfigManager config;

// ----------------------------------------------------------------------
// Demodulation modes
// ----------------------------------------------------------------------
enum class DemodMode {
    USB,
    LSB,
    NFM
};

// Bandwidth per mode (in Hz)
constexpr double BW_SSB              = 2800.0;
constexpr double BW_NFM              = 12500.0;

// VFO output sample rate
//   For SSB: ~12 kHz is enough (we listen to ±1.4 kHz)
//   For NFM: ~24 kHz needed (we listen to ±6.25 kHz)
constexpr double VFO_RATE_SSB        = 12000.0;
constexpr double VFO_RATE_NFM        = 24000.0;

// Default audio center (typical SSTV mid-frequency)
//   For SSB: 1400 Hz = BW_SSB/2 (when VFO is tuned with edge on carrier)
//   For NFM: 1900 Hz = mid-point of SSTV pixel range
constexpr double DEFAULT_AUDIO_CENTER_SSB = 1400.0;
constexpr double DEFAULT_AUDIO_CENTER_NFM = 1900.0;

// Low-pass filter for NFM audio frequency extraction
constexpr double NFM_LPF_CUTOFF       = 1200.0;
constexpr double NFM_LPF_TRANSITION   = 400.0;

// ----------------------------------------------------------------------
// Image file format
// ----------------------------------------------------------------------
enum class ImageFormat {
    BMP = 0,
    PNG = 1,
    JPEG = 2,
};

constexpr int DEFAULT_JPEG_QUALITY = 92;

class SSTVDecoderModule : public ModuleManager::Instance {
public:
    SSTVDecoderModule(std::string name)
        : folderSelect("") {
        this->name = name;

        // Build mode option list (Auto + all 11 modes)
        modeList.define("Auto (VIS)",  sstv::Mode::AUTO);
        modeList.define("Martin M1",   sstv::Mode::MARTIN_M1);
        modeList.define("Martin M2",   sstv::Mode::MARTIN_M2);
        modeList.define("Scottie S1",  sstv::Mode::SCOTTIE_S1);
        modeList.define("Scottie S2",  sstv::Mode::SCOTTIE_S2);
        modeList.define("Scottie DX",  sstv::Mode::SCOTTIE_DX);
        modeList.define("Robot 36",    sstv::Mode::ROBOT_36);
        modeList.define("Robot 72",    sstv::Mode::ROBOT_72);
        modeList.define("PD50",        sstv::Mode::PD50);
        modeList.define("PD90",        sstv::Mode::PD90);
        modeList.define("PD120",       sstv::Mode::PD120);
        modeList.define("PD180",       sstv::Mode::PD180);
        modeList.define("PD160",       sstv::Mode::PD160);
        modeList.define("PD240",       sstv::Mode::PD240);
        modeList.define("PD290",       sstv::Mode::PD290);
        modeId = 0;

        // Demod mode option list
        demodList.define("USB", DemodMode::USB);
        demodList.define("LSB", DemodMode::LSB);
        demodList.define("NFM", DemodMode::NFM);
        demodId = 0;  // USB default

        // Image format option list
        formatList.define("BMP",  ImageFormat::BMP);
        formatList.define("PNG",  ImageFormat::PNG);
        formatList.define("JPEG", ImageFormat::JPEG);
        formatId = 0; // BMP default

        // Load config
        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name]["demodId"]         = 0;
            config.conf[name]["audioCenterSSB"]  = DEFAULT_AUDIO_CENTER_SSB;
            config.conf[name]["audioCenterNFM"]  = DEFAULT_AUDIO_CENTER_NFM;
            config.conf[name]["autoSave"]        = true;
            config.conf[name]["savePath"]        = "";
            config.conf[name]["modeId"]          = 0;
            config.conf[name]["formatId"]        = 0;
            config.conf[name]["jpegQuality"]     = DEFAULT_JPEG_QUALITY;
        }
        demodId         = config.conf[name].value("demodId", 0);
        audioCenterSSB  = config.conf[name].value("audioCenterSSB", DEFAULT_AUDIO_CENTER_SSB);
        audioCenterNFM  = config.conf[name].value("audioCenterNFM", DEFAULT_AUDIO_CENTER_NFM);
        autoSave        = config.conf[name].value("autoSave", true);
        savePath        = config.conf[name].value("savePath", std::string(""));
        modeId          = config.conf[name].value("modeId", 0);
        formatId        = config.conf[name].value("formatId", 0);
        jpegQuality     = config.conf[name].value("jpegQuality", DEFAULT_JPEG_QUALITY);
        bool weakInit   = config.conf[name].value("weakSignal", false);
        bool ransacInit = config.conf[name].value("ransac", false);
        bool medianInit = config.conf[name].value("median", false);
        config.release(true);

        decoder.setWeakSignalMode(weakInit);
        decoder.setRansacEnabled(ransacInit);
        decoder.setMedianFilterEnabled(medianInit);

        // Active audio_center depends on demod mode
        audioCenter = (demodList.value(demodId) == DemodMode::NFM)
                        ? audioCenterNFM : audioCenterSSB;

        // Default save path: ~/sstv_images
        if (savePath.empty()) {
            const char* home =
            #ifdef _WIN32
                std::getenv("USERPROFILE");
            #else
                std::getenv("HOME");
            #endif
            if (home) savePath = std::string(home) + "/sstv_images";
            else      savePath = "./sstv_images";
        }
        folderSelect.setPath(savePath);

        // Configure decoder
        decoder.init(0.0);  // sample rate set when chain is built
        applyForcedMode();
        decoder.setLineCallback([this](int line){
            std::lock_guard<std::mutex> lck(textureMutex);
            textureDirty = true;
            // Re-arm auto-save when a new image starts.
            //   line <= linesPerCycle means we just committed the very first
            //   cycle's worth of lines for a fresh reception. Works for both
            //   1-line modes (Martin, Scottie: line==1) and 2-line modes
            //   (PD: line==2).
            int linesPerCycle = 1;
            const auto* params = sstv::getModeParams(decoder.getCurrentMode());
            if (params) linesPerCycle = (int)params->linesPerCycle;
            if (line > 0 && line <= linesPerCycle) {
                imageSaved = false;
            }
            if (decoder.getState() == sstv::SSTVDecoder::State::DONE && !imageSaved) {
                imageSaved = true;
                if (autoSave) saveImageRequested = true;
            }
        });

        // Build DSP chain for the current demod mode
        buildChain(demodList.value(demodId));

        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~SSTVDecoderModule() {
        gui::menu.removeEntry(name);
        if (enabled) {
            stopChain();
            if (vfo) {
                sigpath::vfoManager.deleteVFO(vfo);
                vfo = nullptr;
            }
        }
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        dsp::taps::free(lpfTaps);
        dsp::taps::free(audioLpfTaps);
    }

    void postInit() {}

    void enable() {
        if (enabled) return;
        buildChain(demodList.value(demodId));
        enabled = true;
    }

    void disable() {
        if (!enabled) return;
        stopChain();
        if (vfo) {
            sigpath::vfoManager.deleteVFO(vfo);
            vfo = nullptr;
        }
        enabled = false;
    }

    bool isEnabled() { return enabled; }

private:
    // ------------------------------------------------------------------
    // DSP chain builder - reconfigures per demod mode
    // ------------------------------------------------------------------
    //   USB:  VFO(REF_LOWER, BW 2800, rate 12k)
    //           User clicks LEFT edge of marker on suppressed carrier.
    //           VFO center is at carrier + bw/2.
    //           A signal at audio freq f appears in baseband at f - bw/2.
    //           Quadrature -> inst_freq = f - bw/2
    //           handler: audio = inst_freq + audio_center   (default 1400 = bw/2)
    //
    //   LSB:  VFO(REF_UPPER, BW 2800, rate 12k)
    //           User clicks RIGHT edge of marker on suppressed carrier.
    //           VFO center is at carrier - bw/2.
    //           A signal at audio freq f appears in baseband at bw/2 - f.
    //           Quadrature -> inst_freq = bw/2 - f
    //           handler: audio = audio_center - inst_freq   (default 1400 = bw/2)
    //
    //   NFM:  VFO(REF_CENTER, BW 12500, rate 24k)
    //           User clicks CENTER of marker on FM carrier.
    //           -> FM demod (-> real audio)
    //           -> RealToComplex (zero-padded imag)
    //           -> FrequencyXlator (shift by -audio_center)
    //           -> FIR LPF (kills the conjugate term + passes sync/VIS/pixels)
    //           -> Quadrature (-> Hz relative to audio_center)
    //           handler: audio = inst_freq + audio_center   (default 1900)
    // ------------------------------------------------------------------
    void buildChain(DemodMode mode) {
        stopChain();

        // Delete previous VFO
        if (vfo) {
            sigpath::vfoManager.deleteVFO(vfo);
            vfo = nullptr;
        }

        currentDemod = mode;

        // Pick VFO reference + bandwidth per mode
        int    vfoRef;
        double bw;
        double rate;
        switch (mode) {
            case DemodMode::USB:
                vfoRef = ImGui::WaterfallVFO::REF_LOWER;
                bw = BW_SSB;
                rate = VFO_RATE_SSB;
                break;
            case DemodMode::LSB:
                vfoRef = ImGui::WaterfallVFO::REF_UPPER;
                bw = BW_SSB;
                rate = VFO_RATE_SSB;
                break;
            case DemodMode::NFM:
            default:
                vfoRef = ImGui::WaterfallVFO::REF_CENTER;
                bw = BW_NFM;
                rate = VFO_RATE_NFM;
                break;
        }
        chainSampleRate = rate;

        // Sync the active audio_center from the per-mode-saved value
        audioCenter = (mode == DemodMode::NFM) ? audioCenterNFM : audioCenterSSB;

        // (Re)create VFO
        double initialOffset = 0;
        if (gui::waterfall.getBandwidth() > 0) {
            initialOffset = std::clamp<double>(0,
                              -gui::waterfall.getBandwidth() / 2.0,
                               gui::waterfall.getBandwidth() / 2.0);
        }
        vfo = sigpath::vfoManager.createVFO(name,
                                            vfoRef,
                                            initialOffset,
                                            bw, rate, bw, bw,
                                            true);
        vfo->setSnapInterval(100);

        // The decoder operates at the rate of the final freq stream
        decoder.init(rate);

        // Build mode-specific DSP chain
        if (mode == DemodMode::USB || mode == DemodMode::LSB) {
            // SSB: Quadrature directly on VFO IQ
            //   Output is in Hz (instantaneous freq) when deviation=1.0
            freqExtractor = std::make_unique<dsp::demod::Quadrature>();
            freqExtractor->init(vfo->output, 1.0, rate);

            sink = std::make_unique<dsp::sink::Handler<float>>();
            sink->init(&freqExtractor->out,
                       (mode == DemodMode::USB) ? dspHandlerUSB : dspHandlerLSB,
                       this);

            freqExtractor->start();
            sink->start();
        }
        else { // NFM
            // FM demod: complex IQ -> real audio
            fmDemod = std::make_unique<dsp::demod::FM<float>>();

            // Cross-version init: older SDR++ has FM::init(in, rate, bw, lowPass)
            // (4 args), newer versions added a 5th argument `highPass` for an
            // optional DC-blocking high-pass filter. We detect the available
            // signature at compile-time via a generic lambda + if-constexpr on
            // is_invocable, so the same source compiles against both APIs.
            auto initFM = [](auto* d, auto* in, double rate, double bw) {
                if constexpr (std::is_invocable_v<
                                decltype(&dsp::demod::FM<float>::init),
                                dsp::demod::FM<float>*,
                                dsp::stream<dsp::complex_t>*,
                                double, double, bool, bool>) {
                    // 5-arg signature: (in, rate, bw, lowPass, highPass)
                    d->init(in, rate, bw, true, false);
                } else {
                    // 4-arg signature: (in, rate, bw, lowPass)
                    d->init(in, rate, bw, true);
                }
            };
            initFM(fmDemod.get(), vfo->output, rate, bw);

            // Real-domain BANDPASS after FM demod to keep only SSTV audio
            // SSTV signals span 1100-2300 Hz; we bandpass 1000-2400 Hz with
            // 500 Hz transition. This kills both sub-audio rumble and the
            // out-of-band FM noise that was saturating the decoder.
            dsp::taps::free(audioLpfTaps);
            audioLpfTaps = dsp::taps::bandPass<float>(1000.0, 2400.0, 500.0, rate);
            audioLpf = std::make_unique<dsp::filter::FIR<float, float>>();
            audioLpf->init(&fmDemod->out, audioLpfTaps);

            // Real -> Complex (imag=0)
            realToComplex = std::make_unique<dsp::convert::RealToComplex>();
            realToComplex->init(&audioLpf->out);

            // FrequencyXlator: mix down by audio_center
            xlator = std::make_unique<dsp::channel::FrequencyXlator>();
            xlator->init(&realToComplex->out, -audioCenter, rate);

            // Complex LPF kills the conjugate term + further out-of-band rejection
            dsp::taps::free(lpfTaps);
            lpfTaps = dsp::taps::lowPass(NFM_LPF_CUTOFF, NFM_LPF_TRANSITION, rate);
            lpf = std::make_unique<dsp::filter::FIR<dsp::complex_t, float>>();
            lpf->init(&xlator->out, lpfTaps);

            // Quadrature demod on filtered complex -> freq in Hz
            freqExtractor = std::make_unique<dsp::demod::Quadrature>();
            freqExtractor->init(&lpf->out, 1.0, rate);

            sink = std::make_unique<dsp::sink::Handler<float>>();
            sink->init(&freqExtractor->out, dspHandlerNFM, this);

            fmDemod->start();
            audioLpf->start();
            realToComplex->start();
            xlator->start();
            lpf->start();
            freqExtractor->start();
            sink->start();
        }

        flog::info("[SSTV] DSP chain built for mode {0} (ref={1}, rate={2} Hz, bw={3} Hz, audio_ctr={4} Hz)",
                   demodList.name(demodId), vfoRef, (int)rate, (int)bw, (int)audioCenter);
    }

    void stopChain() {
        if (sink)          sink->stop();
        if (freqExtractor) freqExtractor->stop();
        if (lpf)           lpf->stop();
        if (xlator)        xlator->stop();
        if (realToComplex) realToComplex->stop();
        if (audioLpf)      audioLpf->stop();
        if (fmDemod)       fmDemod->stop();

        sink.reset();
        freqExtractor.reset();
        lpf.reset();
        xlator.reset();
        realToComplex.reset();
        audioLpf.reset();
        fmDemod.reset();
    }

    // ------------------------------------------------------------------
    // DSP handlers (one per branch, to inline the audio conversion)
    // ------------------------------------------------------------------
    //   USB (REF_LOWER): inst_freq = audio_freq - bw/2
    //   => audio = inst_freq + audio_center  (audio_center nominally bw/2)
    static void dspHandlerUSB(float* data, int count, void* ctx) {
        SSTVDecoderModule* _this = (SSTVDecoderModule*)ctx;
        static thread_local std::vector<float> audioFreq;
        if ((int)audioFreq.size() < count) audioFreq.resize(count);
        float center = (float)_this->audioCenter;
        for (int i = 0; i < count; i++) {
            audioFreq[i] = data[i] + center;
        }
        _this->decoder.process(audioFreq.data(), count);
    }

    //   LSB (REF_UPPER): inst_freq = bw/2 - audio_freq  (spectrum mirror)
    //   => audio = audio_center - inst_freq  (audio_center nominally bw/2)
    static void dspHandlerLSB(float* data, int count, void* ctx) {
        SSTVDecoderModule* _this = (SSTVDecoderModule*)ctx;
        static thread_local std::vector<float> audioFreq;
        if ((int)audioFreq.size() < count) audioFreq.resize(count);
        float center = (float)_this->audioCenter;
        for (int i = 0; i < count; i++) {
            audioFreq[i] = center - data[i];
        }
        _this->decoder.process(audioFreq.data(), count);
    }

    //   NFM (REF_CENTER + analytic chain): inst_freq = audio_freq - audio_center
    //   => audio = inst_freq + audio_center
    static void dspHandlerNFM(float* data, int count, void* ctx) {
        SSTVDecoderModule* _this = (SSTVDecoderModule*)ctx;
        static thread_local std::vector<float> audioFreq;
        if ((int)audioFreq.size() < count) audioFreq.resize(count);
        float center = (float)_this->audioCenter;
        for (int i = 0; i < count; i++) {
            audioFreq[i] = data[i] + center;
        }
        _this->decoder.process(audioFreq.data(), count);
    }

    // ------------------------------------------------------------------
    // Forced mode helper
    // ------------------------------------------------------------------
    void applyForcedMode() {
        sstv::Mode m = modeList.value(modeId);
        decoder.setForcedMode(m);
    }

    // ------------------------------------------------------------------
    // Texture upload (UI thread)
    // ------------------------------------------------------------------
    void uploadTexture() {
        int w = decoder.getImageWidth();
        int h = decoder.getImageHeight();
        if (w <= 0 || h <= 0) return;

        std::lock_guard<std::mutex> tlck(textureMutex);
        if (texture == 0 || texWidth != w || texHeight != h) {
            if (texture != 0) glDeleteTextures(1, &texture);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
            texWidth = w;
            texHeight = h;
        }
        {
            std::lock_guard<std::mutex> ilck(decoder.getImageMutex());
            const uint8_t* data = decoder.getImageRGB();
            if (data) {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, data);
            }
        }
        textureDirty = false;
    }

    // ------------------------------------------------------------------
    // Image writers using stb_image_write
    // Note: stb_image_write expects RGB data already in correct order.
    // Our imageBuffer is stored as RGB (per row, [R, G, B, R, G, B, ...]).
    // ------------------------------------------------------------------
    static bool writeBMP(const std::string& path, int w, int h, const uint8_t* rgbData) {
        // stbi_write_bmp returns 0 on failure, non-zero on success
        return stbi_write_bmp(path.c_str(), w, h, 3, rgbData) != 0;
    }
    static bool writePNG(const std::string& path, int w, int h, const uint8_t* rgbData) {
        // stride_in_bytes = w * 3 for our packed RGB layout
        return stbi_write_png(path.c_str(), w, h, 3, rgbData, w * 3) != 0;
    }
    static bool writeJPEG(const std::string& path, int w, int h, const uint8_t* rgbData, int quality) {
        if (quality < 1) quality = 1;
        if (quality > 100) quality = 100;
        return stbi_write_jpg(path.c_str(), w, h, 3, rgbData, quality) != 0;
    }

    bool writeImage(const std::string& path, int w, int h, const uint8_t* rgbData) {
        ImageFormat fmt = formatList.value(formatId);
        switch (fmt) {
            case ImageFormat::BMP:  return writeBMP(path, w, h, rgbData);
            case ImageFormat::PNG:  return writePNG(path, w, h, rgbData);
            case ImageFormat::JPEG: return writeJPEG(path, w, h, rgbData, jpegQuality);
        }
        return false;
    }

    const char* currentFormatExtension() const {
        ImageFormat fmt = formatList.value(formatId);
        switch (fmt) {
            case ImageFormat::BMP:  return "bmp";
            case ImageFormat::PNG:  return "png";
            case ImageFormat::JPEG: return "jpg";
        }
        return "bmp";
    }

    std::string buildSaveFilename() {
        auto t = std::time(nullptr);
        std::tm tm_;
    #ifdef _WIN32
        localtime_s(&tm_, &t);
    #else
        localtime_r(&t, &tm_);
    #endif
        std::stringstream ss;
        ss << "sstv_"
           << std::put_time(&tm_, "%Y%m%d_%H%M%S")
           << "_" << sstv::getModeName(decoder.getCurrentMode())
           << "." << currentFormatExtension();
        std::string fname = ss.str();
        for (char& c : fname) if (c == ' ') c = '_';
        return fname;
    }

    void doSaveImage() {
        try {
            std::filesystem::create_directories(savePath);
        } catch (...) {
            flog::error("[SSTV] Failed to create save dir: {0}", savePath);
            return;
        }
        std::string fname = savePath + "/" + buildSaveFilename();
        int w = decoder.getImageWidth();
        int h = decoder.getImageHeight();
        if (w <= 0 || h <= 0) return;
        std::vector<uint8_t> copy;
        {
            std::lock_guard<std::mutex> lck(decoder.getImageMutex());
            const uint8_t* p = decoder.getImageRGB();
            copy.assign(p, p + w * h * 3);
        }
        if (writeImage(fname, w, h, copy.data())) {
            flog::info("[SSTV] Saved: {0}", fname);
            lastSavedFile = fname;
        } else {
            flog::error("[SSTV] Save failed: {0}", fname);
        }
    }

    // ------------------------------------------------------------------
    // Menu (ImGui)
    // ------------------------------------------------------------------
    static void menuHandler(void* ctx) {
        SSTVDecoderModule* _this = (SSTVDecoderModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (!_this->enabled) { style::beginDisabled(); }

        if (_this->saveImageRequested.exchange(false)) {
            _this->doSaveImage();
        }

        // ---- Demod mode (LSB/USB/NFM) ----
        ImGui::LeftLabel("Demod");
        ImGui::FillWidth();
        if (ImGui::Combo(("##sstv_demod_" + _this->name).c_str(),
                         &_this->demodId, _this->demodList.txt)) {
            _this->buildChain(_this->demodList.value(_this->demodId));
            config.acquire();
            config.conf[_this->name]["demodId"] = _this->demodId;
            config.release(true);
        }

        // ---- SSTV mode (Auto / Martin M1 / ...) ----
        ImGui::LeftLabel("Mode");
        ImGui::FillWidth();
        if (ImGui::Combo(("##sstv_mode_" + _this->name).c_str(),
                         &_this->modeId, _this->modeList.txt)) {
            _this->applyForcedMode();
            config.acquire();
            config.conf[_this->name]["modeId"] = _this->modeId;
            config.release(true);
        }

        // ---- Audio center ----
        DemodMode demod = _this->demodList.value(_this->demodId);
        bool isNFM = (demod == DemodMode::NFM);
        const char* acLabel = isNFM ? "Audio ctr (Hz)" : "Carrier ofs (Hz)";
        ImGui::LeftLabel(acLabel);
        ImGui::FillWidth();
        float ac = (float)_this->audioCenter;
        float acMin = isNFM ? 1500.0f : 1100.0f;
        float acMax = isNFM ? 2300.0f : 1700.0f;
        if (ImGui::SliderFloat(("##sstv_ac_" + _this->name).c_str(),
                                &ac, acMin, acMax, "%.0f")) {
            _this->audioCenter = ac;
            // Save per-mode
            if (isNFM) _this->audioCenterNFM = ac;
            else       _this->audioCenterSSB = ac;
            // If we're in NFM, the xlator needs the new offset
            if (isNFM && _this->xlator) {
                _this->xlator->setOffset(-_this->audioCenter, _this->chainSampleRate);
            }
            config.acquire();
            if (isNFM) config.conf[_this->name]["audioCenterNFM"] = _this->audioCenterNFM;
            else       config.conf[_this->name]["audioCenterSSB"] = _this->audioCenterSSB;
            config.release(true);
        }
        if (isNFM) {
            ImGui::TextDisabled("FM carrier in middle of VFO; tune for clean image");
        } else if (demod == DemodMode::USB) {
            ImGui::TextDisabled("Click LEFT edge of marker on suppressed carrier");
        } else {
            ImGui::TextDisabled("Click RIGHT edge of marker on suppressed carrier");
        }

        // ---- Status ----
        ImGui::Separator();
        ImGui::Text("State : %s", _this->decoder.getStateName());
        ImGui::Text("Mode  : %s", sstv::getModeName(_this->decoder.getCurrentMode()));
        ImGui::Text("Freq  : %.0f Hz", _this->decoder.getLastFreq());
        ImGui::Text("Lines : %d / %d",
                    _this->decoder.getLinesReceived(),
                    _this->decoder.getImageHeight());
        ImGui::ProgressBar(_this->decoder.getProgress(), ImVec2(menuWidth, 0));

        // ---- Reception quality indicator ----
        {
            float rms = _this->decoder.getSyncRmsResidual();
            float ratio = _this->decoder.getSyncDetectionRatio();
            if (rms >= 0.0f) {
                // Map RMS residual to a quality label & color.
                //   < 5 samples  : excellent
                //   5-20         : good
                //   20-50        : fair
                //   > 50         : poor
                const char* qual;
                ImVec4 col;
                if (rms < 5.0f)       { qual = "Excellent"; col = ImVec4(0.20f, 0.85f, 0.20f, 1.0f); }
                else if (rms < 20.0f) { qual = "Good";      col = ImVec4(0.55f, 0.85f, 0.20f, 1.0f); }
                else if (rms < 50.0f) { qual = "Fair";      col = ImVec4(0.90f, 0.75f, 0.10f, 1.0f); }
                else                  { qual = "Poor";      col = ImVec4(0.90f, 0.30f, 0.20f, 1.0f); }
                ImGui::Text("Quality:");
                ImGui::SameLine();
                ImGui::TextColored(col, "%s", qual);
                ImGui::SameLine();
                ImGui::TextDisabled("(jitter %.1f, syncs %.0f%%)",
                                    rms, (ratio >= 0.0f ? ratio * 100.0f : 0.0f));
            }
        }

        // ---- Image display ----
        ImGui::Separator();
        bool textureDirty;
        {
            std::lock_guard<std::mutex> lck(_this->textureMutex);
            textureDirty = _this->textureDirty;
        }
        if (textureDirty) _this->uploadTexture();
        if (_this->texture != 0 && _this->texWidth > 0 && _this->texHeight > 0) {
            float aspect = (float)_this->texHeight / (float)_this->texWidth;
            float w = menuWidth;
            float h = w * aspect;
            ImGui::Image((ImTextureID)(uintptr_t)_this->texture, ImVec2(w, h));
        } else {
            ImGui::TextDisabled("(no image yet)");
        }

        // ---- Controls ----
        ImGui::Separator();
        if (ImGui::Button(("Force start##sstv_start_" + _this->name).c_str(),
                          ImVec2(menuWidth, 0))) {
            _this->imageSaved = false;
            _this->decoder.forceStartImage();
        }
        if (ImGui::Button(("Reset##sstv_reset_" + _this->name).c_str(),
                          ImVec2(menuWidth, 0))) {
            _this->imageSaved = false;
            _this->decoder.reset();
        }
        if (ImGui::Button(("Save image##sstv_save_" + _this->name).c_str(),
                          ImVec2(menuWidth, 0))) {
            _this->doSaveImage();
        }

        if (ImGui::Checkbox(("Auto-save on complete##sstv_autosave_" + _this->name).c_str(),
                            &_this->autoSave)) {
            config.acquire();
            config.conf[_this->name]["autoSave"] = _this->autoSave;
            config.release(true);
        }

        // ---- Image format selector ----
        ImGui::LeftLabel("Format");
        ImGui::FillWidth();
        if (ImGui::Combo(("##sstv_fmt_" + _this->name).c_str(),
                         &_this->formatId, _this->formatList.txt)) {
            config.acquire();
            config.conf[_this->name]["formatId"] = _this->formatId;
            config.release(true);
        }

        // ---- JPEG quality (only when JPEG selected) ----
        if (_this->formatList.value(_this->formatId) == ImageFormat::JPEG) {
            ImGui::LeftLabel("Quality");
            ImGui::FillWidth();
            int q = _this->jpegQuality;
            if (ImGui::SliderInt(("##sstv_jpegq_" + _this->name).c_str(),
                                  &q, 1, 100)) {
                _this->jpegQuality = q;
                config.acquire();
                config.conf[_this->name]["jpegQuality"] = _this->jpegQuality;
                config.release(true);
            }
        }

        ImGui::LeftLabel("Folder");
        ImGui::FillWidth();
        if (_this->folderSelect.render("##sstv_folder_" + _this->name)) {
            if (_this->folderSelect.pathIsValid()) {
                _this->savePath = _this->folderSelect.path;
                config.acquire();
                config.conf[_this->name]["savePath"] = _this->savePath;
                config.release(true);
            }
        }

        // ---- Robustness / quality options ----
        ImGui::Separator();

        bool weak = _this->decoder.getWeakSignalMode();
        if (ImGui::Checkbox(("Weak signal mode (ISS / low SNR)##sstv_weak_" + _this->name).c_str(),
                            &weak)) {
            _this->decoder.setWeakSignalMode(weak);
            config.acquire();
            config.conf[_this->name]["weakSignal"] = weak;
            config.release(true);
        }

        bool ransac = _this->decoder.getRansacEnabled();
        if (ImGui::Checkbox(("RANSAC slant correction##sstv_ransac_" + _this->name).c_str(),
                            &ransac)) {
            _this->decoder.setRansacEnabled(ransac);
            config.acquire();
            config.conf[_this->name]["ransac"] = ransac;
            config.release(true);
        }

        bool median = _this->decoder.getMedianFilterEnabled();
        if (ImGui::Checkbox(("Median filter (3x3, denoise)##sstv_median_" + _this->name).c_str(),
                            &median)) {
            _this->decoder.setMedianFilterEnabled(median);
            config.acquire();
            config.conf[_this->name]["median"] = median;
            config.release(true);
        }

        if (!_this->lastSavedFile.empty()) {
            ImGui::TextDisabled("Saved: %s",
                                std::filesystem::path(_this->lastSavedFile).filename().string().c_str());
        }

        if (!_this->enabled) { style::endDisabled(); }
    }

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------
    std::string                 name;
    bool                        enabled = true;

    // VFO (recreated on demod mode change)
    VFOManager::VFO*            vfo = nullptr;

    // Active demod mode + sample rate at which decoder receives data
    DemodMode                   currentDemod = DemodMode::USB;
    double                      chainSampleRate = VFO_RATE_SSB;

    // DSP blocks (instantiated per chain)
    std::unique_ptr<dsp::demod::FM<float>>                       fmDemod;
    std::unique_ptr<dsp::filter::FIR<float, float>>              audioLpf;
    std::unique_ptr<dsp::convert::RealToComplex>                 realToComplex;
    std::unique_ptr<dsp::channel::FrequencyXlator>               xlator;
    std::unique_ptr<dsp::filter::FIR<dsp::complex_t, float>>     lpf;
    std::unique_ptr<dsp::demod::Quadrature>                      freqExtractor;
    std::unique_ptr<dsp::sink::Handler<float>>                   sink;
    dsp::tap<float>                                              lpfTaps{};
    dsp::tap<float>                                              audioLpfTaps{};

    // SSTV decoder
    sstv::SSTVDecoder           decoder;

    // UI state
    OptionList<std::string, sstv::Mode>     modeList;
    OptionList<std::string, DemodMode>      demodList;
    OptionList<std::string, ImageFormat>    formatList;
    int                         modeId = 0;
    int                         demodId = 0;
    int                         formatId = 0;
    int                         jpegQuality = DEFAULT_JPEG_QUALITY;
    double                      audioCenter = DEFAULT_AUDIO_CENTER_NFM;
    double                      audioCenterSSB = DEFAULT_AUDIO_CENTER_SSB;
    double                      audioCenterNFM = DEFAULT_AUDIO_CENTER_NFM;

    // Texture
    GLuint                      texture = 0;
    int                         texWidth = 0;
    int                         texHeight = 0;
    std::mutex                  textureMutex;
    bool                        textureDirty = false;

    // Save state
    bool                        autoSave = true;
    bool                        imageSaved = false;
    std::atomic<bool>           saveImageRequested{false};
    std::string                 savePath;
    std::string                 lastSavedFile;
    FolderSelect                folderSelect;
};

// ----------------------------------------------------------------------
// Module entry points
// ----------------------------------------------------------------------
MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/sstv_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SSTVDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (SSTVDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
