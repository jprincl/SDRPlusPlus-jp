#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <mutex>
#include "sstv_modes.h"

namespace sstv {

    // Sample rate used internally for the decoder
    // 12 kHz is the standard for SSTV decoders (QSSTV uses this)
    constexpr double INTERNAL_SAMPLERATE = 12000.0;

    // Bandwidth around audio carrier we listen to (Hz)
    // SSTV uses frequencies 1100 to 2300 Hz, plus margin for slant
    constexpr double SSTV_BANDWIDTH      = 3000.0;

    class SSTVDecoder {
    public:
        enum class State {
            HUNTING,        // Looking for VIS leader
            LEADER_LOCK,    // Leader confirmed, waiting for it to end
            VIS_TIMING,     // Counting samples since leader-end (break + 11 bits)
            IMAGE_RX,       // Receiving image data
            DONE            // Image complete
        };

        SSTVDecoder();
        ~SSTVDecoder();

        // Initialize with a sample rate (typically INTERNAL_SAMPLERATE)
        void init(double sampleRate);

        // Process a block of instantaneous frequency samples (in Hz)
        // This is what comes out of an FM demodulator
        void process(const float* freqHz, int count);

        // Reset state (kept in HUNTING, image cleared)
        void reset();

        // Reset just the image (keep state)
        void resetImage();

        // ---- State queries ----
        State        getState()         const { return state; }
        Mode         getCurrentMode()   const { return currentMode; }
        int          getLinesReceived() const { return linesReceived; }
        int          getImageWidth()    const;
        int          getImageHeight()   const;
        float        getProgress()      const;
        float        getLastFreq()      const { return lastAvgFreq; }
        const char*  getStateName()     const;

        // ---- Reception quality ----
        // RMS of sync timing residuals vs the calibrated regression, in samples.
        // Lower = cleaner signal. ~0-5 = excellent, 5-20 = good, >50 = poor/fading.
        // Returns -1 if no calibration yet.
        float        getSyncRmsResidual() const { return syncRmsResidual; }
        // Fraction of expected syncs that were actually detected (0..1).
        // Low values indicate signal loss / fading during reception.
        float        getSyncDetectionRatio() const;

        // ---- Image data access ----
        // RGB image, width*height*3 bytes (read-only)
        // Use getImageMutex() to lock while reading
        const uint8_t* getImageRGB() const { return imageBuffer.data(); }
        std::mutex&    getImageMutex()     { return imageMutex; }

        // ---- Mode control ----
        // Force a specific mode (skips VIS detection, useful for testing)
        // Mode::AUTO = automatic via VIS
        void setForcedMode(Mode m);
        Mode getForcedMode() const { return forcedMode; }

        // Force start of image reception (bypass VIS for testing)
        void forceStartImage();

        // Weak signal mode: relaxes leader/break detection thresholds for
        // noisy transmissions (ISS, satellite, low-SNR HF).
        //   - Leader detection: 80 ms (vs 150) consecutive samples required
        //   - Leader freq range widened: 1750-2050 Hz (vs 1820-1980)
        //   - LEADER_LOCK end threshold: < 1600 Hz (vs 1500)
        //   - Sync detector threshold: < 1450 Hz (vs 1350)
        void setWeakSignalMode(bool on) { weakSignalMode = on; }
        bool getWeakSignalMode() const  { return weakSignalMode; }

        // 3x3 median filter applied during render to remove isolated noise spikes.
        void setMedianFilterEnabled(bool on) { medianFilter = on; }
        bool getMedianFilterEnabled() const  { return medianFilter; }

        // RANSAC for slant calibration: robust against missed/spurious syncs.
        void setRansacEnabled(bool on) { ransacEnabled = on; }
        bool getRansacEnabled() const  { return ransacEnabled; }

        // ---- Notification ----
        // Called from process() thread when a new line is decoded
        using LineCallback = std::function<void(int line)>;
        void setLineCallback(LineCallback cb) { lineCallback = cb; }

    private:
        // ---- State machine handlers ----
        void processHunting(float freq);
        void processLeaderLock(float freq);
        void processVISTiming(float freq);
        void processImageRX(float freq);

        // Evaluate a completed VIS bit
        //   bitIdx 0 = start bit
        //   bitIdx 1..8 = data bits (LSB first)
        //   bitIdx 9 = parity bit
        //   bitIdx 10 = stop bit
        // Returns false if a hard error means we should abort VIS
        bool processVISBit(int bitIdx, float avgFreq);

        // Commit accumulated per-pixel sums to the image at end of cycle
        void commitCycleToImage();

        // Enter a new state, recompute params
        void switchState(State newState);

        // Recompute per-mode timing in samples
        void recomputeTimings();

        // Map frequency to pixel value (0..255)
        // black=1500Hz, white=2300Hz
        static inline uint8_t freqToPixel(float freq) {
            float v = (freq - SSTV_BLACK_HZ) / (SSTV_WHITE_HZ - SSTV_BLACK_HZ);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            return (uint8_t)(v * 255.0f);
        }

        // Classify a measured bit frequency to a logical bit
        //   ~1100 Hz -> 1
        //   ~1300 Hz -> 0
        static inline int classifyVISBit(float freq) {
            return (freq < 1200.0f) ? 1 : 0;
        }

        // ---- Configuration ----
        double sampleRate;
        State  state;
        Mode   currentMode;
        Mode   forcedMode;
        const ModeParams* modeParams;

        // ---- Diagnostics ----
        float lastAvgFreq;          // Smoothed frequency for UI

        // ---- VIS detection ----
        int     samplesPerVISBit;    // = sampleRate * 0.030 s
        int     samplesPerBreak;     // = sampleRate * 0.010 s
        int     leaderSamples;       // Count of consecutive leader samples (for HUNTING)
        int     leaderEndRun;        // Consecutive samples < 1500 Hz (LEADER_LOCK)
        int     visSampleCount;      // Samples since leader-end (in VIS_TIMING)
        float   visBitFreqSum;       // Accumulator for current bit averaging
        int     visBitFreqCount;
        int     visLastBitProcessed; // Last bit index that we already committed
        uint8_t visByte;             // Decoded byte (8 data bits)
        int    visParity;            // XOR of data bits

        // ---- Image reception ----
        int    linesReceived;       // Number of full image rows committed
        int    cyclesReceived;      // Number of mode cycles done (linesReceived = cyclesReceived * linesPerCycle)
        int    sampleInLine;        // Sample index within current cycle
        int    samplesPerCycle;     // = round(modeParams->cycleDuration * sampleRate)
        int    startupSkipRemaining; // Samples to discard at start of IMAGE_RX (Scottie's 9ms initial sync)

        // ---- Slant correction ----
        // Strategy: we record EVERY frequency sample during IMAGE_RX into a raw buffer,
        // detect sync pulses (1200 Hz, > 4 ms duration), and use a linear regression
        // over the sync positions to determine the TRUE samplesPerCycle.
        // Whenever we have enough syncs, we re-render the image from the raw buffer
        // with the corrected timing. This corrects slant in both the live preview
        // and the saved BMP.
        std::vector<float> rawFreqBuffer;        // All samples since start of IMAGE_RX
        int     rawSampleCounter;                // = rawFreqBuffer.size() (faster access)

        // Sync detection state
        bool    inSyncPulse;
        int     syncPulseStart;                  // index in rawFreqBuffer
        std::vector<int> syncPositions;          // center of each detected sync pulse
        // Refractory counter: after detecting a sync, ignore sync detection for
        // a fraction of a cycle so a noise spike right after the real sync can't
        // register a spurious second sync (which would corrupt the regression).
        int     syncRefractoryRemaining = 0;

        // Position (within a cycle) where the sync is expected for this mode.
        // = midpoint of the IGNORE segment whose duration matches the sync duration.
        // 0 means "no sync detection" (skip slant correction for this mode)
        int     expectedSyncOffsetInCycle;
        int     expectedSyncDurationSamples;     // for detector tolerance

        // Calibrated cycle length (in samples, possibly fractional during regression)
        double  calibratedSamplesPerCycle;
        // RMS of sync residuals vs the regression (reception quality metric, samples)
        float   syncRmsResidual = -1.0f;
        // Calibrated offset (samples from start of IMAGE_RX to the first sync)
        double  calibratedFirstSyncOffset;
        // Have we computed a calibration yet?
        bool    calibrationValid;
        // Once the calibration has converged (enough syncs, stable slope), we
        // LOCK it: stop recalibrating and stop full re-renders. This prevents
        // late-arriving spurious syncs (RF fades on long modes like PD290) from
        // shifting the calibration and corrupting already-good parts of the image.
        bool    calibrationLocked = false;
        double  lockedCycleValue = 0.0;    // for stability comparison
        int     stableCalibCount = 0;      // consecutive stable calibrations
        // How often we re-run calibration & re-render (in number of syncs)
        int     nextCalibrationAt;

        // Render the image from rawFreqBuffer using current calibration.
        // If calibrationValid is false, uses nominal samplesPerCycle.
        void renderImageFromRaw();

        // Update calibration from syncPositions (linear regression or RANSAC).
        // Returns true if calibration was updated.
        bool updateCalibration();
        bool updateCalibrationLinear();
        bool updateCalibrationRansac();

        // Apply a 3x3 median filter to imageBuffer in-place (post-render).
        void applyMedianFilter();

        // ---- Feature flags ----
        bool    weakSignalMode = false;
        bool    medianFilter   = false;
        bool    ransacEnabled  = false;

        // Precomputed segment table in sample units (start, end, pixel count, kind)
        struct SegmentRT {
            SegmentKind kind;
            int         startSample;    // inclusive
            int         endSample;      // exclusive
            int         pixelCount;
            int         spanSamples;    // = end - start
        };
        std::vector<SegmentRT> runtimeSegments;

        // Per-cycle accumulators for averaging (sum + count per pixel slot)
        // Slots are: 0=GREEN/Y1, 1=BLUE/B, 2=RED/R, 3=Y2 (PD only)
        // size = width * 4
        std::vector<float>   slotSumBuffer;
        std::vector<int>     slotCountBuffer;

        // ---- Image buffer (RGB) ----
        std::vector<uint8_t> imageBuffer;  // width*height*3 bytes, R G B per pixel
        std::mutex           imageMutex;

        // ---- Callback ----
        LineCallback lineCallback;
    };

} // namespace sstv
