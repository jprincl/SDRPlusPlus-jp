#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <mutex>
#include <string>

namespace wefax {

    // --------------------------------------------------------------------
    // WEFAX (HF radiofacsimile / weather fax) tone plan.
    // WEFAX is frequency-modulated exactly like analog SSTV:
    //   black = 1500 Hz, white = 2300 Hz, apex/center = 1900 Hz.
    // The decoder therefore consumes a stream of *instantaneous frequency*
    // samples (Hz), identical to the SSTV decoder, and maps them to a
    // grayscale value per pixel.
    // --------------------------------------------------------------------
    constexpr float WEFAX_BLACK_HZ  = 1500.0f;
    constexpr float WEFAX_WHITE_HZ  = 2300.0f;
    constexpr float WEFAX_CENTER_HZ = 1900.0f;

    // APT (Automatic Picture Transmission) control tones.
    //   Start tone : 300 Hz (IOC 576) or 675 Hz (IOC 288), ~5 s.
    //   Stop  tone : 450 Hz, ~5 s.
    // These are black<->white square-wave alternations at the given rate.
    constexpr float APT_START_576_HZ = 300.0f;
    constexpr float APT_START_288_HZ = 675.0f;
    constexpr float APT_STOP_HZ      = 450.0f;

    // Hard cap on image height (lines). A 120 LPM chart of ~16 minutes.
    constexpr int   WEFAX_MAX_LINES  = 2000;

    // Slant-calibration quality gate (in samples of phasing-pulse residual).
    // A clean phasing preamble yields an RMS residual of only a few to ~20
    // samples; spurious pulses from image white content yield hundreds to
    // thousands. We refuse to APPLY or LOCK a fit whose residual exceeds this,
    // so auto-slant never corrupts the image when there is no real phasing.
    constexpr double WEFAX_CALIB_RMS_MAX = 60.0;

    // Index Of Cooperation -> pixels per scan line = round(IOC * pi).
    inline int iocToWidth(int ioc) {
        return (int)(ioc * 3.14159265358979323846 + 0.5);
    }

    class WEFAXDecoder {
    public:
        enum class State {
            IDLE,        // Waiting (optionally listening for the APT start tone)
            RECEIVING,   // Receiving phasing + image
            DONE         // APT stop tone seen / reception finished
        };

        WEFAXDecoder();
        ~WEFAXDecoder();

        // Initialize with the input sample rate (Hz, = rate of the freq stream).
        void init(double sampleRate);

        // Process a block of instantaneous-frequency samples (Hz).
        void process(const float* freqHz, int count);

        // Full reset: back to IDLE, image and calibration cleared.
        void reset();

        // Re-render the whole image from the raw buffer with the CURRENT slant /
        // shift / median settings. Safe to call from the UI thread when not
        // actively receiving (the raw buffer is frozen once reception is Done);
        // during reception it just requests the worker thread to re-render. Used
        // to guarantee the saved image matches exactly what is displayed.
        void renderSyncIfIdle();

        // ---- Parameters -------------------------------------------------
        // Lines per minute (60/90/100/120/180/240). Default 120.
        void  setLPM(double lpm);
        double getLPM() const { return lpm; }
        // Index Of Cooperation (576 or 288). Default 576.
        void  setIOC(int ioc);
        int   getIOC() const { return ioc; }

        // ---- State queries ---------------------------------------------
        State        getState()         const { return state; }
        int          getLinesReceived() const { return linesReceived; }
        int          getImageWidth()    const { return width; }
        int          getImageHeight()   const { return (linesReceived > 0) ? linesReceived : 1; }
        float        getProgress()      const;
        float        getLastFreq()      const { return lastAvgFreq; }
        const char*  getStateName()     const;

        // ---- Reception quality (mirrors the SSTV decoder) --------------
        // RMS of phasing-pulse timing residuals vs the calibrated regression,
        // in samples. Lower = cleaner. Returns -1 if no calibration yet.
        float        getSyncRmsResidual()    const { return syncRmsResidual; }
        // Fraction of expected phasing pulses actually detected (0..1).
        float        getSyncDetectionRatio() const;
        bool         isCalibrationLocked()   const { return calibrationLocked; }

        // ---- Image data access -----------------------------------------
        const uint8_t* getImageRGB() const {
            // When the median filter is on, the displayed/saved pixels come from
            // displayBuffer (median of the clean imageBuffer). Both the preview
            // and Save read this same buffer, so they always match.
            if (medianFilter && displayBuffer.size() == imageBuffer.size() && !displayBuffer.empty())
                return displayBuffer.data();
            return imageBuffer.data();
        }
        std::mutex&    getImageMutex()     { return imageMutex; }

        // ---- Band view (instantaneous-frequency distribution) ----------
        // A light, smoothed histogram of the incoming instantaneous frequency
        // over [BAND_FLO, BAND_FHI]. It is computed on the SAME stream the
        // decoder thresholds (so it is consistent with the black/center/white
        // references regardless of demod mode), letting the GUI show where the
        // demodulated energy sits for AF / audio-center alignment.
        static constexpr float BAND_FLO  = 1000.0f;
        static constexpr float BAND_FHI  = 2800.0f;
        static constexpr int   SPEC_BINS = 160;
        // Copies up to n smoothed bins (0..1) into out; returns count copied.
        int   getBandSpectrum(float* out, int n) const;
        float getBandFlo() const { return BAND_FLO; }
        float getBandFhi() const { return BAND_FHI; }

        // Debug/inspection: the line period (samples) currently used to render.
        double getEffectiveLinePeriod() const { return effectiveLinePeriod(); }

        // ---- Manual controls -------------------------------------------
        // Begin reception immediately at the current LPM/IOC (bypasses the
        // APT start tone). Anchors a fresh raw buffer at the current sample.
        void forceStart();

        // APT start-tone auto-detection: when on and in IDLE, the decoder
        // listens for the 300/675 Hz start tone and begins automatically
        // (also auto-selecting the IOC from the tone).
        void setAutoStart(bool on) { autoStart = on; }
        bool getAutoStart() const  { return autoStart; }
        // When false, the 450 Hz APT stop tone does NOT end reception, so the
        // decoder keeps running continuously until the user resets it.
        void setAutoStopApt(bool on) { autoStopApt = on; }
        bool getAutoStopApt() const  { return autoStopApt; }
        // True while actively building the image (past the phasing preamble),
        // used by the UI to show an "ongoing" indicator instead of a bar that
        // would otherwise sit misleadingly at 100%.
        bool isReceivingImage() const { return state == State::RECEIVING && linesReceived > 2; }

        // Automatic slant correction via phasing-pulse regression. When on,
        // RANSAC is used (robust against missed/spurious pulses); otherwise a
        // plain robust linear fit. When auto-slant is OFF entirely the manual
        // slant (ppm) value is used. (Reused from the SSTV slant engine.)
        void setAutoSlant(bool on) { autoSlant = on; reRenderRequested = true; }
        bool getAutoSlant() const  { return autoSlant; }
        void setRansacEnabled(bool on) { ransacEnabled = on; }
        bool getRansacEnabled() const  { return ransacEnabled; }

        // Manual slant trim in ppm (only used when auto-slant is OFF).
        void  setManualSlantPpm(double ppm) { manualSlantPpm = ppm; reRenderRequested = true; }
        double getManualSlantPpm() const    { return manualSlantPpm; }

        // Learned slant (ppm): set from the last CONFIDENT auto-slant lock.
        // The hardware clock error is constant, so this is reused as a fallback
        // when a reception is tuned in mid-fax (no phasing preamble to lock on).
        // The host persists it across sessions.
        double getLearnedSlantPpm() const   { return learnedSlantPpm; }
        bool   hasLearnedSlant() const      { return slantLearned; }
        void   setLearnedSlantPpm(double ppm) {
            learnedSlantPpm = ppm; slantLearned = true; reRenderRequested = true;
        }
        void   clearLearnedSlant() {
            learnedSlantPpm = 0.0; slantLearned = false; reRenderRequested = true;
        }

        // Manual horizontal alignment, in pixels (wraps the line start).
        void setHShiftPixels(int px) { hShiftPixels = px; reRenderRequested = true; }
        int  getHShiftPixels() const { return hShiftPixels; }

        // 3x3 median filter applied on render (denoise). Reused from SSTV.
        void setMedianFilterEnabled(bool on) { medianFilter = on; reRenderRequested = true; }
        bool getMedianFilterEnabled() const  { return medianFilter; }

        // ---- Notification ----------------------------------------------
        // Called from the process() thread whenever new lines are rendered.
        // 'line' is the latest line index (1-based).
        using LineCallback = std::function<void(int line)>;
        void setLineCallback(LineCallback cb) { lineCallback = cb; }

    private:
        // ---- Internals --------------------------------------------------
        void switchState(State s);
        void recomputeTimings();

        // APT tone detectors (black<->white alternation rate).
        void runAptDetectors(float freq);

        // Phasing-pulse detector (white burst on an otherwise black line).
        void detectPhasingPulse(float freq);

        // Calibration from phasing-pulse positions (linear / RANSAC).
        bool updateCalibration();
        bool updateCalibrationLinear();
        bool updateCalibrationRansac();

        // Render image lines from the raw frequency buffer using the current
        // calibration (or nominal / manual timing). Incremental or full.
        void renderNewLines();
        void renderAll();
        void renderLineRange(int firstLine, int lastLine);
        void applyMedianFilter();
        void applyMedianFilterRange(int firstLine, int lastLine);

        // Accumulate one instantaneous-frequency sample into the band-view
        // histogram (called for every incoming sample, in any state).
        void accumulateSpectrum(float f);

        // Map an instantaneous frequency (Hz) to a 0..255 gray value.
        static inline uint8_t freqToGray(float freq) {
            float v = (freq - WEFAX_BLACK_HZ) / (WEFAX_WHITE_HZ - WEFAX_BLACK_HZ);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            return (uint8_t)(v * 255.0f + 0.5f);
        }

        // Effective per-line period (samples) and absolute origin of line 0.
        double effectiveLinePeriod() const;
        double effectiveLineOrigin() const;

        // ---- Configuration ----
        double sampleRate;
        double lpm;
        int    ioc;
        int    width;                 // pixels per line = round(IOC*pi)

        State  state;
        float  lastAvgFreq;

        // ---- Raw capture ----
        // Every freq sample since the reception anchor (capped to MAX_LINES).
        std::vector<float> rawFreqBuffer;
        bool   bufferFull;            // hit the cap
        // Guards growth of rawFreqBuffer (worker thread) against reads during a
        // render triggered from the UI thread (save / manual re-render).
        std::mutex rawMutex;

        // Nominal per-line length (samples), = round(60/lpm * sampleRate)
        int    samplesPerLineNominal;

        // ---- Phasing-pulse detection (the WEFAX "sync") ----
        // The phasing signal is a black line with a short white pulse marking
        // the left margin. We detect the white burst and regress the pulse
        // centers against the line index to recover the true line period and
        // the left-edge offset -- exactly the SSTV sync-regression approach.
        bool   inWhitePulse;
        int    whitePulseStart;       // index in rawFreqBuffer
        std::vector<int> syncPositions;   // absolute pulse centers
        int    syncRefractoryRemaining;
        int    expectedSyncOffsetInCycle; // pulse-center offset from line start
        int    expectedPulseWidth;        // samples

        // ---- Calibration (samples) ----
        double calibratedSamplesPerCycle;   // = calibrated line period
        double calibratedFirstSyncOffset;   // first pulse absolute position
        float  syncRmsResidual;
        bool   calibrationValid;
        bool   calibrationLocked;
        double lockedCycleValue;
        int    stableCalibCount;
        int    nextCalibrationAt;

        // ---- Rendering bookkeeping ----
        int    linesReceived;        // committed image rows
        int    lastRenderedLine;     // for incremental render
        bool   reRenderRequested;    // force a full re-render next process()

        // ---- APT tone detection ----
        bool   autoStart;
        bool   autoStopApt;
        // Running alternation-rate estimator (sign changes of freq-center).
        int    aptSign;
        int    aptCrossings;
        long long aptWindowSamples;
        int    aptStartHoldSamples;  // sustained start-tone duration
        int    aptStopHoldSamples;   // sustained stop-tone duration

        // ---- Feature flags ----
        bool   autoSlant;
        bool   ransacEnabled;
        double manualSlantPpm;        int    hShiftPixels;
        bool   medianFilter;
        double learnedSlantPpm;       // ppm from the last confident lock
        bool   slantLearned;          // a confident lock has been seen

        // ---- Image buffer (RGB; gray replicated to R=G=B) ----
        std::vector<uint8_t> imageBuffer;   // width * MAX_LINES * 3 (CLEAN render)
        std::vector<uint8_t> displayBuffer; // imageBuffer with median applied
        std::mutex           imageMutex;

        LineCallback lineCallback;

        // ---- Band-view histogram (smoothed) ----
        std::vector<float> specMag;      // smoothed, normalized 0..1
        std::vector<float> specAccum;    // current accumulation window
        int                specCount;    // samples accumulated so far
        int                specWindow;   // window length (samples)
        mutable std::mutex specMtx;
    };

} // namespace wefax
