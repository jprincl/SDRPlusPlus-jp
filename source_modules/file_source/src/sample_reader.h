#pragma once
#include <stdint.h>
#include <stddef.h>

enum class WAVE_FORMAT : uint16_t {
    PCM = 1,
    ADPCM = 2,
    IEEE_FLOAT = 3,
    ALAW = 6,             // 8-bit ITU-T G.711 A-law
    MULAW = 7,            // 8-bit ITU-T G.711 u-law
    EXTENSIBLE = 0xFFFE,  // Actual format is in the SubFormat GUID
};

// Common interface over the file readers file_source can play (WAV/RF64 and
// FLAC). getFormat()/getBitDepth() drive the byte->float conversion in main.cpp,
// so every reader exposes its samples as packed little-endian PCM (or float for
// WAV IEEE_FLOAT) laid out getBlockAlign() bytes per frame.
class SampleReader {
public:
    virtual ~SampleReader() {}

    virtual bool isValid() = 0;

    virtual WAVE_FORMAT getFormat() = 0;
    virtual const char* getFormatName() = 0;
    virtual uint16_t getBitDepth() = 0;
    virtual uint16_t getChannelCount() = 0;
    virtual uint16_t getBlockAlign() = 0;
    virtual uint32_t getSampleRate() = 0;
    virtual uint64_t getSampleCount() = 0;

    virtual uint64_t getSamplePosition() = 0;
    virtual void seek(uint64_t sampleNumber) = 0;
    virtual void rewind() = 0;
    virtual size_t readSamples(void* data, size_t size) = 0;
    virtual void close() = 0;
};
