#pragma once
#include <string>
#include <utils/flac_reader.h>
#include "sample_reader.h"

// Adapts core's flac::FlacReader to the SampleReader interface. The core
// decoder already hands back WAV-style packed little-endian PCM, so this
// presents the stream as WAVE_FORMAT::PCM: main.cpp's existing PCM converters
// (convU8/convI16/convI24/convI32) then handle it with no FLAC-specific code.
class FlacReader : public SampleReader {
public:
    FlacReader(const std::string& path) : _dec(path) {}

    bool isValid() override { return _dec.isValid(); }

    WAVE_FORMAT getFormat() override { return WAVE_FORMAT::PCM; }
    const char* getFormatName() override { return "FLAC"; }
    uint16_t getBitDepth() override { return _dec.getBitDepth(); }
    uint16_t getChannelCount() override { return _dec.getChannelCount(); }
    uint16_t getBlockAlign() override { return _dec.getChannelCount() * ((_dec.getBitDepth() + 7) / 8); }
    uint32_t getSampleRate() override { return _dec.getSampleRate(); }
    uint64_t getSampleCount() override { return _dec.getSampleCount(); }

    uint64_t getSamplePosition() override { return _dec.getSamplePosition(); }
    void seek(uint64_t sampleNumber) override { _dec.seek(sampleNumber); }
    void rewind() override { _dec.rewind(); }
    size_t readSamples(void* data, size_t size) override { return _dec.readSamples(data, size); }
    void close() override { _dec.close(); }

private:
    flac::FlacReader _dec;
};
