#include "sstv_decoder.h"
#include <utils/flog.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sstv {

    // Map a SegmentKind to a "slot index" within the per-cycle accumulator.
    //   slots 0..3 are reused per ColorModel:
    //     Martin/Scottie (GBR):
    //       slot 0 = G, slot 1 = B, slot 2 = R
    //     PD (YUV):
    //       slot 0 = Y1, slot 1 = B-Y, slot 2 = R-Y, slot 3 = Y2
    //     Robot 72 (YUV per line):
    //       slot 0 = Y, slot 1 = B-Y, slot 2 = R-Y
    //     Robot 36 (YUV alternating chroma over 2 lines):
    //       slot 0 = Y_odd, slot 1 = B-Y, slot 2 = R-Y, slot 3 = Y_even
    //   Returns -1 if the kind has no slot (IGNORE).
    static inline int slotForKind(SegmentKind k) {
        switch (k) {
            case SegmentKind::Y_GREEN:    return 0;
            case SegmentKind::Y_BLUE:     return 1;
            case SegmentKind::Y_RED:      return 2;
            case SegmentKind::PD_Y1:      return 0;
            case SegmentKind::PD_B:       return 1;
            case SegmentKind::PD_R:       return 2;
            case SegmentKind::PD_Y2:      return 3;
            case SegmentKind::R72_Y:      return 0;
            case SegmentKind::R72_B:      return 1;
            case SegmentKind::R72_R:      return 2;
            case SegmentKind::R36_Y_ODD:  return 0;
            case SegmentKind::R36_BY:     return 1;
            case SegmentKind::R36_RY:     return 2;
            case SegmentKind::R36_Y_EVEN: return 3;
            default:                       return -1;
        }
    }

    SSTVDecoder::SSTVDecoder() {
        sampleRate = INTERNAL_SAMPLERATE;
        state = State::HUNTING;
        currentMode = Mode::UNKNOWN;
        forcedMode = Mode::AUTO;
        modeParams = nullptr;
        lastAvgFreq = 0.0f;

        samplesPerVISBit = 0;
        samplesPerBreak = 0;
        leaderSamples = 0;
        leaderEndRun = 0;
        visSampleCount = 0;
        visBitFreqSum = 0.0f;
        visBitFreqCount = 0;
        visLastBitProcessed = -1;
        visByte = 0;
        visParity = 0;

        linesReceived = 0;
        cyclesReceived = 0;
        sampleInLine = 0;
        samplesPerCycle = 0;
        startupSkipRemaining = 0;

        rawSampleCounter = 0;
        inSyncPulse = false;
        syncRefractoryRemaining = 0;
        syncPulseStart = 0;
        expectedSyncOffsetInCycle = 0;
        expectedSyncDurationSamples = 0;
        calibratedSamplesPerCycle = 0.0;
        calibratedFirstSyncOffset = 0.0;
        calibrationValid = false;
        calibrationLocked = false;
        stableCalibCount = 0;
        nextCalibrationAt = 3;
    }

    SSTVDecoder::~SSTVDecoder() {}

    void SSTVDecoder::init(double sampleRate_) {
        sampleRate = sampleRate_;
        samplesPerVISBit = (int)std::round(VIS_BIT_DURATION    * sampleRate);
        samplesPerBreak  = (int)std::round(VIS_BREAK_DURATION  * sampleRate);
        reset();
    }

    void SSTVDecoder::reset() {
        std::lock_guard<std::mutex> lck(imageMutex);
        state = State::HUNTING;
        currentMode = Mode::UNKNOWN;
        modeParams = nullptr;
        leaderSamples = 0;
        leaderEndRun = 0;
        visSampleCount = 0;
        visBitFreqSum = 0.0f;
        visBitFreqCount = 0;
        visLastBitProcessed = -1;
        visByte = 0;
        visParity = 0;
        linesReceived = 0;
        cyclesReceived = 0;
        sampleInLine = 0;
        imageBuffer.clear();
        slotSumBuffer.clear();
        slotCountBuffer.clear();
        runtimeSegments.clear();

        rawFreqBuffer.clear();
        rawSampleCounter = 0;
        inSyncPulse = false;
        syncRefractoryRemaining = 0;
        syncPositions.clear();
        calibrationValid = false;
        calibrationLocked = false;
        stableCalibCount = 0;
        nextCalibrationAt = 3;
        syncRmsResidual = -1.0f;
    }

    void SSTVDecoder::resetImage() {
        std::lock_guard<std::mutex> lck(imageMutex);
        std::fill(imageBuffer.begin(), imageBuffer.end(), 0);
        linesReceived = 0;
    }

    void SSTVDecoder::setForcedMode(Mode m) {
        forcedMode = m;
    }

    void SSTVDecoder::forceStartImage() {
        if (forcedMode == Mode::AUTO || forcedMode == Mode::UNKNOWN) {
            flog::warn("[SSTV] forceStartImage requires a non-AUTO forced mode");
            return;
        }
        currentMode = forcedMode;
        modeParams = getModeParams(currentMode);
        if (!modeParams) {
            flog::error("[SSTV] forceStartImage: unknown mode params");
            return;
        }
        recomputeTimings();
        sampleInLine = 0;
        linesReceived = 0;
        {
            std::lock_guard<std::mutex> lck(imageMutex);
            imageBuffer.assign(modeParams->width * modeParams->height * 3, 0);
        }
        switchState(State::IMAGE_RX);
        flog::info("[SSTV] Forced start: {0}, {1}x{2}", modeParams->longName,
                   (int)modeParams->width, (int)modeParams->height);
    }

    int SSTVDecoder::getImageWidth() const {
        return modeParams ? (int)modeParams->width : 0;
    }

    int SSTVDecoder::getImageHeight() const {
        return modeParams ? (int)modeParams->height : 0;
    }

    float SSTVDecoder::getProgress() const {
        if (!modeParams || modeParams->height == 0) return 0.0f;
        return (float)linesReceived / (float)modeParams->height;
    }

    const char* SSTVDecoder::getStateName() const {
        switch (state) {
            case State::HUNTING:      return "Hunting";
            case State::LEADER_LOCK:  return "Leader lock";
            case State::VIS_TIMING:   return "VIS";
            case State::IMAGE_RX:     return "Image RX";
            case State::DONE:         return "Done";
        }
        return "?";
    }

    void SSTVDecoder::switchState(State newState) {
        flog::debug("[SSTV] State: {0} -> {1}", (int)state, (int)newState);
        state = newState;
        if (newState == State::HUNTING) {
            leaderSamples = 0;
            leaderEndRun = 0;
        }
        else if (newState == State::LEADER_LOCK) {
            leaderEndRun = 0;
        }
        else if (newState == State::VIS_TIMING) {
            // visSampleCount is set by caller (preserved from leader-end)
            visBitFreqSum = 0.0f;
            visBitFreqCount = 0;
            visLastBitProcessed = -1;
            visByte = 0;
            visParity = 0;
        }
        else if (newState == State::IMAGE_RX) {
            sampleInLine = 0;
            linesReceived = 0;
            cyclesReceived = 0;
            startupSkipRemaining = modeParams
                ? (int)std::round(modeParams->startupOffsetDuration * sampleRate)
                : 0;
            if (modeParams) {
                slotSumBuffer.assign((size_t)modeParams->width * 4, 0.0f);
                slotCountBuffer.assign((size_t)modeParams->width * 4, 0);

                // Reserve raw freq buffer for the entire transmission.
                // Use a generous margin (+20%) so clock drift / extra samples
                // never trigger a mid-stream reallocation (which on a ~28 MB
                // buffer could stall and drop samples).
                int totalSamples = samplesPerCycle * (int)(modeParams->height / modeParams->linesPerCycle);
                rawFreqBuffer.clear();
                rawFreqBuffer.reserve((size_t)(totalSamples * 1.2) + 4096);

                // Pre-compute expected sync position from params
                expectedSyncOffsetInCycle  = (int)std::round(modeParams->syncOffsetInCycle * sampleRate);
                expectedSyncDurationSamples = (int)std::round(modeParams->syncDuration * sampleRate);

                // Initial calibration values = nominal
                calibratedSamplesPerCycle = (double)samplesPerCycle;
                calibratedFirstSyncOffset = (double)expectedSyncOffsetInCycle;
            }
            rawSampleCounter = 0;
            inSyncPulse = false;
            syncRefractoryRemaining = 0;
            syncPositions.clear();
            calibrationValid = false;
            calibrationLocked = false;
            stableCalibCount = 0;
            nextCalibrationAt = 3;
            syncRmsResidual = -1.0f;
        }
    }

    void SSTVDecoder::recomputeTimings() {
        if (!modeParams) return;

        // Build the runtime segment table using cumulative rounding so
        // each segment boundary lands at an integer sample, with no drift.
        runtimeSegments.clear();
        runtimeSegments.reserve(modeParams->segmentCount);
        double cursor = 0.0;
        int prevSample = 0;
        for (uint32_t i = 0; i < modeParams->segmentCount; i++) {
            const LineSegment& s = modeParams->segments[i];
            cursor += s.duration * sampleRate;
            int endSample = (int)std::round(cursor);
            SegmentRT rt;
            rt.kind         = s.kind;
            rt.startSample  = prevSample;
            rt.endSample    = endSample;
            rt.spanSamples  = endSample - prevSample;
            rt.pixelCount   = (int)s.pixelCount;
            runtimeSegments.push_back(rt);
            prevSample = endSample;
        }
        // samplesPerCycle = sum of rounded segments = endSample of the last one.
        // This guarantees segments sum exactly equals the cycle, avoiding
        // cumulative drift per cycle that would otherwise accumulate.
        samplesPerCycle = prevSample;

        flog::info("[SSTV] Mode {0}: samples/cycle={1}, segments={2}, width={3}, h={4}, linesPerCycle={5}",
                   modeParams->shortName, samplesPerCycle,
                   (int)runtimeSegments.size(), (int)modeParams->width,
                   (int)modeParams->height, (int)modeParams->linesPerCycle);
    }

    void SSTVDecoder::process(const float* freqHz, int count) {
        for (int i = 0; i < count; i++) {
            float f = freqHz[i];
            lastAvgFreq = 0.98f * lastAvgFreq + 0.02f * f;

            switch (state) {
                case State::HUNTING:      processHunting(f);     break;
                case State::LEADER_LOCK:  processLeaderLock(f);  break;
                case State::VIS_TIMING:   processVISTiming(f);   break;
                case State::IMAGE_RX:     processImageRX(f);     break;
                case State::DONE:                                break;
            }
        }
    }

    // ------------------------------------------------------------------
    // HUNTING: count consecutive samples near 1900 Hz (leader)
    //   Once we have 150 ms of leader (or 80 ms in weak mode), transition.
    // ------------------------------------------------------------------
    void SSTVDecoder::processHunting(float freq) {
        // Weak signal mode: wider freq window, shorter required duration
        const float LEADER_LO = weakSignalMode ? 1750.0f : 1820.0f;
        const float LEADER_HI = weakSignalMode ? 2050.0f : 1980.0f;
        const int   LEADER_REQUIRED = weakSignalMode
            ? (int)(0.080 * sampleRate)     //  80 ms in weak mode
            : (int)(0.150 * sampleRate);    // 150 ms normally

        if (freq > LEADER_LO && freq < LEADER_HI) {
            leaderSamples++;
            if (leaderSamples >= LEADER_REQUIRED) {
                flog::debug("[SSTV] Leader confirmed (weak={0})", weakSignalMode ? 1 : 0);
                switchState(State::LEADER_LOCK);
            }
        }
        else {
            // Decay slowly so brief glitches don't break detection
            if (leaderSamples > 0) leaderSamples -= 2;
            if (leaderSamples < 0) leaderSamples = 0;
        }
    }

    // ------------------------------------------------------------------
    // LEADER_LOCK: we have a confirmed leader, waiting for it to end.
    //   We use hysteresis: only declare the leader's end when we see
    //   at least ~3 ms (or 36 samples @ 12kHz) of consecutive frequency
    //   below 1500 Hz. This avoids a single filter-induced ripple sample
    //   in the 1600-1800 Hz zone from triggering a false transition.
    //   When triggered, we mark t=0 of VIS timing as the START of that
    //   sub-1500 Hz run (i.e. we rewind visSampleCount).
    // ------------------------------------------------------------------
    void SSTVDecoder::processLeaderLock(float freq) {
        // Weak mode: more permissive end-of-leader threshold
        const float LEADER_END_THRESH = weakSignalMode ? 1600.0f : 1500.0f;
        const float LEADER_LO = weakSignalMode ? 1750.0f : 1820.0f;
        const float LEADER_HI = weakSignalMode ? 2050.0f : 1980.0f;
        const int   MAX_LOCK_HOLD = (int)(0.500 * sampleRate);
        const int   END_RUN_REQUIRED = (int)(0.003 * sampleRate);

        if (freq < LEADER_END_THRESH) {
            leaderEndRun++;
            if (leaderEndRun >= END_RUN_REQUIRED) {
                flog::debug("[SSTV] Leader -> break (freq={0} Hz)", (int)freq);
                switchState(State::VIS_TIMING);
                visSampleCount = leaderEndRun;
                return;
            }
            return;
        }
        leaderEndRun = 0;

        if (freq > LEADER_LO && freq < LEADER_HI) {
            if (leaderSamples < MAX_LOCK_HOLD) leaderSamples++;
        }
        else {
            if (leaderSamples > 0) leaderSamples--;
            if (leaderSamples == 0) {
                flog::warn("[SSTV] Lost leader lock, back to hunt");
                switchState(State::HUNTING);
            }
        }
    }

    // ------------------------------------------------------------------
    // VIS_TIMING: visSampleCount = samples since leader-end (= start of break)
    //
    //   Timeline (from leader-end), CCIR-625 standard:
    //     0          ... 10 ms       : break (1200 Hz)
    //     10 ms      ... 40 ms       : start bit (1200 Hz)
    //     40 ms      ... 70 ms       : data bit 1 (LSB of 7-bit code)
    //     70 ms      ... 100 ms      : data bit 2
    //     ...
    //     220 ms     ... 250 ms      : data bit 7 (MSB of 7-bit code)
    //     250 ms     ... 280 ms      : parity bit (even over the 7 data bits)
    //     280 ms     ... 310 ms      : stop bit (1200 Hz)
    //     310 ms     ...             : image data
    //
    //   bitIdx 0 = start, 1..7 = data (7 bits), 8 = parity, 9 = stop
    // ------------------------------------------------------------------
    void SSTVDecoder::processVISTiming(float freq) {
        int sinceBitsStart = visSampleCount - samplesPerBreak;

        if (sinceBitsStart >= 0) {
            int bitIdx = sinceBitsStart / samplesPerVISBit;
            int bitPos = sinceBitsStart % samplesPerVISBit;

            if (bitIdx > 9) {
                // Should have transitioned to IMAGE_RX already; safety net
                flog::warn("[SSTV] VIS overrun, back to hunt");
                switchState(State::HUNTING);
                return;
            }

            // Sample only in the middle 50% of each bit
            int margin = samplesPerVISBit / 4;
            if (bitPos >= margin && bitPos < samplesPerVISBit - margin) {
                visBitFreqSum += freq;
                visBitFreqCount++;
            }

            // Bit boundary: just-completed bit can be processed
            if (bitPos == samplesPerVISBit - 1 && bitIdx != visLastBitProcessed) {
                visLastBitProcessed = bitIdx;
                float avg = visBitFreqCount > 0 ? visBitFreqSum / visBitFreqCount : 0.0f;
                bool ok = processVISBit(bitIdx, avg);
                visBitFreqSum = 0.0f;
                visBitFreqCount = 0;
                if (!ok) {
                    switchState(State::HUNTING);
                    return;
                }
                // After the stop bit (bitIdx 9), switch to IMAGE_RX
                if (bitIdx == 9) {
                    Mode m = lookupVIS(visByte);
                    if (m == Mode::UNKNOWN) {
                        flog::warn("[SSTV] Unknown VIS code 0x{0:02X}, back to hunt",
                                   (int)visByte);
                        switchState(State::HUNTING);
                        return;
                    }
                    currentMode = m;
                    modeParams = getModeParams(m);
                    if (!modeParams) {
                        flog::error("[SSTV] No params for mode {0}", (int)m);
                        switchState(State::HUNTING);
                        return;
                    }
                    flog::info("[SSTV] Identified mode: {0} (VIS 0x{1:02X})",
                               modeParams->longName, (int)visByte);
                    recomputeTimings();
                    {
                        std::lock_guard<std::mutex> lck(imageMutex);
                        imageBuffer.assign(modeParams->width * modeParams->height * 3, 0);
                    }
                    switchState(State::IMAGE_RX);
                    return;
                }
            }
        }
        visSampleCount++;
    }

    // ------------------------------------------------------------------
    // VIS bit decision logic
    //
    //   Standard CCIR-625 VIS structure (after the 10 ms break):
    //     bitIdx 0      : start bit  (30 ms @ 1200 Hz)
    //     bitIdx 1..7   : data bits  (30 ms each, LSB first, 7 bits = code)
    //     bitIdx 8      : parity bit (30 ms, even parity over the 7 data bits)
    //     bitIdx 9      : stop bit   (30 ms @ 1200 Hz)
    //   Total = 10 bits after start sync.
    //
    //   Frequency mapping:
    //     1100 Hz -> logic 1
    //     1300 Hz -> logic 0
    // ------------------------------------------------------------------
    bool SSTVDecoder::processVISBit(int bitIdx, float avg) {
        if (bitIdx == 0) {
            // Start bit, must be ~1200 Hz
            if (avg < 1100.0f || avg > 1300.0f) {
                return false;
            }
        }
        else if (bitIdx >= 1 && bitIdx <= 7) {
            // Data bits 1-7, LSB first
            int bit = (avg < 1200.0f) ? 1 : 0;
            int bitPos = bitIdx - 1;
            visByte |= (bit << bitPos);
            visParity ^= bit;
        }
        else if (bitIdx == 8) {
            // Parity bit (even parity over the 7 data bits)
            int parityBit = (avg < 1200.0f) ? 1 : 0;
            bool parityOK = (parityBit == visParity);
            if (!parityOK) {
                flog::warn("[SSTV] VIS parity error (byte 0x{0:02X})", (int)visByte);
                return false;
            }
        }
        else if (bitIdx == 9) {
            // Stop bit at 1200 Hz, lenient (no log needed)
        }
        return true;
    }

    // ------------------------------------------------------------------
    // IMAGE_RX: walk through the runtime segment table for each cycle.
    //   We accumulate samples into slot buffers keyed by SegmentKind.
    //   At end-of-cycle, commit pixels to the image (1 or 2 image lines).
    // ------------------------------------------------------------------
    void SSTVDecoder::processImageRX(float freq) {
        if (!modeParams || runtimeSegments.empty()) {
            switchState(State::HUNTING);
            return;
        }

        // Skip mandatory leading samples (e.g. Scottie's 9 ms startup sync)
        if (startupSkipRemaining > 0) {
            startupSkipRemaining--;
            return;
        }

        // ---- Record sample into the raw freq buffer ----
        rawFreqBuffer.push_back(freq);
        int absSampleIdx = rawSampleCounter;
        rawSampleCounter++;

        // ---- Sync pulse detector ----
        //   In normal mode: freq < 1350 Hz, >= syncDuration/3 samples wide
        //   In weak mode:   freq < 1450 Hz (more permissive), same duration
        // ---- Sync pulse detector ----
        //   In normal mode: freq < 1350 Hz, >= syncDuration/3 samples wide
        //   In weak mode:   freq < 1450 Hz (more permissive), same duration
        //
        //   REFRACTORY PERIOD: after registering a sync, we ignore new pulses
        //   for ~60% of the expected sync interval. This prevents a single noise
        //   spike mid-sync from splitting one long sync (e.g. PD290's 20 ms sync)
        //   into TWO detections - which would corrupt cycle-index inference and
        //   cascade into a garbled bottom of the image. The longest-sync modes
        //   (PD*) on the longest transmissions (PD290) were most affected.
        const float SYNC_THRESH_HZ = weakSignalMode ? 1450.0f : 1350.0f;
        const int   SYNC_MIN_SAMPLES = std::max(1, expectedSyncDurationSamples / 3);
        int spcRefr = (modeParams->syncsPerCycle > 0) ? modeParams->syncsPerCycle : 1;
        const int   SYNC_REFRACTORY = (samplesPerCycle / spcRefr) * 6 / 10;

        if (syncRefractoryRemaining > 0) {
            syncRefractoryRemaining--;
            // During refractory we still record samples and update line counter,
            // but we do NOT run sync detection.
        }
        else if (freq < SYNC_THRESH_HZ) {
            if (!inSyncPulse) {
                inSyncPulse = true;
                syncPulseStart = absSampleIdx;
            }
        }
        else {
            if (inSyncPulse) {
                int pulseLen = absSampleIdx - syncPulseStart;
                if (pulseLen >= SYNC_MIN_SAMPLES) {
                    int center = (syncPulseStart + absSampleIdx) / 2;

                    // --- Diagnostic: check the interval since the previous sync ---
                    if (!syncPositions.empty()) {
                        int spc2 = (modeParams->syncsPerCycle > 0)
                                     ? modeParams->syncsPerCycle : 1;
                        int expectedInterval = samplesPerCycle / spc2;
                        int interval = center - syncPositions.back();
                        int dev = interval - expectedInterval;
                        if (std::abs(dev) > expectedInterval / 10) {
                            flog::warn("[SSTV] Sync interval anomaly at sync #{0} "
                                       "(cycle ~{1}): interval={2}, expected={3}, dev={4}",
                                       (int)syncPositions.size(),
                                       (int)(center / samplesPerCycle),
                                       interval, expectedInterval, dev);
                        }
                    }

                    syncPositions.push_back(center);
                    // Start refractory period so a noise spike right after this
                    // sync can't immediately register a second (false) sync.
                    syncRefractoryRemaining = SYNC_REFRACTORY;
                    // Trigger calibration & re-render at progressive intervals,
                    // but ONLY while the calibration is not yet locked. Once
                    // locked, we keep the stable calibration and skip the
                    // expensive full re-render (late fades won't corrupt the
                    // already-decoded image).
                    if (!calibrationLocked &&
                        (int)syncPositions.size() >= nextCalibrationAt) {
                        if (updateCalibration()) {
                            renderImageFromRaw();
                            // Lock once we have a solid calibration. We base the
                            // decision on the JITTER (RMS residual) being low
                            // rather than on cycle-to-cycle stability, because
                            // RANSAC is stochastic and its cycle estimate can
                            // wobble by >1 sample between calls (which would
                            // otherwise prevent locking and let late syncs
                            // corrupt the image - the PD290 79% bug).
                            //   Lock when: >= 24 syncs AND jitter < 6 samples.
                            if (calibrationValid &&
                                syncPositions.size() >= 24 &&
                                syncRmsResidual >= 0.0f &&
                                syncRmsResidual < 6.0f) {
                                calibrationLocked = true;
                                lockedCycleValue = calibratedSamplesPerCycle;
                                flog::info("[SSTV] Calibration locked: cycle={0}, "
                                           "after {1} syncs (jitter {2})",
                                           (int)calibratedSamplesPerCycle,
                                           (int)syncPositions.size(),
                                           (int)syncRmsResidual);
                            }
                        }
                        nextCalibrationAt = (int)syncPositions.size()
                                            + (syncPositions.size() < 16 ? 2 : 8);
                    }
                    else if (calibrationLocked &&
                             (int)syncPositions.size() >= nextCalibrationAt) {
                        // Locked: refresh the preview occasionally using the
                        // FROZEN calibration. This is safe - it never changes
                        // the timing, so already-good lines stay good. We just
                        // fill in newly-received lines.
                        renderImageFromRaw();
                        nextCalibrationAt = (int)syncPositions.size() + 16;
                    }
                }
                inSyncPulse = false;
            }
        }

        // Update line counter (drives UI texture refresh) periodically
        int newLines = (int)(rawSampleCounter / (double)samplesPerCycle)
                       * (int)modeParams->linesPerCycle;
        if (newLines > linesReceived && newLines <= (int)modeParams->height) {
            linesReceived = newLines;
            cyclesReceived = (int)(rawSampleCounter / (double)samplesPerCycle);
            if (lineCallback) lineCallback(linesReceived);
        }

        // ---- Are we done? ----
        int totalCycles  = (int)(modeParams->height / modeParams->linesPerCycle);
        int totalSamples = samplesPerCycle * totalCycles;
        if (rawSampleCounter >= totalSamples) {
            // Final calibration ONLY if we never locked a stable one. If locked,
            // we trust the converged calibration and do NOT let end-of-image
            // syncs (possibly corrupted by a fade) disturb it.
            if (!calibrationLocked && syncPositions.size() >= 2) {
                updateCalibration();
            }
            renderImageFromRaw();
            linesReceived = (int)modeParams->height;
            cyclesReceived = totalCycles;
            flog::info("[SSTV] Image complete: {0} lines, {1} syncs, "
                       "cycle={2}, locked={3}, jitter={4}",
                       linesReceived, (int)syncPositions.size(),
                       (int)std::round(calibratedSamplesPerCycle),
                       calibrationLocked ? 1 : 0,
                       (int)syncRmsResidual);
            switchState(State::DONE);
            if (lineCallback) lineCallback(linesReceived);
            // Auto-reset back to HUNTING so the next image is detected automatically
            switchState(State::HUNTING);
        }
    }

    // YUV-to-RGB conversion for PD modes (BT.601 approximation, as used by QSSTV)
    //   Inputs: y (0..255), u (= B-Y, centered around 128), v (= R-Y, centered around 128)
    //   PD encoded with luminance Y at 1500..2300 = 0..255 like normal channels,
    //   and chroma R-Y, B-Y also at 1500..2300 = -128..+127 (centered).
    static inline void yuvToRGB(uint8_t y, uint8_t u_by, uint8_t v_ry,
                                 uint8_t& r, uint8_t& g, uint8_t& b) {
        // Standard BT.601 inverse - but PD's chroma centering is at 128
        int Y = (int)y;
        int U = (int)u_by - 128;   // B-Y
        int V = (int)v_ry - 128;   // R-Y
        int R = Y                + (int)(1.402f * V);
        int G = Y - (int)(0.344f * U) - (int)(0.714f * V);
        int B = Y + (int)(1.772f * U);
        if (R < 0) R = 0; if (R > 255) R = 255;
        if (G < 0) G = 0; if (G > 255) G = 255;
        if (B < 0) B = 0; if (B > 255) B = 255;
        r = (uint8_t)R; g = (uint8_t)G; b = (uint8_t)B;
    }

    void SSTVDecoder::commitCycleToImage() {
        if (!modeParams) return;
        if (linesReceived >= (int)modeParams->height) return;

        std::lock_guard<std::mutex> lck(imageMutex);
        const int w = (int)modeParams->width;

        if (modeParams->colorModel == ColorModel::MARTIN_GBR ||
            modeParams->colorModel == ColorModel::SCOTTIE_GBR) {
            uint8_t* row = &imageBuffer[linesReceived * w * 3];
            for (int x = 0; x < w; x++) {
                int gIdx = x * 4 + 0;
                int bIdx = x * 4 + 1;
                int rIdx = x * 4 + 2;
                uint8_t G = (slotCountBuffer[gIdx] > 0)
                              ? freqToPixel(slotSumBuffer[gIdx] / (float)slotCountBuffer[gIdx]) : 0;
                uint8_t B = (slotCountBuffer[bIdx] > 0)
                              ? freqToPixel(slotSumBuffer[bIdx] / (float)slotCountBuffer[bIdx]) : 0;
                uint8_t R = (slotCountBuffer[rIdx] > 0)
                              ? freqToPixel(slotSumBuffer[rIdx] / (float)slotCountBuffer[rIdx]) : 0;
                row[x*3 + 0] = R;
                row[x*3 + 1] = G;
                row[x*3 + 2] = B;
            }
        }
        else if (modeParams->colorModel == ColorModel::PD_YUV) {
            // 2 image lines per cycle. Y1, R-Y, B-Y, Y2
            uint8_t* row1 = &imageBuffer[(linesReceived    ) * w * 3];
            uint8_t* row2 = (linesReceived + 1 < (int)modeParams->height)
                              ? &imageBuffer[(linesReceived + 1) * w * 3]
                              : nullptr;
            for (int x = 0; x < w; x++) {
                int y1Idx = x * 4 + 0;
                int byIdx = x * 4 + 1;
                int ryIdx = x * 4 + 2;
                int y2Idx = x * 4 + 3;
                uint8_t Y1 = (slotCountBuffer[y1Idx] > 0)
                              ? freqToPixel(slotSumBuffer[y1Idx] / (float)slotCountBuffer[y1Idx]) : 0;
                uint8_t U  = (slotCountBuffer[byIdx] > 0)
                              ? freqToPixel(slotSumBuffer[byIdx] / (float)slotCountBuffer[byIdx]) : 128;
                uint8_t V  = (slotCountBuffer[ryIdx] > 0)
                              ? freqToPixel(slotSumBuffer[ryIdx] / (float)slotCountBuffer[ryIdx]) : 128;
                uint8_t Y2 = (slotCountBuffer[y2Idx] > 0)
                              ? freqToPixel(slotSumBuffer[y2Idx] / (float)slotCountBuffer[y2Idx]) : 0;

                uint8_t R, G, B;
                yuvToRGB(Y1, U, V, R, G, B);
                row1[x*3 + 0] = R;
                row1[x*3 + 1] = G;
                row1[x*3 + 2] = B;
                if (row2) {
                    yuvToRGB(Y2, U, V, R, G, B);
                    row2[x*3 + 0] = R;
                    row2[x*3 + 1] = G;
                    row2[x*3 + 2] = B;
                }
            }
        }
        else if (modeParams->colorModel == ColorModel::R72_YUV) {
            // 1 image line per cycle. Slots: 0=Y, 1=B-Y, 2=R-Y
            uint8_t* row = &imageBuffer[linesReceived * w * 3];
            for (int x = 0; x < w; x++) {
                int yIdx  = x * 4 + 0;
                int byIdx = x * 4 + 1;
                int ryIdx = x * 4 + 2;
                uint8_t Y  = (slotCountBuffer[yIdx] > 0)
                              ? freqToPixel(slotSumBuffer[yIdx] / (float)slotCountBuffer[yIdx]) : 0;
                uint8_t U  = (slotCountBuffer[byIdx] > 0)
                              ? freqToPixel(slotSumBuffer[byIdx] / (float)slotCountBuffer[byIdx]) : 128;
                uint8_t V  = (slotCountBuffer[ryIdx] > 0)
                              ? freqToPixel(slotSumBuffer[ryIdx] / (float)slotCountBuffer[ryIdx]) : 128;
                uint8_t R, G, B;
                yuvToRGB(Y, U, V, R, G, B);
                row[x*3 + 0] = R;
                row[x*3 + 1] = G;
                row[x*3 + 2] = B;
            }
        }
        else if (modeParams->colorModel == ColorModel::R36_YUV) {
            // 2 image lines per cycle. Slots:
            //   0 = Y_odd  (line N)
            //   1 = B-Y    (recorded on line N+1)
            //   2 = R-Y    (recorded on line N)
            //   3 = Y_even (line N+1)
            // Each chroma is only sampled once per pair-of-lines, applied to both rows.
            uint8_t* row1 = &imageBuffer[(linesReceived    ) * w * 3];
            uint8_t* row2 = (linesReceived + 1 < (int)modeParams->height)
                              ? &imageBuffer[(linesReceived + 1) * w * 3]
                              : nullptr;
            for (int x = 0; x < w; x++) {
                int y1Idx = x * 4 + 0;
                int byIdx = x * 4 + 1;
                int ryIdx = x * 4 + 2;
                int y2Idx = x * 4 + 3;
                uint8_t Y1 = (slotCountBuffer[y1Idx] > 0)
                              ? freqToPixel(slotSumBuffer[y1Idx] / (float)slotCountBuffer[y1Idx]) : 0;
                uint8_t U  = (slotCountBuffer[byIdx] > 0)
                              ? freqToPixel(slotSumBuffer[byIdx] / (float)slotCountBuffer[byIdx]) : 128;
                uint8_t V  = (slotCountBuffer[ryIdx] > 0)
                              ? freqToPixel(slotSumBuffer[ryIdx] / (float)slotCountBuffer[ryIdx]) : 128;
                uint8_t Y2 = (slotCountBuffer[y2Idx] > 0)
                              ? freqToPixel(slotSumBuffer[y2Idx] / (float)slotCountBuffer[y2Idx]) : 0;

                uint8_t R, G, B;
                yuvToRGB(Y1, U, V, R, G, B);
                row1[x*3 + 0] = R;
                row1[x*3 + 1] = G;
                row1[x*3 + 2] = B;
                if (row2) {
                    yuvToRGB(Y2, U, V, R, G, B);
                    row2[x*3 + 0] = R;
                    row2[x*3 + 1] = G;
                    row2[x*3 + 2] = B;
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    // Slant calibration via linear regression over detected sync positions
    //
    //   Model: sync_position[i] = a * cycleIdx[i] + b
    //     a = true samplesPerCycle (corrects for clock drift)
    //     b = offset of first sync from start of IMAGE_RX
    //
    //   For each sync, we infer its cycle index using the current best
    //   estimate (or nominal if no calibration yet), then refit.
    // ---------------------------------------------------------------------
    bool SSTVDecoder::updateCalibration() {
        if (!modeParams || syncPositions.size() < 2) return false;
        // RANSAC currently assumes 1 sync per cycle. For multi-sync modes
        // (Robot 36) fall back to the linear estimator which handles syncsPerCycle.
        int spc = (modeParams->syncsPerCycle > 0) ? modeParams->syncsPerCycle : 1;
        if (ransacEnabled && spc == 1 && syncPositions.size() >= 4) {
            return updateCalibrationRansac();
        }
        return updateCalibrationLinear();
    }

    bool SSTVDecoder::updateCalibrationLinear() {
        if (!modeParams || syncPositions.size() < 2) return false;

        // For modes with multiple syncs per cycle (Robot 36 = 2), each detected
        // sync advances by samplesPerCycle / syncsPerCycle. We regress against
        // a "sync index" and then multiply the slope by syncsPerCycle to get
        // the true per-cycle length.
        const int spc = (modeParams->syncsPerCycle > 0) ? modeParams->syncsPerCycle : 1;
        const double subCycle = (double)samplesPerCycle / (double)spc;

        double nominalSub  = subCycle;
        double curSub = calibrationValid ? (calibratedSamplesPerCycle / spc) : nominalSub;
        double nominalFirst = (double)expectedSyncOffsetInCycle;
        double curFirst = calibrationValid ? calibratedFirstSyncOffset : nominalFirst;

        // Infer sub-cycle (sync) index for each detected sync
        int n = (int)syncPositions.size();
        std::vector<int>  idx(n);
        std::vector<bool> use(n, true);
        for (int i = 0; i < n; i++) {
            int ci = (int)std::round((syncPositions[i] - curFirst) / curSub);
            if (ci < 0) ci = 0;
            idx[i] = ci;
        }

        // Iterative robust linear fit: fit, compute residuals, reject points
        // whose residual exceeds 2.5x the RMS, refit. This prevents a handful of
        // spurious syncs (noise crossing 1200 Hz during a fade) from dragging the
        // slope and slanting the whole image. Up to 3 rejection passes.
        double a = subCycle, b = curFirst;
        for (int pass = 0; pass < 4; pass++) {
            double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
            int cnt = 0;
            for (int i = 0; i < n; i++) {
                if (!use[i]) continue;
                double x = (double)idx[i];
                double y = (double)syncPositions[i];
                sumX += x; sumY += y;
                sumXY += x * y; sumX2 += x * x;
                cnt++;
            }
            if (cnt < 2) break;
            double denom = (double)cnt * sumX2 - sumX * sumX;
            if (std::abs(denom) < 1e-9) break;
            a = ((double)cnt * sumXY - sumX * sumY) / denom;
            b = (sumY - a * sumX) / (double)cnt;

            // Compute RMS residual
            double sumR2 = 0;
            for (int i = 0; i < n; i++) {
                if (!use[i]) continue;
                double r = (double)syncPositions[i] - (a * idx[i] + b);
                sumR2 += r * r;
            }
            double rms = std::sqrt(sumR2 / cnt);
            // On the last pass don't reject (just fit)
            if (pass == 3 || rms < 1.0) break;

            // Reject outliers beyond 2.5x RMS
            double thresh = 2.5 * rms;
            int rejected = 0;
            for (int i = 0; i < n; i++) {
                if (!use[i]) continue;
                double r = std::abs((double)syncPositions[i] - (a * idx[i] + b));
                if (r > thresh) { use[i] = false; rejected++; }
            }
            if (rejected == 0) break;
        }

        double fullCycle = a * spc;
        if (std::abs(fullCycle - (double)samplesPerCycle) > 0.05 * samplesPerCycle) {
            flog::warn("[SSTV] Slant calibration rejected: cycle={0} vs nominal {1}",
                       (int)fullCycle, samplesPerCycle);
            return false;
        }

        // Compute final RMS residual over the inlier set (quality metric)
        {
            double sumR2 = 0; int cnt = 0;
            for (int i = 0; i < n; i++) {
                if (!use[i]) continue;
                double r = (double)syncPositions[i] - (a * idx[i] + b);
                sumR2 += r * r; cnt++;
            }
            syncRmsResidual = (cnt > 0) ? (float)std::sqrt(sumR2 / cnt) : -1.0f;
        }

        calibratedSamplesPerCycle = fullCycle;
        calibratedFirstSyncOffset = b;
        calibrationValid = true;
        return true;
    }

    // ---------------------------------------------------------------------
    // RANSAC variant of calibration
    //   - Randomly pick 2 syncs, fit a line through them
    //   - Count how many of the OTHER syncs are within INLIER_TOL samples
    //   - Repeat MAX_ITER times, keep the model with the most inliers
    //   - Final fit = linear regression on the inliers only
    // ---------------------------------------------------------------------
    bool SSTVDecoder::updateCalibrationRansac() {
        if (!modeParams || syncPositions.size() < 4) return false;

        double nominalCycle = (double)samplesPerCycle;
        double nominalFirst = (double)expectedSyncOffsetInCycle;
        double curCycle = calibrationValid ? calibratedSamplesPerCycle : nominalCycle;
        double curFirst = calibrationValid ? calibratedFirstSyncOffset : nominalFirst;

        const int n = (int)syncPositions.size();
        std::vector<int> cycleIdx(n);
        for (int i = 0; i < n; i++) {
            int ci = (int)std::round((syncPositions[i] - curFirst) / curCycle);
            if (ci < 0) ci = 0;
            cycleIdx[i] = ci;
        }

        // Base tolerance ~30 samples (~1.25 ms at 24 kHz). For long modes
        // (PD290 = 308 cycles, ~5 min) a tiny clock-rate mismatch accumulates:
        // syncs late in the image drift from a line fitted on early syncs. If
        // the tolerance stays at 30, RANSAC drops the bottom syncs as outliers,
        // fits only the top, and renders the bottom at the wrong offset -> the
        // "corruption at 79%" bug. We widen the tolerance for longer images so
        // the whole transmission stays within the inlier band.
        int totalCyc = (int)(modeParams->height / modeParams->linesPerCycle);
        const double INLIER_TOL = 30.0 + 0.5 * (double)totalCyc;  // PD290: ~184
        const int    MAX_ITER   = 50;

        // Deterministic LCG to make this reproducible across runs (and avoid
        // pulling in <random>). Seed from the data itself.
        unsigned long rng = 0x9E3779B9UL ^ (unsigned long)syncPositions[0]
                          ^ ((unsigned long)syncPositions[n-1] << 16);
        auto nextRand = [&rng]() -> unsigned long {
            rng = rng * 1103515245UL + 12345UL;
            return (rng >> 8) & 0x7FFFFFFFUL;
        };

        int bestInlierCount = 0;
        double bestA = nominalCycle;
        double bestB = nominalFirst;

        for (int iter = 0; iter < MAX_ITER; iter++) {
            int i1 = (int)(nextRand() % n);
            int i2 = (int)(nextRand() % n);
            if (i1 == i2) continue;
            int dx = cycleIdx[i2] - cycleIdx[i1];
            if (dx == 0) continue;
            double a = (double)(syncPositions[i2] - syncPositions[i1]) / (double)dx;
            // Slope sanity check
            if (std::abs(a - nominalCycle) > 0.05 * nominalCycle) continue;
            double b = (double)syncPositions[i1] - a * (double)cycleIdx[i1];

            int inliers = 0;
            for (int i = 0; i < n; i++) {
                double predicted = a * (double)cycleIdx[i] + b;
                if (std::abs((double)syncPositions[i] - predicted) <= INLIER_TOL) {
                    inliers++;
                }
            }
            if (inliers > bestInlierCount) {
                bestInlierCount = inliers;
                bestA = a;
                bestB = b;
            }
        }

        if (bestInlierCount < std::max(3, n / 2)) {
            flog::warn("[SSTV] RANSAC rejected: only {0}/{1} inliers", bestInlierCount, n);
            // Fall back to linear regression
            return updateCalibrationLinear();
        }

        // Refit linearly on the inliers for best precision
        double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        int inlierN = 0;
        for (int i = 0; i < n; i++) {
            double predicted = bestA * (double)cycleIdx[i] + bestB;
            if (std::abs((double)syncPositions[i] - predicted) > INLIER_TOL) continue;
            double x = (double)cycleIdx[i];
            double y = (double)syncPositions[i];
            sumX += x; sumY += y;
            sumXY += x * y; sumX2 += x * x;
            inlierN++;
        }
        if (inlierN < 2) return false;
        double denom = (double)inlierN * sumX2 - sumX * sumX;
        if (std::abs(denom) < 1e-9) {
            calibratedSamplesPerCycle = bestA;
            calibratedFirstSyncOffset = bestB;
        } else {
            calibratedSamplesPerCycle = ((double)inlierN * sumXY - sumX * sumY) / denom;
            calibratedFirstSyncOffset = (sumY - calibratedSamplesPerCycle * sumX) / (double)inlierN;
        }

        if (std::abs(calibratedSamplesPerCycle - nominalCycle) > 0.05 * nominalCycle) {
            flog::warn("[SSTV] RANSAC final fit out of range, rejected");
            return false;
        }

        calibrationValid = true;
        // Compute RMS residual over inliers (quality metric).
        // RANSAC only runs for syncsPerCycle==1, so cycle length == sub-cycle length.
        {
            double sumR2 = 0; int cnt = 0;
            double a2 = calibratedSamplesPerCycle;
            for (int i = 0; i < n; i++) {
                double predicted = a2 * (double)cycleIdx[i] + calibratedFirstSyncOffset;
                double r = (double)syncPositions[i] - predicted;
                if (std::abs(r) <= INLIER_TOL) { sumR2 += r * r; cnt++; }
            }
            syncRmsResidual = (cnt > 0) ? (float)std::sqrt(sumR2 / cnt) : -1.0f;
        }
        // Only log occasionally to avoid flooding (every recalibration would
        // otherwise print a line; on PD290 that's 40+ lines).
        static int lastLoggedInliers = -100;
        if (std::abs(inlierN - lastLoggedInliers) >= 32 || inlierN < lastLoggedInliers) {
            flog::info("[SSTV] RANSAC: {0}/{1} inliers, calibrated cycle={2}",
                       inlierN, n, (int)calibratedSamplesPerCycle);
            lastLoggedInliers = inlierN;
        }
        return true;
    }

    // ---------------------------------------------------------------------
    // Render the entire image from rawFreqBuffer using current calibration.
    // This rebuilds the image from scratch so slant correction applies to
    // BOTH the live preview AND the saved BMP.
    // ---------------------------------------------------------------------
    void SSTVDecoder::renderImageFromRaw() {
        if (!modeParams || runtimeSegments.empty()) return;
        if (rawFreqBuffer.empty()) return;

        const int W = (int)modeParams->width;
        const int totalCycles = (int)(modeParams->height / modeParams->linesPerCycle);

        double cyclePeriod;
        double cycleZeroPos;
        if (calibrationValid) {
            cyclePeriod  = calibratedSamplesPerCycle;
            cycleZeroPos = calibratedFirstSyncOffset - (double)expectedSyncOffsetInCycle;
        } else {
            cyclePeriod  = (double)samplesPerCycle;
            cycleZeroPos = 0.0;
        }

        // Clear image
        {
            std::lock_guard<std::mutex> lck(imageMutex);
            imageBuffer.assign((size_t)W * modeParams->height * 3, 0);
        }

        // Render each cycle
        for (int c = 0; c < totalCycles; c++) {
            std::fill(slotSumBuffer.begin(),   slotSumBuffer.end(),   0.0f);
            std::fill(slotCountBuffer.begin(), slotCountBuffer.end(), 0);

            double cycleStart = cycleZeroPos + c * cyclePeriod;
            double scale = cyclePeriod / (double)samplesPerCycle;

            for (const SegmentRT& seg : runtimeSegments) {
                if (seg.kind == SegmentKind::IGNORE || seg.pixelCount == 0) continue;

                int slot = slotForKind(seg.kind);
                if (slot < 0) continue;

                double segStart = cycleStart + seg.startSample * scale;
                double segEnd   = cycleStart + seg.endSample   * scale;
                double segSpan  = segEnd - segStart;
                if (segSpan <= 0) continue;

                int sStart = (int)std::ceil(segStart);
                int sEnd   = (int)std::floor(segEnd);
                if (sStart < 0) sStart = 0;
                if (sEnd > (int)rawFreqBuffer.size()) sEnd = (int)rawFreqBuffer.size();

                for (int s = sStart; s < sEnd; s++) {
                    double scanPos = (double)s - segStart;
                    int pixelIdx = (int)(scanPos * seg.pixelCount / segSpan);
                    if (pixelIdx < 0 || pixelIdx >= seg.pixelCount) continue;
                    int bufIdx = pixelIdx * 4 + slot;
                    if (bufIdx >= (int)slotSumBuffer.size()) continue;
                    slotSumBuffer[bufIdx]   += rawFreqBuffer[(size_t)s];
                    slotCountBuffer[bufIdx] += 1;
                }
            }

            int saveLR = linesReceived;
            linesReceived = c * (int)modeParams->linesPerCycle;
            commitCycleToImage();
            linesReceived = saveLR;
        }

        // Optional post-processing: 3x3 median filter to remove isolated noise spikes
        if (medianFilter) {
            applyMedianFilter();
        }
    }

    // ---------------------------------------------------------------------
    // 3x3 median filter applied per-channel to imageBuffer.
    //   For each pixel (x, y) and each channel c, take the median of the 9
    //   surrounding pixels in a 3x3 neighborhood.
    //   Edges are handled by clamping to the nearest valid pixel.
    //   We operate on a copy to avoid contaminating the median computation
    //   with already-filtered values.
    // ---------------------------------------------------------------------
    void SSTVDecoder::applyMedianFilter() {
        if (!modeParams) return;
        std::lock_guard<std::mutex> lck(imageMutex);

        const int W = (int)modeParams->width;
        const int H = (int)modeParams->height;
        if (imageBuffer.size() < (size_t)W * H * 3) return;

        std::vector<uint8_t> src = imageBuffer;
        uint8_t window[9];

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                for (int c = 0; c < 3; c++) {
                    int k = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        int yy = y + dy;
                        if (yy < 0) yy = 0;
                        if (yy >= H) yy = H - 1;
                        for (int dx = -1; dx <= 1; dx++) {
                            int xx = x + dx;
                            if (xx < 0) xx = 0;
                            if (xx >= W) xx = W - 1;
                            window[k++] = src[(yy * W + xx) * 3 + c];
                        }
                    }
                    // Sort the 9 values, pick the middle one
                    std::sort(window, window + 9);
                    imageBuffer[(y * W + x) * 3 + c] = window[4];
                }
            }
        }
    }

    float SSTVDecoder::getSyncDetectionRatio() const {
        if (!modeParams || samplesPerCycle <= 0) return -1.0f;
        int totalCycles = (int)(modeParams->height / modeParams->linesPerCycle);
        if (totalCycles <= 0) return -1.0f;
        int spc = (modeParams->syncsPerCycle > 0) ? modeParams->syncsPerCycle : 1;
        int expectedSyncs = totalCycles * spc;
        if (expectedSyncs <= 0) return -1.0f;
        float ratio = (float)syncPositions.size() / (float)expectedSyncs;
        if (ratio > 1.0f) ratio = 1.0f;
        return ratio;
    }

} // namespace sstv
