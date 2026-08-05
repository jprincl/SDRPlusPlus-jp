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
#include <mutex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "wefax_decoder.h"

// stb_image_write: BMP/PNG/JPEG writers (vendored, public domain, single-header)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "wefax_decoder",
    /* Description:     */ "WEFAX / HF Radiofax Decoder (auto-slant)",
    /* Author:          */ "WEFAX Decoder Contributors",
    /* Version:         */ 0, 1, 11,
    /* Max instances    */ -1
};

ConfigManager config;

// ----------------------------------------------------------------------
// Demodulation modes (identical to the SSTV module)
// ----------------------------------------------------------------------
enum class DemodMode { USB, LSB, NFM };

constexpr double BW_SSB        = 2800.0;
constexpr double BW_NFM        = 12500.0;
constexpr double VFO_RATE_SSB  = 12000.0;
constexpr double VFO_RATE_NFM  = 24000.0;

constexpr double DEFAULT_AUDIO_CENTER_SSB = 1400.0;
constexpr double DEFAULT_AUDIO_CENTER_NFM = 1900.0;

constexpr double NFM_LPF_CUTOFF     = 1200.0;
constexpr double NFM_LPF_TRANSITION = 400.0;

enum class ImageFormat { BMP = 0, PNG = 1, JPEG = 2 };
constexpr int DEFAULT_JPEG_QUALITY = 92;

class WEFAXDecoderModule : public ModuleManager::Instance {
public:
    WEFAXDecoderModule(std::string name)
        : folderSelect("") {
        this->name = name;

        // LPM list (lines per minute)
        lpmList.define("60 LPM",  60.0);
        lpmList.define("90 LPM",  90.0);
        lpmList.define("100 LPM", 100.0);
        lpmList.define("120 LPM", 120.0);
        lpmList.define("180 LPM", 180.0);
        lpmList.define("240 LPM", 240.0);
        lpmId = 3; // 120 LPM default

        // IOC list (Index Of Cooperation)
        iocList.define("IOC 576", 576);
        iocList.define("IOC 288", 288);
        iocId = 0; // 576 default

        // Demod modes
        demodList.define("USB", DemodMode::USB);
        demodList.define("LSB", DemodMode::LSB);
        demodList.define("NFM", DemodMode::NFM);
        demodId = 0;

        // Image formats
        formatList.define("BMP",  ImageFormat::BMP);
        formatList.define("PNG",  ImageFormat::PNG);
        formatList.define("JPEG", ImageFormat::JPEG);
        formatId = 0;

        // Load config
        config.acquire();
        if (!config.conf.contains(name)) {
            config.conf[name]["demodId"]        = 0;
            config.conf[name]["audioCenterSSB"] = DEFAULT_AUDIO_CENTER_SSB;
            config.conf[name]["audioCenterNFM"] = DEFAULT_AUDIO_CENTER_NFM;
            config.conf[name]["autoSave"]       = true;
            config.conf[name]["savePath"]       = "";
            config.conf[name]["lpmId"]          = 3;
            config.conf[name]["iocId"]          = 0;
            config.conf[name]["formatId"]       = 0;
            config.conf[name]["jpegQuality"]    = DEFAULT_JPEG_QUALITY;
        }
        demodId        = config.conf[name].value("demodId", 0);
        audioCenterSSB = config.conf[name].value("audioCenterSSB", DEFAULT_AUDIO_CENTER_SSB);
        audioCenterNFM = config.conf[name].value("audioCenterNFM", DEFAULT_AUDIO_CENTER_NFM);
        autoSave       = config.conf[name].value("autoSave", true);
        savePath       = config.conf[name].value("savePath", std::string(""));
        lpmId          = config.conf[name].value("lpmId", 3);
        iocId          = config.conf[name].value("iocId", 0);
        formatId       = config.conf[name].value("formatId", 0);
        jpegQuality    = config.conf[name].value("jpegQuality", DEFAULT_JPEG_QUALITY);
        bool autoStartI  = config.conf[name].value("autoStart", false);
        bool autoStopAptI = config.conf[name].value("autoStopApt", true);
        bool autoSlantI  = config.conf[name].value("autoSlant", true);
        bool ransacI     = config.conf[name].value("ransac", true);
        bool medianI     = config.conf[name].value("median", false);
        manualSlantPpm   = config.conf[name].value("manualSlantPpm", 0.0);
        hShiftPixels     = config.conf[name].value("hShift", 0);
        bool slantLearnedI = config.conf[name].value("slantLearned", false);
        double learnedPpmI = config.conf[name].value("learnedSlantPpm", 0.0);
        config.release(true);

        decoder.setAutoStart(autoStartI);
        decoder.setAutoStopApt(autoStopAptI);
        decoder.setAutoSlant(autoSlantI);
        decoder.setRansacEnabled(ransacI);
        decoder.setMedianFilterEnabled(medianI);
        decoder.setManualSlantPpm(manualSlantPpm);
        decoder.setHShiftPixels(hShiftPixels);
        if (slantLearnedI) { decoder.setLearnedSlantPpm(learnedPpmI); lastSavedLearnedPpm = learnedPpmI; }

        audioCenter = (demodList.value(demodId) == DemodMode::NFM)
                        ? audioCenterNFM : audioCenterSSB;

        if (savePath.empty()) {
            const char* home =
            #ifdef _WIN32
                std::getenv("USERPROFILE");
            #else
                std::getenv("HOME");
            #endif
            if (home) savePath = std::string(home) + "/wefax_images";
            else      savePath = "./wefax_images";
        }
        folderSelect.setPath(savePath);

        decoder.setLPM(lpmList.value(lpmId));
        decoder.setIOC(iocList.value(iocId));
        decoder.init(0.0);

        decoder.setLineCallback([this](int line){
            std::lock_guard<std::mutex> lck(textureMutex);
            textureDirty = true;
            if (decoder.getState() == wefax::WEFAXDecoder::State::DONE && !imageSaved) {
                imageSaved = true;
                if (autoSave && decoder.getImageHeight() > 1) saveImageRequested = true;
            }
        });

        buildChain(demodList.value(demodId));
        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~WEFAXDecoderModule() {
        gui::menu.removeEntry(name);
        if (enabled) {
            stopChain();
            if (vfo) { sigpath::vfoManager.deleteVFO(vfo); vfo = nullptr; }
        }
        if (texture != 0) { glDeleteTextures(1, &texture); texture = 0; }
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
        if (vfo) { sigpath::vfoManager.deleteVFO(vfo); vfo = nullptr; }
        enabled = false;
    }

    bool isEnabled() { return enabled; }

private:
    // ------------------------------------------------------------------
    // DSP chain (identical to the SSTV module): produces an instantaneous
    // frequency stream (Hz) for USB / LSB / NFM tuning.
    // ------------------------------------------------------------------
    void buildChain(DemodMode mode) {
        stopChain();
        if (vfo) { sigpath::vfoManager.deleteVFO(vfo); vfo = nullptr; }

        currentDemod = mode;
        int vfoRef; double bw; double rate;
        switch (mode) {
            case DemodMode::USB:
                vfoRef = ImGui::WaterfallVFO::REF_LOWER; bw = BW_SSB; rate = VFO_RATE_SSB; break;
            case DemodMode::LSB:
                vfoRef = ImGui::WaterfallVFO::REF_UPPER; bw = BW_SSB; rate = VFO_RATE_SSB; break;
            case DemodMode::NFM:
            default:
                vfoRef = ImGui::WaterfallVFO::REF_CENTER; bw = BW_NFM; rate = VFO_RATE_NFM; break;
        }
        chainSampleRate = rate;
        audioCenter = (mode == DemodMode::NFM) ? audioCenterNFM : audioCenterSSB;

        double initialOffset = 0;
        if (gui::waterfall.getBandwidth() > 0) {
            initialOffset = std::clamp<double>(0,
                              -gui::waterfall.getBandwidth() / 2.0,
                               gui::waterfall.getBandwidth() / 2.0);
        }
        vfo = sigpath::vfoManager.createVFO(name, vfoRef, initialOffset, bw, rate, bw, bw, true);
        vfo->setSnapInterval(100);

        decoder.init(rate);

        if (mode == DemodMode::USB || mode == DemodMode::LSB) {
            freqExtractor = std::make_unique<dsp::demod::Quadrature>();
            freqExtractor->init(vfo->output, 1.0, rate);
            sink = std::make_unique<dsp::sink::Handler<float>>();
            sink->init(&freqExtractor->out,
                       (mode == DemodMode::USB) ? dspHandlerUSB : dspHandlerLSB, this);
            freqExtractor->start();
            sink->start();
        }
        else { // NFM
            fmDemod = std::make_unique<dsp::demod::FM<float>>();
            fmDemod->init(vfo->output, rate, bw, true);

            dsp::taps::free(audioLpfTaps);
            audioLpfTaps = dsp::taps::bandPass<float>(1000.0, 2400.0, 500.0, rate);
            audioLpf = std::make_unique<dsp::filter::FIR<float, float>>();
            audioLpf->init(&fmDemod->out, audioLpfTaps);

            realToComplex = std::make_unique<dsp::convert::RealToComplex>();
            realToComplex->init(&audioLpf->out);

            xlator = std::make_unique<dsp::channel::FrequencyXlator>();
            xlator->init(&realToComplex->out, -audioCenter, rate);

            dsp::taps::free(lpfTaps);
            lpfTaps = dsp::taps::lowPass(NFM_LPF_CUTOFF, NFM_LPF_TRANSITION, rate);
            lpf = std::make_unique<dsp::filter::FIR<dsp::complex_t, float>>();
            lpf->init(&xlator->out, lpfTaps);

            freqExtractor = std::make_unique<dsp::demod::Quadrature>();
            freqExtractor->init(&lpf->out, 1.0, rate);

            sink = std::make_unique<dsp::sink::Handler<float>>();
            sink->init(&freqExtractor->out, dspHandlerNFM, this);

            fmDemod->start(); audioLpf->start(); realToComplex->start();
            xlator->start(); lpf->start(); freqExtractor->start(); sink->start();
        }

        flog::info("[WEFAX] DSP chain built for {0} (rate={1} Hz, bw={2} Hz, audio_ctr={3} Hz)",
                   demodList.name(demodId), (int)rate, (int)bw, (int)audioCenter);
    }

    void stopChain() {
        if (sink)          sink->stop();
        if (freqExtractor) freqExtractor->stop();
        if (lpf)           lpf->stop();
        if (xlator)        xlator->stop();
        if (realToComplex) realToComplex->stop();
        if (audioLpf)      audioLpf->stop();
        if (fmDemod)       fmDemod->stop();
        sink.reset(); freqExtractor.reset(); lpf.reset(); xlator.reset();
        realToComplex.reset(); audioLpf.reset(); fmDemod.reset();
    }

    // ---- DSP handlers (audio-freq conversion per branch) ----
    static void dspHandlerUSB(float* data, int count, void* ctx) {
        WEFAXDecoderModule* _this = (WEFAXDecoderModule*)ctx;
        static thread_local std::vector<float> af;
        if ((int)af.size() < count) af.resize(count);
        float c = (float)_this->audioCenter;
        for (int i = 0; i < count; i++) af[i] = data[i] + c;
        _this->decoder.process(af.data(), count);
    }
    static void dspHandlerLSB(float* data, int count, void* ctx) {
        WEFAXDecoderModule* _this = (WEFAXDecoderModule*)ctx;
        static thread_local std::vector<float> af;
        if ((int)af.size() < count) af.resize(count);
        float c = (float)_this->audioCenter;
        for (int i = 0; i < count; i++) af[i] = c - data[i];
        _this->decoder.process(af.data(), count);
    }
    static void dspHandlerNFM(float* data, int count, void* ctx) {
        WEFAXDecoderModule* _this = (WEFAXDecoderModule*)ctx;
        static thread_local std::vector<float> af;
        if ((int)af.size() < count) af.resize(count);
        float c = (float)_this->audioCenter;
        for (int i = 0; i < count; i++) af[i] = data[i] + c;
        _this->decoder.process(af.data(), count);
    }

    // ------------------------------------------------------------------
    // Texture upload (UI thread). Allocated at full height once.
    // ------------------------------------------------------------------
    void uploadTexture() {
        int w = decoder.getImageWidth();
        int h = decoder.getImageHeight();
        if (w <= 0) return;

        std::lock_guard<std::mutex> tlck(textureMutex);
        if (texture == 0 || texWidth != w) {
            if (texture != 0) glDeleteTextures(1, &texture);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, wefax::WEFAX_MAX_LINES, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, nullptr);
            texWidth = w;
            texHeight = wefax::WEFAX_MAX_LINES;
        }
        {
            std::lock_guard<std::mutex> ilck(decoder.getImageMutex());
            const uint8_t* data = decoder.getImageRGB();
            int rows = std::min(h, wefax::WEFAX_MAX_LINES);
            if (data && rows > 0) {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, rows,
                                GL_RGB, GL_UNSIGNED_BYTE, data);
            }
        }
        textureDirty = false;
    }

    // ---- Image writers (stb) ----
    bool writeImage(const std::string& path, int w, int h, const uint8_t* rgb) {
        switch (formatList.value(formatId)) {
            case ImageFormat::BMP:  return stbi_write_bmp(path.c_str(), w, h, 3, rgb) != 0;
            case ImageFormat::PNG:  return stbi_write_png(path.c_str(), w, h, 3, rgb, w * 3) != 0;
            case ImageFormat::JPEG: {
                int q = std::clamp(jpegQuality, 1, 100);
                return stbi_write_jpg(path.c_str(), w, h, 3, rgb, q) != 0;
            }
        }
        return false;
    }
    const char* currentFormatExtension() const {
        switch (formatList.value(formatId)) {
            case ImageFormat::BMP:  return "bmp";
            case ImageFormat::PNG:  return "png";
            case ImageFormat::JPEG: return "jpg";
        }
        return "bmp";
    }
    std::string buildSaveFilename() {
        auto t = std::time(nullptr); std::tm tm_;
    #ifdef _WIN32
        localtime_s(&tm_, &t);
    #else
        localtime_r(&t, &tm_);
    #endif
        std::stringstream ss;
        ss << "wefax_" << std::put_time(&tm_, "%Y%m%d_%H%M%S")
           << "_" << (int)decoder.getLPM() << "lpm_ioc" << decoder.getIOC()
           << "." << currentFormatExtension();
        return ss.str();
    }
    void doSaveImage(bool reuseLast = false) {
        try { std::filesystem::create_directories(savePath); }
        catch (...) { flog::error("[WEFAX] Failed to create save dir: {0}", savePath); return; }
        // Re-rendering before copy guarantees the file matches the preview
        // (current slant / shift / median).
        decoder.renderSyncIfIdle();
        std::string fname = (reuseLast && !lastSavedFile.empty())
                          ? lastSavedFile
                          : savePath + "/" + buildSaveFilename();
        int w = decoder.getImageWidth();
        int h = decoder.getImageHeight();
        if (w <= 0 || h <= 0) return;
        std::vector<uint8_t> copy;
        {
            std::lock_guard<std::mutex> lck(decoder.getImageMutex());
            const uint8_t* p = decoder.getImageRGB();
            copy.assign(p, p + (size_t)w * h * 3);
        }
        if (writeImage(fname, w, h, copy.data())) {
            flog::info("[WEFAX] Saved: {0}", fname);
            lastSavedFile = fname;
        } else {
            flog::error("[WEFAX] Save failed: {0}", fname);
        }
    }

    // ------------------------------------------------------------------
    // Menu (ImGui) -- layout mirrors the SSTV module.
    // ------------------------------------------------------------------
    static void menuHandler(void* ctx) {
        WEFAXDecoderModule* _this = (WEFAXDecoderModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (!_this->enabled) { style::beginDisabled(); }

        if (_this->saveImageRequested.exchange(false)) _this->doSaveImage();

        // Re-render the image and refresh the preview right now using the
        // current slant/shift/median settings (so changes are visible even when
        // no signal is flowing, and the preview always matches what gets saved).
        auto refreshRender = [_this]() {
            _this->decoder.renderSyncIfIdle();
            { std::lock_guard<std::mutex> lck(_this->textureMutex); _this->textureDirty = true; }
            // Keep the saved file in sync with manual tuning: once an image has
            // been saved for this reception (via Save image or auto-save), any
            // later slant / shift / median change overwrites that same file, in
            // any state (including while still receiving). So the file on disk
            // always matches what is shown in the module.
            if (!_this->lastSavedFile.empty()) {
                _this->doSaveImage(true);
            }
        };

        // ---- Demod mode ----
        ImGui::LeftLabel("Demod");
        ImGui::FillWidth();
        if (ImGui::Combo(("##wefax_demod_" + _this->name).c_str(),
                         &_this->demodId, _this->demodList.txt)) {
            _this->buildChain(_this->demodList.value(_this->demodId));
            config.acquire();
            config.conf[_this->name]["demodId"] = _this->demodId;
            config.release(true);
        }

        // ---- LPM ----
        ImGui::LeftLabel("LPM");
        ImGui::FillWidth();
        if (ImGui::Combo(("##wefax_lpm_" + _this->name).c_str(),
                         &_this->lpmId, _this->lpmList.txt)) {
            _this->decoder.setLPM(_this->lpmList.value(_this->lpmId));
            config.acquire();
            config.conf[_this->name]["lpmId"] = _this->lpmId;
            config.release(true);
        }

        // ---- IOC ----
        ImGui::LeftLabel("IOC");
        ImGui::FillWidth();
        if (ImGui::Combo(("##wefax_ioc_" + _this->name).c_str(),
                         &_this->iocId, _this->iocList.txt)) {
            _this->decoder.setIOC(_this->iocList.value(_this->iocId));
            config.acquire();
            config.conf[_this->name]["iocId"] = _this->iocId;
            config.release(true);
        }

        // ---- Band view: smoothed histogram of the demodulated instantaneous
        //      frequency, with the fixed WEFAX references (black 1500, center
        //      1900, white 2300). Tune the audio-center so the signal energy
        //      sits between the black and white markers. Click/drag to set it.
        //      Same visual approach as the RTTY / PSK / MFSK modules. ----
        DemodMode demod = _this->demodList.value(_this->demodId);
        bool isNFM = (demod == DemodMode::NFM);
        float acMin = isNFM ? 1500.0f : 1100.0f;
        float acMax = isNFM ? 2300.0f : 1700.0f;
        {
            static float spec[wefax::WEFAXDecoder::SPEC_BINS];
            int nb = _this->decoder.getBandSpectrum(spec, wefax::WEFAXDecoder::SPEC_BINS);
            float flo = _this->decoder.getBandFlo();
            float fhi = _this->decoder.getBandFhi();

            float w = ImGui::GetContentRegionAvail().x;
            float h = 58.0f;
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 cBg     = IM_COL32(20, 22, 28, 255);
            ImU32 cBar    = IM_COL32(90, 160, 230, 255);
            ImU32 cBand   = IM_COL32(90, 160, 230, 45);
            ImU32 cBlack  = IM_COL32(80, 80, 90, 255);
            ImU32 cWhite  = IM_COL32(230, 230, 235, 255);
            ImU32 cCenter = IM_COL32(250, 200, 80, 255);
            dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), cBg, 3.0f);

            auto f2x = [&](float f) {
                return p0.x + w * ((f - flo) / (fhi - flo));
            };

            // Shaded band = expected signal footprint (black..white).
            dl->AddRectFilled(ImVec2(f2x(wefax::WEFAX_BLACK_HZ), p0.y),
                              ImVec2(f2x(wefax::WEFAX_WHITE_HZ), p0.y + h), cBand);

            // Spectrum bars.
            if (nb > 0) {
                float bw = w / (float)nb;
                for (int i = 0; i < nb; i++) {
                    float v = spec[i]; if (v < 0) v = 0; if (v > 1) v = 1;
                    float bh = v * (h - 4.0f);
                    float bx = p0.x + i * bw;
                    dl->AddRectFilled(ImVec2(bx, p0.y + h - bh),
                                      ImVec2(bx + bw + 0.5f, p0.y + h), cBar);
                }
            }

            // Markers: black / center / white.
            dl->AddLine(ImVec2(f2x(wefax::WEFAX_BLACK_HZ), p0.y),
                        ImVec2(f2x(wefax::WEFAX_BLACK_HZ), p0.y + h), cBlack, 1.5f);
            dl->AddLine(ImVec2(f2x(wefax::WEFAX_WHITE_HZ), p0.y),
                        ImVec2(f2x(wefax::WEFAX_WHITE_HZ), p0.y + h), cWhite, 1.5f);
            dl->AddLine(ImVec2(f2x(wefax::WEFAX_CENTER_HZ), p0.y),
                        ImVec2(f2x(wefax::WEFAX_CENTER_HZ), p0.y + h), cCenter, 2.0f);

            // Interaction: click/drag moves the clicked frequency onto the
            // center marker by adjusting the audio-center.
            ImGui::InvisibleButton(("##wefax_band_" + _this->name).c_str(), ImVec2(w, h));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float mx = ImGui::GetIO().MousePos.x;
                float frac = (mx - p0.x) / w; if (frac < 0) frac = 0; if (frac > 1) frac = 1;
                float fClick = flo + frac * (fhi - flo);
                double newAC = _this->audioCenter + (wefax::WEFAX_CENTER_HZ - fClick);
                if (newAC < acMin) newAC = acMin;
                if (newAC > acMax) newAC = acMax;
                if (std::abs(newAC - _this->audioCenter) > 0.5) {
                    _this->audioCenter = newAC;
                    if (isNFM) _this->audioCenterNFM = newAC; else _this->audioCenterSSB = newAC;
                    if (isNFM && _this->xlator)
                        _this->xlator->setOffset(-_this->audioCenter, _this->chainSampleRate);
                    config.acquire();
                    if (isNFM) config.conf[_this->name]["audioCenterNFM"] = _this->audioCenterNFM;
                    else       config.conf[_this->name]["audioCenterSSB"] = _this->audioCenterSSB;
                    config.release(true);
                }
            }
        }

        // ---- Audio center ----
        const char* acLabel = isNFM ? "Audio ctr (Hz)" : "Carrier ofs (Hz)";
        ImGui::LeftLabel(acLabel);
        ImGui::FillWidth();
        float ac = (float)_this->audioCenter;
        if (ImGui::SliderFloat(("##wefax_ac_" + _this->name).c_str(),
                                &ac, acMin, acMax, "%.0f")) {
            _this->audioCenter = ac;
            if (isNFM) _this->audioCenterNFM = ac; else _this->audioCenterSSB = ac;
            if (isNFM && _this->xlator)
                _this->xlator->setOffset(-_this->audioCenter, _this->chainSampleRate);
            config.acquire();
            if (isNFM) config.conf[_this->name]["audioCenterNFM"] = _this->audioCenterNFM;
            else       config.conf[_this->name]["audioCenterSSB"] = _this->audioCenterSSB;
            config.release(true);
        }
        if (isNFM)                          ImGui::TextDisabled("FM carrier in middle of VFO");
        else if (demod == DemodMode::USB)   ImGui::TextDisabled("Tune so energy sits black..white");
        else                                ImGui::TextDisabled("Tune so energy sits black..white");

        // ---- Status ----
        ImGui::Separator();
        ImGui::Text("State : %s", _this->decoder.getStateName());
        ImGui::Text("Format: IOC %d, %d LPM (%d px)",
                    _this->decoder.getIOC(), (int)_this->decoder.getLPM(),
                    _this->decoder.getImageWidth());
        ImGui::Text("Freq  : %.0f Hz", _this->decoder.getLastFreq());
        ImGui::Text("Lines : %d", _this->decoder.getLinesReceived());

        // ---- Reception quality indicator ----
        {
            float rms = _this->decoder.getSyncRmsResidual();
            float ratio = _this->decoder.getSyncDetectionRatio();
            if (rms >= 0.0f) {
                const char* qual; ImVec4 col;
                if (rms < 5.0f)       { qual = "Excellent"; col = ImVec4(0.20f,0.85f,0.20f,1.0f); }
                else if (rms < 20.0f) { qual = "Good";      col = ImVec4(0.55f,0.85f,0.20f,1.0f); }
                else if (rms < 50.0f) { qual = "Fair";      col = ImVec4(0.90f,0.75f,0.10f,1.0f); }
                else                  { qual = "Poor";      col = ImVec4(0.90f,0.30f,0.20f,1.0f); }
                ImGui::Text("Quality:");
                ImGui::SameLine();
                ImGui::TextColored(col, "%s", qual);
                ImGui::SameLine();
                ImGui::TextDisabled("(jitter %.1f, sync %.0f%%)",
                                    rms, (ratio >= 0.0f ? ratio * 100.0f : 0.0f));
            }
        }

        // ---- Image display ----
        ImGui::Separator();
        bool dirty;
        { std::lock_guard<std::mutex> lck(_this->textureMutex); dirty = _this->textureDirty; }
        if (dirty) _this->uploadTexture();
        int lines = _this->decoder.getLinesReceived();
        if (_this->texture != 0 && _this->texWidth > 0 && lines > 0) {
            float w = menuWidth;
            float h = w * (float)lines / (float)_this->texWidth;
            float v1 = (float)lines / (float)wefax::WEFAX_MAX_LINES;
            ImGui::Image((ImTextureID)(uintptr_t)_this->texture, ImVec2(w, h),
                         ImVec2(0, 0), ImVec2(1, v1));
        } else {
            ImGui::TextDisabled("(no image yet)");
        }

        // ---- Controls ----
        ImGui::Separator();
        if (ImGui::Button(("Force start##wefax_start_" + _this->name).c_str(),
                          ImVec2(menuWidth, 0))) {
            _this->imageSaved = false;
            _this->lastSavedFile.clear();
            _this->decoder.forceStart();
        }
        if (ImGui::Button(("Reset##wefax_reset_" + _this->name).c_str(),
                          ImVec2(menuWidth, 0))) {
            _this->imageSaved = false;
            _this->lastSavedFile.clear();
            _this->decoder.reset();
        }
        if (ImGui::Button(("Save image##wefax_save_" + _this->name).c_str(),
                          ImVec2(menuWidth, 0))) {
            _this->doSaveImage();
        }

        if (ImGui::Checkbox(("Auto-save on complete##wefax_autosave_" + _this->name).c_str(),
                            &_this->autoSave)) {
            config.acquire();
            config.conf[_this->name]["autoSave"] = _this->autoSave;
            config.release(true);
        }

        // ---- Image format ----
        ImGui::LeftLabel("Format");
        ImGui::FillWidth();
        if (ImGui::Combo(("##wefax_fmt_" + _this->name).c_str(),
                         &_this->formatId, _this->formatList.txt)) {
            config.acquire();
            config.conf[_this->name]["formatId"] = _this->formatId;
            config.release(true);
        }
        if (_this->formatList.value(_this->formatId) == ImageFormat::JPEG) {
            ImGui::LeftLabel("Quality");
            ImGui::FillWidth();
            int q = _this->jpegQuality;
            if (ImGui::SliderInt(("##wefax_jpegq_" + _this->name).c_str(), &q, 1, 100)) {
                _this->jpegQuality = q;
                config.acquire();
                config.conf[_this->name]["jpegQuality"] = _this->jpegQuality;
                config.release(true);
            }
        }

        ImGui::LeftLabel("Folder");
        ImGui::FillWidth();
        if (_this->folderSelect.render("##wefax_folder_" + _this->name)) {
            if (_this->folderSelect.pathIsValid()) {
                _this->savePath = _this->folderSelect.path;
                config.acquire();
                config.conf[_this->name]["savePath"] = _this->savePath;
                config.release(true);
            }
        }

        // ---- Robustness / quality options ----
        ImGui::Separator();

        bool astart = _this->decoder.getAutoStart();
        if (ImGui::Checkbox(("Auto-start on APT tone##wefax_astart_" + _this->name).c_str(),
                            &astart)) {
            _this->decoder.setAutoStart(astart);
            config.acquire();
            config.conf[_this->name]["autoStart"] = astart;
            config.release(true);
        }

        bool astop = _this->decoder.getAutoStopApt();
        if (ImGui::Checkbox(("Auto-stop on APT tone##wefax_astop_" + _this->name).c_str(),
                            &astop)) {
            _this->decoder.setAutoStopApt(astop);
            config.acquire();
            config.conf[_this->name]["autoStopApt"] = astop;
            config.release(true);
        }
        if (!astop) ImGui::TextDisabled("Decoding runs non-stop until Reset");

        bool aslant = _this->decoder.getAutoSlant();
        if (ImGui::Checkbox(("Auto slant (phasing)##wefax_aslant_" + _this->name).c_str(),
                            &aslant)) {
            _this->decoder.setAutoSlant(aslant);
            config.acquire();
            config.conf[_this->name]["autoSlant"] = aslant;
            config.release(true);
            refreshRender();
        }

        bool ransac = _this->decoder.getRansacEnabled();
        if (ImGui::Checkbox(("RANSAC slant correction##wefax_ransac_" + _this->name).c_str(),
                            &ransac)) {
            _this->decoder.setRansacEnabled(ransac);
            config.acquire();
            config.conf[_this->name]["ransac"] = ransac;
            config.release(true);
        }

        bool median = _this->decoder.getMedianFilterEnabled();
        if (ImGui::Checkbox(("Median filter (3x3, denoise)##wefax_median_" + _this->name).c_str(),
                            &median)) {
            _this->decoder.setMedianFilterEnabled(median);
            config.acquire();
            config.conf[_this->name]["median"] = median;
            config.release(true);
            refreshRender();
        }

        // ---- Learned slant (clock error) ----
        // Persist the learned ppm whenever a confident lock updates it. The
        // hardware clock error is constant, so this carries across receptions
        // and sessions, helping when a fax is tuned in mid-transmission (no
        // phasing preamble to lock on).
        if (_this->decoder.hasLearnedSlant()) {
            double lp = _this->decoder.getLearnedSlantPpm();
            if (std::abs(lp - _this->lastSavedLearnedPpm) > 0.5) {
                _this->lastSavedLearnedPpm = lp;
                config.acquire();
                config.conf[_this->name]["slantLearned"]    = true;
                config.conf[_this->name]["learnedSlantPpm"] = lp;
                config.release(true);
            }
            ImGui::TextDisabled("Learned slant: %.0f ppm", lp);
            ImGui::SameLine();
            if (ImGui::SmallButton(("Reset##wefax_lrst_" + _this->name).c_str())) {
                _this->decoder.clearLearnedSlant();
                _this->lastSavedLearnedPpm = 0.0;
                config.acquire();
                config.conf[_this->name]["slantLearned"]    = false;
                config.conf[_this->name]["learnedSlantPpm"] = 0.0;
                config.release(true);
                refreshRender();
            }
        }

        // ---- Manual trims (used mainly when auto-slant is off) ----
        ImGui::LeftLabel("Slant (ppm)");
        ImGui::FillWidth();
        float ppm = (float)_this->manualSlantPpm;
        if (ImGui::SliderFloat(("##wefax_ppm_" + _this->name).c_str(),
                                &ppm, -1000.0f, 1000.0f, "%.0f")) {
            _this->manualSlantPpm = ppm;
            _this->decoder.setManualSlantPpm(ppm);   // requests a re-render
            config.acquire();
            config.conf[_this->name]["manualSlantPpm"] = _this->manualSlantPpm;
            config.release(true);
        }
        // Synchronous re-render only when the drag finishes, so dragging stays
        // smooth and never blocks the decode thread per frame.
        if (ImGui::IsItemDeactivatedAfterEdit()) refreshRender();

        ImGui::LeftLabel("H-shift (px)");
        ImGui::FillWidth();
        int hs = _this->hShiftPixels;
        int hsMax = 1000;
        if (ImGui::SliderInt(("##wefax_hshift_" + _this->name).c_str(),
                              &hs, -hsMax, hsMax)) {
            _this->hShiftPixels = hs;
            _this->decoder.setHShiftPixels(hs);   // requests a re-render
            config.acquire();
            config.conf[_this->name]["hShift"] = _this->hShiftPixels;
            config.release(true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) refreshRender();

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

    VFOManager::VFO*            vfo = nullptr;
    DemodMode                   currentDemod = DemodMode::USB;
    double                      chainSampleRate = VFO_RATE_SSB;

    std::unique_ptr<dsp::demod::FM<float>>                       fmDemod;
    std::unique_ptr<dsp::filter::FIR<float, float>>              audioLpf;
    std::unique_ptr<dsp::convert::RealToComplex>                 realToComplex;
    std::unique_ptr<dsp::channel::FrequencyXlator>               xlator;
    std::unique_ptr<dsp::filter::FIR<dsp::complex_t, float>>     lpf;
    std::unique_ptr<dsp::demod::Quadrature>                      freqExtractor;
    std::unique_ptr<dsp::sink::Handler<float>>                   sink;
    dsp::tap<float>                                              lpfTaps{};
    dsp::tap<float>                                              audioLpfTaps{};

    wefax::WEFAXDecoder         decoder;

    OptionList<std::string, double>      lpmList;
    OptionList<std::string, int>         iocList;
    OptionList<std::string, DemodMode>   demodList;
    OptionList<std::string, ImageFormat> formatList;
    int     lpmId = 3;
    int     iocId = 0;
    int     demodId = 0;
    int     formatId = 0;
    int     jpegQuality = DEFAULT_JPEG_QUALITY;
    double  audioCenter = DEFAULT_AUDIO_CENTER_SSB;
    double  audioCenterSSB = DEFAULT_AUDIO_CENTER_SSB;
    double  audioCenterNFM = DEFAULT_AUDIO_CENTER_NFM;
    double  manualSlantPpm = 0.0;
    int     hShiftPixels = 0;
    double  lastSavedLearnedPpm = 0.0;

    GLuint      texture = 0;
    int         texWidth = 0;
    int         texHeight = 0;
    std::mutex  textureMutex;
    bool        textureDirty = false;

    bool                autoSave = true;
    bool                imageSaved = false;
    std::atomic<bool>   saveImageRequested{false};
    std::string         savePath;
    std::string         lastSavedFile;
    FolderSelect        folderSelect;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/wefax_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new WEFAXDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (WEFAXDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
