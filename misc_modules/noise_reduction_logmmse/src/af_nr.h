#pragma once
#include <dsp/block.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <dsp/processor.h>
#include <utils/flog.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include "omlsa_mcra.h"

namespace dsp {

    // OMLSA-MCRA speech denoiser ("Audio NR2"), inserted into the radio's
    // stereo AF chain. Processes the left channel in fixed-point blocks and
    // outputs mono (L copied to R). Passthrough while `allowed` is off, while
    // accumulating less than one OMLSA block, or after a fatal init failure.
    struct AFNR_OMLSA_MCRA : public Processor<stereo_t, stereo_t> {
        using base_type = Processor<stereo_t, stereo_t>;
        bool failed = false;
        dsp::omlsa_mcra omlsa_mcra;
        std::vector<stereo_t> buffer;
        bool allowed = false;
        bool allowed2 = true;       // just convenient for various conditions

        void init(stream<stereo_t>* in) override {
            base_type::init(in);
        }

        void setInput(stream<stereo_t>* in) override {
            base_type::setInput(in);
        }

        void start() override {
            block::start();
        }

        void stop() override {
            block::stop();
        }

        // Audio rate of the AF chain this block sits in. Written by the UI
        // thread (from SinkManager), applied lazily in the DSP thread so the
        // OMLSA state is never re-initialized while a block is processed.
        std::atomic<int> targetSampleRate { 48000 };
        int appliedSampleRate = 0;

        void setAudioSampleRate(int sampleRate) {
            if (sampleRate > 0) { targetSampleRate = sampleRate; }
        }

        float scaled = 32767.0;      // amplitude shaper

        void process(stereo_t *readBuf, int count, stereo_t *writeBuf, int &wrote) {
            if (!allowed || !allowed2 || failed) {
                std::copy(readBuf, readBuf+count, writeBuf);
                wrote = count;
                buffer.clear();
                return;
            } else {
                int rate = targetSampleRate.load(std::memory_order_relaxed);
                if (rate != appliedSampleRate) {
                    omlsa_mcra.setSampleRate(rate);
                    appliedSampleRate = rate;
                    buffer.clear();
                }
                buffer.reserve(buffer.size() + count);
                buffer.insert(buffer.end(), readBuf, readBuf + count);
                int blockSize = omlsa_mcra.blockSize();
                if (buffer.size() >= blockSize) {
                    double max = 0;
                    std::vector<short> processIn(blockSize, 0);
                    std::vector<short> processOut(3 * blockSize, 0);
                    if (scaled < 32757) {
                        scaled += 10;
                    }
                    for(int q=0; q<blockSize; q++) {
                        if (fabs(buffer[q].l) > max) {
                            max = fabs(buffer[q].l);
                        }
                        processIn[q] = buffer[q].l * scaled;
                    }
                    bool processedOk = true;
                    if (max > 32767/scaled) {
                        float newScaled = 32767 / max;
                        for (int q = 0; q < blockSize; q++) {
                            processIn[q] = buffer[q].l * newScaled;
                        }
                        scaled = newScaled;
                    }
                    processedOk = omlsa_mcra.process((short*)processIn.data(), blockSize, (short*)processOut.data(), wrote);
                    if (!processedOk) {
                        // Init/alloc failure while preparing the in-memory OM-LSA gain
                        // table - not transient, so disable permanently instead of
                        // re-initializing and failing on every block.
                        flog::error("OMLSA processing failed, disabling Audio NR2");
                        failed = true;
                        std::copy(buffer.begin(), buffer.end(), writeBuf);
                        wrote = buffer.size();
                        buffer.clear();
                    }
                    else {
                        buffer.erase(buffer.begin(), buffer.begin() + blockSize);
                        for(int q=0; q<wrote; q++) {
                            writeBuf[q].r = writeBuf[q].l = processOut[q] / scaled;
                        }
                    }
                }
                else {
                    wrote = 0;
                }
            }
        }

        int run() override {
            int count = _in->read();
            if (count < 0) { return -1; }
            int wrote;
            process(_in->readBuf, count, out.writeBuf, wrote);
            _in->flush();
            if (!out.swap(wrote)) {
                return 0;
            }
            return 1;
        }
    };

}
