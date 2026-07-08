#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

namespace flac {
    // Streaming decoder for native .flac files (the recorder's FLAC container).
    //
    // Decoded audio is presented as WAV-identical packed little-endian PCM:
    // unsigned 8-bit, signed 16/24/32-bit. A file_source consumer can therefore
    // feed the bytes straight through the same per-format converters it uses for
    // a WAV PCM file, with no FLAC-specific handling.
    //
    // libFLAC stays a PRIVATE implementation detail of sdrpp_core: the decoder
    // handle and all decode state live in an opaque impl struct defined in the
    // .cpp, so <FLAC/stream_decoder.h> never leaks into this header (same
    // reasoning as the encoder held opaquely by wav::Writer).
    //
    // Only finalized streams are accepted: isValid() is false when STREAMINFO
    // carries no total sample count (an unfinalized recording), because a
    // compressed stream's length cannot be recovered without decoding it whole.
    class FlacReader {
    public:
        // Opens and reads the STREAMINFO metadata. Never throws; check isValid().
        FlacReader(const std::string& path);
        ~FlacReader();

        FlacReader(const FlacReader&) = delete;
        FlacReader& operator=(const FlacReader&) = delete;

        // True once the file opened and a STREAMINFO with a usable sample rate,
        // channel count and (finalized) total sample count was found.
        bool isValid();

        uint32_t getSampleRate();
        uint16_t getChannelCount();
        uint16_t getBitDepth();
        uint64_t getSampleCount();

        // Number of samples (per channel) already returned by readSamples().
        uint64_t getSamplePosition();

        // Frame-granular seek. Clamped to [0, getSampleCount()].
        void seek(uint64_t sampleNumber);
        void rewind();

        // Fills up to size bytes of interleaved WAV-style PCM and returns the
        // number of bytes written. A short read (including 0) means end of file.
        size_t readSamples(void* data, size_t size);

        void close();

    private:
        void* impl;
    };
}
