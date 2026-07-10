#pragma once
#include <utils/networking.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <server_protocol.h>

namespace server {
    void setInput(dsp::stream<dsp::complex_t>* stream);
    int main();

    void setInputSampleRate(double samplerate);
    // Called by SourceManager (native/hardware domain). Cached, pushed to the
    // connected client and re-sent to any client connecting later.
    void setTuningLimits(bool enabled, double minFreq, double maxFreq);
}
