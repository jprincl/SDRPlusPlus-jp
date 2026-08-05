#include "wefax_decoder.h"
#include <utils/flog.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace wefax {

    // White-detection threshold for the phasing pulse (between center & white)
    static constexpr float WHITE_THRESH = 2100.0f;

    WEFAXDecoder::WEFAXDecoder() {
        sampleRate            = 12000.0;
        lpm                   = 120.0;
        ioc                   = 576;
        width                 = iocToWidth(576);
        state                 = State::IDLE;
        lastAvgFreq           = WEFAX_CENTER_HZ;
        bufferFull            = false;
        samplesPerLineNominal = 6000;

        inWhitePulse          = false;
        whitePulseStart       = 0;
        syncRefractoryRemaining = 0;
        expectedSyncOffsetInCycle = 0;
        expectedPulseWidth    = 300;

        calibratedSamplesPerCycle = 6000.0;
        calibratedFirstSyncOffset = 0.0;
        syncRmsResidual       = -1.0f;
        calibrationValid      = false;
        calibrationLocked     = false;
        lockedCycleValue      = 0.0;
        stableCalibCount      = 0;
        nextCalibrationAt     = 2;

        linesReceived         = 0;
        lastRenderedLine      = 0;
        reRenderRequested     = false;

        autoStart             = false;
        autoStopApt           = true;
        aptSign               = 0;
        aptCrossings          = 0;
        aptWindowSamples      = 0;
        aptStartHoldSamples   = 0;
        aptStopHoldSamples    = 0;

        autoSlant             = true;
        ransacEnabled         = true;
        manualSlantPpm        = 0.0;
        hShiftPixels          = 0;
        medianFilter          = false;
        learnedSlantPpm       = 0.0;
        slantLearned          = false;

        specMag.assign(SPEC_BINS, 0.0f);
        specAccum.assign(SPEC_BINS, 0.0f);
        specCount             = 0;
        specWindow            = (int)(0.08 * sampleRate);  // ~80 ms
        if (specWindow < 1) specWindow = 1;
    }

    WEFAXDecoder::~WEFAXDecoder() {}

    void WEFAXDecoder::init(double sr) {
        sampleRate = (sr > 0.0) ? sr : 12000.0;
        recomputeTimings();
        reset();
    }

    void WEFAXDecoder::recomputeTimings() {
        if (sampleRate <= 0.0) sampleRate = 12000.0;
        width = iocToWidth(ioc);
        double linePeriodSec = 60.0 / lpm;
        samplesPerLineNominal = (int)std::round(linePeriodSec * sampleRate);
        if (samplesPerLineNominal < 1) samplesPerLineNominal = 1;
        // Phasing white pulse: ~5% of a scan line, centered on the left margin.
        expectedPulseWidth = std::max(1, (int)std::round(0.05 * samplesPerLineNominal));
        expectedSyncOffsetInCycle = expectedPulseWidth / 2;
        calibratedSamplesPerCycle = (double)samplesPerLineNominal;

        specWindow = (int)(0.08 * sampleRate);   // band-view window, ~80 ms
        if (specWindow < 1) specWindow = 1;

        // Pre-size the image (RGB), height capped to MAX_LINES.
        std::lock_guard<std::mutex> lck(imageMutex);
        imageBuffer.assign((size_t)width * WEFAX_MAX_LINES * 3, 0);
        displayBuffer.assign((size_t)width * WEFAX_MAX_LINES * 3, 0);
    }

    void WEFAXDecoder::setLPM(double v) {
        if (v <= 0.0) return;
        lpm = v;
        recomputeTimings();
        reset();
    }

    void WEFAXDecoder::setIOC(int v) {
        if (v != 288 && v != 576) return;
        ioc = v;
        recomputeTimings();
        reset();
    }

    void WEFAXDecoder::reset() {
        state = State::IDLE;
        rawFreqBuffer.clear();
        rawFreqBuffer.reserve((size_t)samplesPerLineNominal * 64);
        bufferFull = false;

        inWhitePulse = false;
        whitePulseStart = 0;
        syncPositions.clear();
        syncRefractoryRemaining = 0;

        calibratedSamplesPerCycle = (double)samplesPerLineNominal;
        calibratedFirstSyncOffset = 0.0;
        syncRmsResidual = -1.0f;
        calibrationValid = false;
        calibrationLocked = false;
        lockedCycleValue = 0.0;
        stableCalibCount = 0;
        nextCalibrationAt = 2;

        linesReceived = 0;
        lastRenderedLine = 0;
        reRenderRequested = false;

        aptSign = 0;
        aptCrossings = 0;
        aptWindowSamples = 0;
        aptStartHoldSamples = 0;
        aptStopHoldSamples = 0;

        std::lock_guard<std::mutex> lck(imageMutex);
        std::fill(imageBuffer.begin(), imageBuffer.end(), 0);
    }

    void WEFAXDecoder::switchState(State s) {
        state = s;
    }

    void WEFAXDecoder::forceStart() {
        // Begin a fresh reception at the current LPM/IOC.
        reset();
        switchState(State::RECEIVING);
        flog::info("[WEFAX] Reception started (LPM={0}, IOC={1}, {2} px/line, {3} samp/line)",
                   (int)lpm, ioc, width, samplesPerLineNominal);
    }

    const char* WEFAXDecoder::getStateName() const {
        switch (state) {
            case State::IDLE:      return autoStart ? "Idle (listening APT)" : "Idle";
            case State::DONE:      return "Done";
            case State::RECEIVING:
                if (calibrationLocked)            return "Image RX";
                if (syncPositions.size() >= 2)    return "Phasing (calibrating)";
                if (!syncPositions.empty())       return "Phasing";
                return "Receiving";
        }
        return "?";
    }

    float WEFAXDecoder::getProgress() const {
        if (state == State::DONE) return 1.0f;
        // Image reception is open-ended (continuous), so there is no meaningful
        // completion fraction: signal "indeterminate" so the UI shows an
        // ongoing indicator instead of a bar stuck at 100%.
        if (isReceivingImage()) return -1.0f;
        // During phasing, show progress toward a locked calibration.
        float conf = (float)syncPositions.size() / 8.0f;
        return std::min(1.0f, conf);
    }

    float WEFAXDecoder::getSyncDetectionRatio() const {
        // The phasing pulses only occur in the phasing preamble (before the
        // image), and detection stops once the calibration is locked. So the
        // meaningful metric is: over the span of phasing lines that actually
        // elapsed (first detected pulse .. last detected pulse), what fraction
        // of pulses did we catch. Computed this way the ratio stays stable
        // through the rest of the image instead of decaying toward zero.
        const int detected = (int)syncPositions.size();
        if (detected == 0) return -1.0f;
        if (detected == 1) return 1.0f;
        if (samplesPerLineNominal <= 0) return 1.0f;
        int span = syncPositions.back() - syncPositions.front();
        int expected = (int)std::round((double)span / (double)samplesPerLineNominal) + 1;
        if (expected < detected) expected = detected;   // guard rounding
        if (expected <= 0) return -1.0f;
        float ratio = (float)detected / (float)expected;
        return std::min(1.0f, ratio);
    }

    double WEFAXDecoder::effectiveLinePeriod() const {
        if (autoSlant) {
            // Only a CONFIRMED lock (or the persisted learned clock error)
            // overrides the manual trim. Transient "valid" fits are never used,
            // so the period can't flip between manual and a spurious calibration
            // from one render to the next (which made the saved image differ
            // from the preview).
            if (calibrationLocked) return calibratedSamplesPerCycle;
            if (slantLearned)
                return (double)samplesPerLineNominal * (1.0 + learnedSlantPpm * 1e-6);
        }
        return (double)samplesPerLineNominal * (1.0 + manualSlantPpm * 1e-6);
    }

    double WEFAXDecoder::effectiveLineOrigin() const {
        // Match effectiveLinePeriod: the regressed phasing offset is only used
        // once the calibration is locked. Otherwise the line origin is 0 and the
        // H-shift trim handles horizontal positioning.
        if (autoSlant && calibrationLocked) {
            return calibratedFirstSyncOffset - (double)expectedSyncOffsetInCycle;
        }
        return 0.0;
    }

    // ------------------------------------------------------------------
    // Main processing
    // ------------------------------------------------------------------
    void WEFAXDecoder::process(const float* freqHz, int count) {
        for (int i = 0; i < count; i++) {
            float f = freqHz[i];
            // Smooth frequency for the UI readout.
            lastAvgFreq += 0.001f * (f - lastAvgFreq);

            // Band-view histogram (runs in every state, for AF alignment).
            accumulateSpectrum(f);

            if (state == State::IDLE) {
                if (autoStart) runAptDetectors(f);
                continue;
            }
            if (state == State::DONE) continue;

            // ---- RECEIVING ----  (continuous: never auto-stops on buffer cap)
            {
                std::lock_guard<std::mutex> rlk(rawMutex);
                rawFreqBuffer.push_back(f);
                const size_t cap = (size_t)samplesPerLineNominal * (size_t)WEFAX_MAX_LINES;
                if (rawFreqBuffer.size() >= cap) {
                    // Scroll: drop the oldest block of whole lines and keep going,
                    // so reception continues indefinitely with bounded memory.
                    double period = effectiveLinePeriod();
                    if (period < 1.0) period = (double)samplesPerLineNominal;
                    long long trimSamples =
                        (long long)std::llround((WEFAX_MAX_LINES / 8) * period);
                    if (trimSamples < 1) trimSamples = 1;
                    if (trimSamples > (long long)rawFreqBuffer.size())
                        trimSamples = (long long)rawFreqBuffer.size();
                    rawFreqBuffer.erase(rawFreqBuffer.begin(),
                                        rawFreqBuffer.begin() + trimSamples);
                    // Preserve the within-line phasing phase across the scroll.
                    calibratedFirstSyncOffset =
                        std::fmod(calibratedFirstSyncOffset - (double)trimSamples, period);
                    if (calibratedFirstSyncOffset < 0) calibratedFirstSyncOffset += period;
                    for (auto& p : syncPositions) p -= (int)trimSamples;
                    syncPositions.erase(
                        std::remove_if(syncPositions.begin(), syncPositions.end(),
                                       [](int v){ return v < 0; }),
                        syncPositions.end());
                    lastRenderedLine = 0;
                    reRenderRequested = true;
                }
            }

            // APT stop tone -> finish.
            runAptDetectors(f);

            // Phasing-pulse detection feeds the slant calibration.
            if (!calibrationLocked) detectPhasingPulse(f);

            if (syncRefractoryRemaining > 0) syncRefractoryRemaining--;
        }

        if (state == State::RECEIVING || state == State::DONE) {
            // Recalibrate when we have collected new pulses.
            if (!calibrationLocked &&
                (int)syncPositions.size() >= nextCalibrationAt &&
                syncPositions.size() >= 2) {
                nextCalibrationAt = (int)syncPositions.size() + 2;
                if (updateCalibration()) {
                    reRenderRequested = true;
                    // Lock once stable over enough pulses AND the fit quality is
                    // good (low residual). updateCalibration() already rejected
                    // high-residual fits, but we double-check here before the
                    // permanent lock so we never freeze a marginal solution.
                    bool goodQuality = (syncRmsResidual >= 0.0f &&
                                        syncRmsResidual < WEFAX_CALIB_RMS_MAX);
                    if (syncPositions.size() >= 8 && goodQuality) {
                        double rel = (lockedCycleValue > 0.0)
                            ? std::abs(calibratedSamplesPerCycle - lockedCycleValue) / lockedCycleValue
                            : 1.0;
                        // Lock when two successive fits agree closely, or once we
                        // have plenty of consistent pulses (clean phasing).
                        bool stableEnough = (rel < 0.0005 && ++stableCalibCount >= 2);
                        bool plentyPulses = (syncPositions.size() >= 14);
                        if (rel >= 0.0005) stableCalibCount = 0;
                        if (stableEnough || plentyPulses) {
                            calibrationLocked = true;
                            learnedSlantPpm = (calibratedSamplesPerCycle /
                                               (double)samplesPerLineNominal - 1.0) * 1e6;
                            slantLearned = true;
                            flog::info("[WEFAX] Slant LOCKED: {0} samp/line (nominal {1}), jitter {2}, learned {3} ppm",
                                       (int)calibratedSamplesPerCycle, samplesPerLineNominal,
                                       syncRmsResidual, (int)learnedSlantPpm);
                        }
                        lockedCycleValue = calibratedSamplesPerCycle;
                    }
                }
            }

            if (reRenderRequested) {
                renderAll();
                reRenderRequested = false;
            } else {
                renderNewLines();
            }
        }
    }

    // ------------------------------------------------------------------
    // APT start/stop tone detection (black<->white alternation rate)
    // ------------------------------------------------------------------
    void WEFAXDecoder::runAptDetectors(float freq) {
        int sign = (freq > WEFAX_CENTER_HZ) ? 1 : -1;
        if (aptSign != 0 && sign != aptSign) aptCrossings++;
        aptSign = sign;
        aptWindowSamples++;

        // Evaluate every ~0.4 s window.
        long long windowLen = (long long)(0.4 * sampleRate);
        if (windowLen < 1) windowLen = 1;
        if (aptWindowSamples < windowLen) return;

        double windowSec = (double)aptWindowSamples / sampleRate;
        double toneHz = (double)aptCrossings / (2.0 * windowSec);
        aptCrossings = 0;
        aptWindowSamples = 0;

        const int holdNeeded = (int)(1.6 / 0.4);  // ~1.6 s sustained

        if (state == State::IDLE) {
            // Start tone: 300 Hz (IOC 576) or 675 Hz (IOC 288).
            if (toneHz > 250.0 && toneHz < 360.0) {
                if (++aptStartHoldSamples >= holdNeeded) {
                    setIOC(576);
                    forceStart();
                }
                aptStopHoldSamples = 0;
            } else if (toneHz > 580.0 && toneHz < 760.0) {
                if (++aptStartHoldSamples >= holdNeeded) {
                    setIOC(288);
                    forceStart();
                }
            } else {
                aptStartHoldSamples = 0;
            }
        } else if (state == State::RECEIVING) {
            // Stop tone: 450 Hz. Only acted on if auto-stop is enabled, so the
            // decoder can run continuously when the user wants non-stop capture.
            if (!autoStopApt) { aptStopHoldSamples = 0; return; }
            if (toneHz > 400.0 && toneHz < 500.0) {
                if (++aptStopHoldSamples >= holdNeeded) {
                    flog::info("[WEFAX] APT stop tone detected -> reception complete");
                    renderAll();
                    switchState(State::DONE);
                    if (lineCallback) lineCallback(linesReceived);
                }
            } else {
                aptStopHoldSamples = 0;
            }
        }
    }

    // ------------------------------------------------------------------
    // Phasing-pulse detector. During the phasing interval each line is black
    // with a short white burst marking the left margin. We record the burst
    // center as a "sync" position for the slant regression.
    // ------------------------------------------------------------------
    void WEFAXDecoder::detectPhasingPulse(float freq) {
        int idx = (int)rawFreqBuffer.size() - 1;
        if (freq > WHITE_THRESH) {
            if (!inWhitePulse) {
                inWhitePulse = true;
                whitePulseStart = idx;
            }
        } else {
            if (inWhitePulse) {
                inWhitePulse = false;
                int w = idx - whitePulseStart;
                if (syncRefractoryRemaining == 0 &&
                    w >= (int)(0.4 * expectedPulseWidth) &&
                    w <= (int)(2.2 * expectedPulseWidth)) {
                    int center = (whitePulseStart + idx) / 2;
                    syncPositions.push_back(center);
                    syncRefractoryRemaining = samplesPerLineNominal / 2;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Calibration (linear robust / RANSAC) -- ported from the SSTV slant
    // engine, single sync per cycle (one phasing pulse per line).
    // ------------------------------------------------------------------
    bool WEFAXDecoder::updateCalibration() {
        if (syncPositions.size() < 2) return false;
        if (autoSlant && ransacEnabled && syncPositions.size() >= 4) {
            return updateCalibrationRansac();
        }
        return updateCalibrationLinear();
    }

    bool WEFAXDecoder::updateCalibrationLinear() {
        if (syncPositions.size() < 2) return false;

        const double subCycle = (double)samplesPerLineNominal;
        double curSub   = calibrationValid ? calibratedSamplesPerCycle : subCycle;
        double curFirst = calibrationValid ? calibratedFirstSyncOffset
                                           : (double)expectedSyncOffsetInCycle;

        int n = (int)syncPositions.size();
        std::vector<int>  idx(n);
        std::vector<bool> use(n, true);
        for (int i = 0; i < n; i++) {
            int ci = (int)std::round((syncPositions[i] - curFirst) / curSub);
            if (ci < 0) ci = 0;
            idx[i] = ci;
        }

        // Iterative robust fit: reject points whose residual exceeds 2.5x RMS.
        double a = subCycle, b = curFirst;
        for (int pass = 0; pass < 4; pass++) {
            double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
            int cnt = 0;
            for (int i = 0; i < n; i++) {
                if (!use[i]) continue;
                double x = (double)idx[i], y = (double)syncPositions[i];
                sumX += x; sumY += y; sumXY += x * y; sumX2 += x * x; cnt++;
            }
            if (cnt < 2) break;
            double denom = (double)cnt * sumX2 - sumX * sumX;
            if (std::abs(denom) < 1e-9) break;
            a = ((double)cnt * sumXY - sumX * sumY) / denom;
            b = (sumY - a * sumX) / (double)cnt;

            double sumR2 = 0;
            for (int i = 0; i < n; i++) {
                if (!use[i]) continue;
                double r = (double)syncPositions[i] - (a * idx[i] + b);
                sumR2 += r * r;
            }
            double rms = std::sqrt(sumR2 / cnt);
            if (pass == 3 || rms < 1.0) break;
            double thresh = 2.5 * rms;
            int rejected = 0;
            for (int i = 0; i < n; i++) {
                if (!use[i]) continue;
                double r = std::abs((double)syncPositions[i] - (a * idx[i] + b));
                if (r > thresh) { use[i] = false; rejected++; }
            }
            if (rejected == 0) break;
        }

        if (std::abs(a - (double)samplesPerLineNominal) > 0.05 * samplesPerLineNominal) {
            flog::warn("[WEFAX] Slant calibration rejected: line={0} vs nominal {1}",
                       (int)a, samplesPerLineNominal);
            return false;
        }

        double sumR2 = 0; int cnt = 0;
        for (int i = 0; i < n; i++) {
            if (!use[i]) continue;
            double r = (double)syncPositions[i] - (a * idx[i] + b);
            sumR2 += r * r; cnt++;
        }
        syncRmsResidual = (cnt > 0) ? (float)std::sqrt(sumR2 / cnt) : -1.0f;

        // Quality gate: a real phasing preamble gives a small residual; pulses
        // picked up from image white content give a huge one. Refuse to apply
        // a low-quality fit so auto-slant never corrupts the image.
        if (syncRmsResidual < 0.0f || syncRmsResidual > WEFAX_CALIB_RMS_MAX) {
            calibrationValid = false;
            return false;
        }

        calibratedSamplesPerCycle = a;
        calibratedFirstSyncOffset = b;
        calibrationValid = true;
        return true;
    }

    bool WEFAXDecoder::updateCalibrationRansac() {
        if (syncPositions.size() < 4) return false;

        double nominalCycle = (double)samplesPerLineNominal;
        double curCycle = calibrationValid ? calibratedSamplesPerCycle : nominalCycle;
        double curFirst = calibrationValid ? calibratedFirstSyncOffset
                                           : (double)expectedSyncOffsetInCycle;

        const int n = (int)syncPositions.size();
        std::vector<int> cycleIdx(n);
        for (int i = 0; i < n; i++) {
            int ci = (int)std::round((syncPositions[i] - curFirst) / curCycle);
            if (ci < 0) ci = 0;
            cycleIdx[i] = ci;
        }

        const double INLIER_TOL = 30.0 + 0.25 * (double)n;
        const int    MAX_ITER   = 60;

        unsigned long rng = 0x9E3779B9UL ^ (unsigned long)syncPositions[0]
                          ^ ((unsigned long)syncPositions[n - 1] << 16);
        auto nextRand = [&rng]() -> unsigned long {
            rng = rng * 1103515245UL + 12345UL;
            return (rng >> 8) & 0x7FFFFFFFUL;
        };

        int bestInliers = 0;
        double bestA = nominalCycle, bestB = curFirst;

        for (int iter = 0; iter < MAX_ITER; iter++) {
            int i1 = (int)(nextRand() % n);
            int i2 = (int)(nextRand() % n);
            if (i1 == i2) continue;
            int dx = cycleIdx[i2] - cycleIdx[i1];
            if (dx == 0) continue;
            double a = (double)(syncPositions[i2] - syncPositions[i1]) / (double)dx;
            if (std::abs(a - nominalCycle) > 0.05 * nominalCycle) continue;
            double b = (double)syncPositions[i1] - a * (double)cycleIdx[i1];
            int inliers = 0;
            for (int i = 0; i < n; i++) {
                double pred = a * (double)cycleIdx[i] + b;
                if (std::abs((double)syncPositions[i] - pred) <= INLIER_TOL) inliers++;
            }
            if (inliers > bestInliers) { bestInliers = inliers; bestA = a; bestB = b; }
        }

        if (bestInliers < std::max(3, n / 2)) {
            return updateCalibrationLinear();
        }

        double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        int inlierN = 0;
        for (int i = 0; i < n; i++) {
            double pred = bestA * (double)cycleIdx[i] + bestB;
            if (std::abs((double)syncPositions[i] - pred) > INLIER_TOL) continue;
            double x = (double)cycleIdx[i], y = (double)syncPositions[i];
            sumX += x; sumY += y; sumXY += x * y; sumX2 += x * x; inlierN++;
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
            return false;
        }

        calibrationValid = true;
        double sumR2 = 0; int cnt = 0;
        for (int i = 0; i < n; i++) {
            double pred = calibratedSamplesPerCycle * (double)cycleIdx[i] + calibratedFirstSyncOffset;
            double r = (double)syncPositions[i] - pred;
            if (std::abs(r) <= INLIER_TOL) { sumR2 += r * r; cnt++; }
        }
        syncRmsResidual = (cnt > 0) ? (float)std::sqrt(sumR2 / cnt) : -1.0f;
        if (syncRmsResidual < 0.0f || syncRmsResidual > WEFAX_CALIB_RMS_MAX) {
            calibrationValid = false;
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Rendering from the raw frequency buffer
    // ------------------------------------------------------------------
    void WEFAXDecoder::renderLineRange(int firstLine, int lastLine) {
        if (firstLine > lastLine) return;
        const double period = effectiveLinePeriod();
        if (period < 1.0) return;
        const double origin = effectiveLineOrigin();
        const double spp = period / (double)width;

        std::lock_guard<std::mutex> lck(imageMutex);
        std::lock_guard<std::mutex> rlk(rawMutex);
        const int rawNow = (int)rawFreqBuffer.size();
        for (int c = firstLine; c <= lastLine && c < WEFAX_MAX_LINES; c++) {
            double lineStart = origin + (double)c * period;
            uint8_t* row = &imageBuffer[(size_t)c * width * 3];
            for (int x = 0; x < width; x++) {
                double s0 = lineStart + ((double)x - (double)hShiftPixels) * spp;
                double s1 = s0 + spp;
                int is0 = (int)std::floor(s0);
                int is1 = (int)std::ceil(s1);
                if (is0 < 0) is0 = 0;
                if (is1 > rawNow) is1 = rawNow;
                float sum = 0.0f; int cnt = 0;
                for (int s = is0; s < is1; s++) { sum += rawFreqBuffer[s]; cnt++; }
                uint8_t g = (cnt > 0) ? freqToGray(sum / (float)cnt) : 0;
                row[x * 3 + 0] = g;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = g;
            }
        }
    }

    void WEFAXDecoder::renderNewLines() {
        const double period = effectiveLinePeriod();
        if (period < 1.0) return;
        const double origin = effectiveLineOrigin();
        const double spp    = period / (double)width;
        size_t rawSz; { std::lock_guard<std::mutex> rlk(rawMutex); rawSz = rawFreqBuffer.size(); }
        int avail = (int)std::floor(((double)rawSz - origin
                                     + (double)hShiftPixels * spp) / period);
        if (avail > WEFAX_MAX_LINES) avail = WEFAX_MAX_LINES;
        if (avail <= lastRenderedLine) return;

        const int firstNew = lastRenderedLine;
        renderLineRange(lastRenderedLine, avail - 1);
        lastRenderedLine = avail;
        linesReceived = avail;
        // Median-filter only the newly rendered lines (plus one line of overlap
        // so the previous last line gets a real bottom neighbour). This keeps
        // the imageMutex hold time tiny, so the UI thread's texture upload no
        // longer stalls and the waterfall stays smooth.
        if (medianFilter) applyMedianFilterRange(firstNew - 1, avail - 1);
        if (lineCallback) lineCallback(linesReceived);
    }

    void WEFAXDecoder::renderSyncIfIdle() {
        // Re-render the whole image from the raw buffer with the CURRENT slant /
        // shift / median settings. Thread-safe against the worker thread via
        // rawMutex + imageMutex, so it can run on the UI thread in any state --
        // including while receiving or after the signal has stopped. This makes
        // the saved image and the manual re-render always match the preview.
        reRenderRequested = false;
        renderAll();
    }

    void WEFAXDecoder::renderAll() {
        const double period = effectiveLinePeriod();
        if (period < 1.0) return;
        const double origin = effectiveLineOrigin();
        const double spp    = period / (double)width;
        size_t rawSz; { std::lock_guard<std::mutex> rlk(rawMutex); rawSz = rawFreqBuffer.size(); }
        int avail = (int)std::floor(((double)rawSz - origin
                                     + (double)hShiftPixels * spp) / period);
        if (avail < 0) avail = 0;
        if (avail > WEFAX_MAX_LINES) avail = WEFAX_MAX_LINES;

        {
            std::lock_guard<std::mutex> lck(imageMutex);
            std::fill(imageBuffer.begin(), imageBuffer.end(), 0);
        }
        if (avail > 0) renderLineRange(0, avail - 1);
        lastRenderedLine = avail;
        linesReceived = avail;
        if (medianFilter) applyMedianFilter();
        if (lineCallback) lineCallback(linesReceived);
    }

    void WEFAXDecoder::applyMedianFilter() {
        // Full-image median (used on a full re-render). Delegates to the range
        // version so the logic lives in one place.
        applyMedianFilterRange(0, (linesReceived > 0 ? linesReceived - 1 : 0));
    }

    void WEFAXDecoder::applyMedianFilterRange(int firstLine, int lastLine) {
        const int W = width;
        const int H = (linesReceived > 0) ? linesReceived : 0;
        if (H <= 0) return;
        if (firstLine < 0) firstLine = 0;
        if (lastLine > H - 1) lastLine = H - 1;
        if (firstLine > lastLine) return;
        std::lock_guard<std::mutex> lck(imageMutex);
        if (imageBuffer.size() < (size_t)W * H * 3) return;
        if (displayBuffer.size() != imageBuffer.size())
            displayBuffer.assign(imageBuffer.size(), 0);

        // Filter only [firstLine, lastLine] from the CLEAN imageBuffer into
        // displayBuffer. 3x3 median; neighbours clamped at the edges. Holding
        // the lock only for the few new lines keeps the UI (and waterfall)
        // responsive instead of stalling on a whole-image pass each batch.
        uint8_t window[9];
        for (int y = firstLine; y <= lastLine; y++) {
            for (int x = 0; x < W; x++) {
                int k = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    int yy = std::clamp(y + dy, 0, H - 1);
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = std::clamp(x + dx, 0, W - 1);
                        window[k++] = imageBuffer[((size_t)yy * W + xx) * 3];
                    }
                }
                std::sort(window, window + 9);
                uint8_t g = window[4];
                size_t o = (size_t)(y * W + x) * 3;
                displayBuffer[o] = displayBuffer[o + 1] = displayBuffer[o + 2] = g;
            }
        }
    }

    // ------------------------------------------------------------------
    // Band-view histogram of the instantaneous frequency. Light: one bin
    // increment per sample, normalized + smoothed once per window.
    // ------------------------------------------------------------------
    void WEFAXDecoder::accumulateSpectrum(float f) {
        if (f >= BAND_FLO && f < BAND_FHI) {
            int b = (int)((f - BAND_FLO) / (BAND_FHI - BAND_FLO) * SPEC_BINS);
            if (b >= 0 && b < SPEC_BINS) specAccum[b] += 1.0f;
        }
        if (++specCount >= specWindow) {
            // Exponential moving average of the raw per-window counts. Keeping
            // raw magnitudes (normalizing only at read time) preserves the true
            // relative energy between bins and avoids the bias a per-window
            // max-normalize introduces for sweeping signals.
            std::lock_guard<std::mutex> lck(specMtx);
            for (int i = 0; i < SPEC_BINS; i++) {
                specMag[i] = 0.8f * specMag[i] + 0.2f * specAccum[i];
                specAccum[i] = 0.0f;
            }
            specCount = 0;
        }
    }

    int WEFAXDecoder::getBandSpectrum(float* out, int n) const {
        if (!out || n <= 0) return 0;
        int c = (n < SPEC_BINS) ? n : SPEC_BINS;
        std::lock_guard<std::mutex> lck(specMtx);
        float mx = 1e-6f;
        for (int i = 0; i < SPEC_BINS; i++) if (specMag[i] > mx) mx = specMag[i];
        for (int i = 0; i < c; i++) out[i] = specMag[i] / mx;   // 0..1 at display
        return c;
    }

} // namespace wefax
