#include "wav.h"
#include <volk/volk.h>
#include <stdexcept>
#include <dsp/buffer/buffer.h>
#include <dsp/stream.h>
#include <utils/flog.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <FLAC/stream_encoder.h>

namespace wav {
    const char* WAVE_FILE_TYPE          = "WAVE";
    const char* FORMAT_MARKER           = "fmt ";
    const char* DATA_MARKER             = "data";
    const uint32_t FORMAT_HEADER_LEN    = 16;
    const uint16_t SAMPLE_TYPE_PCM      = 1;

    std::map<SampleType, int> SAMP_BITS = {
        { SAMP_TYPE_UINT8, 8 },
        { SAMP_TYPE_INT16, 16 },
        { SAMP_TYPE_INT24, 24 },
        { SAMP_TYPE_INT32, 32 },
        { SAMP_TYPE_FLOAT32, 32 }
    };

    Writer::Writer(int channels, uint64_t samplerate, Format format, SampleType type) {
        // Validate channels and samplerate
        if (channels < 1) { throw std::runtime_error("Channel count must be greater or equal to 1"); }
        if (!samplerate) { throw std::runtime_error("Samplerate must be non-zero"); }

        // Initialize variables
        _channels = channels;
        _samplerate = samplerate;
        _format = format;
        _type = type;
    }

    Writer::~Writer() { close(); }

    bool Writer::open(std::string path) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Close previous file
        if (isOpenInt()) { close(); }

        // Reset work values
        samplesWritten = 0;
        writeErrorLogged = false;
        auto bitsIt = SAMP_BITS.find(_type);
        if (bitsIt == SAMP_BITS.end()) {
            flog::error("Unknown sample type: {}", (int)_type);
            return false;
        }
        int bits = bitsIt->second;
        bytesPerSamp = (bits / 8) * _channels;

        // Symmetric full-scale factor (2^(bits-1) - 1), the same convention as
        // the volk int16/int32 conversions below: 0 maps to 0 (no DC offset on
        // silence) and clamped +/-1.0 input can't overflow the integer range,
        // at the cost of the deepest negative code staying unused. Computed in
        // double so the scale and products are exact for depths up to 32 bits.
        intScale = (double)((1ull << (bits - 1)) - 1);

        if (_format == FORMAT_FLAC) {
            // The FLAC codec takes integer samples only
            if (_type == SAMP_TYPE_FLOAT32) {
                flog::error("FLAC container requires an integer sample type");
                return false;
            }

            // Create and configure the encoder
            FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
            if (!enc) {
                flog::error("FLAC__stream_encoder_new() failed");
                return false;
            }
            FLAC__stream_encoder_set_channels(enc, _channels);
            FLAC__stream_encoder_set_sample_rate(enc, (uint32_t)_samplerate);
            FLAC__stream_encoder_set_bits_per_sample(enc, bits);
            FLAC__stream_encoder_set_compression_level(enc, 5);

            // Open the output file. Fails (among others) for samplerates above
            // FLAC__MAX_SAMPLE_RATE (1048575 Hz; 655350 Hz on libFLAC < 1.4)
            // and for 32-bit samples on libFLAC < 1.4.
            auto status = FLAC__stream_encoder_init_file(enc, path.c_str(), NULL, NULL);
            if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
                flog::error("FLAC__stream_encoder_init_file() failed: {}", FLAC__StreamEncoderInitStatusString[status]);
                FLAC__stream_encoder_delete(enc);
                return false;
            }

            // Conversion buffer (the encoder takes sign-extended int32 at every depth)
            bufI32 = dsp::buffer::alloc<int32_t>(STREAM_BUFFER_SIZE * _channels);

            flacEnc = enc;
            return true;
        }

        // Fill header
        hdr.codec = (_type == SAMP_TYPE_FLOAT32) ? CODEC_FLOAT : CODEC_PCM;
        hdr.channelCount = _channels;
        hdr.sampleRate = _samplerate;
        hdr.bitDepth = bits;
        hdr.bytesPerSample = bytesPerSamp;
        hdr.bytesPerSecond = bytesPerSamp * _samplerate;

        // Precompute sizes and allocate buffers
        switch (_type) {
        case SAMP_TYPE_UINT8:
            bufU8 = dsp::buffer::alloc<uint8_t>(STREAM_BUFFER_SIZE * _channels);
            break;
        case SAMP_TYPE_INT16:
            bufI16 = dsp::buffer::alloc<int16_t>(STREAM_BUFFER_SIZE * _channels);
            break;
        case SAMP_TYPE_INT24:
            bufI24 = dsp::buffer::alloc<uint8_t>(STREAM_BUFFER_SIZE * _channels * 3);
            break;
        case SAMP_TYPE_INT32:
            bufI32 = dsp::buffer::alloc<int32_t>(STREAM_BUFFER_SIZE * _channels);
            break;
        case SAMP_TYPE_FLOAT32:
            break;
        default:
            return false;
            break;
        }

        // Open file
        if (!rw.open(path, WAVE_FILE_TYPE)) {
            freeBuffers();
            return false;
        }

        // Write format chunk
        rw.beginChunk(FORMAT_MARKER);
        rw.write((uint8_t*)&hdr, sizeof(FormatHeader));
        rw.endChunk();

        // Begin data chunk
        rw.beginChunk(DATA_MARKER);

        return true;
    }

    bool Writer::isOpenInt() {
        return flacEnc || rw.isOpen();
    }

    bool Writer::isOpen() {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        return isOpenInt();
    }

    void Writer::close() {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do nothing if the file is not open
        if (!isOpenInt()) { return; }

        if (flacEnc) {
            // Flush the encoder and finalize the file (rewrites STREAMINFO)
            FLAC__StreamEncoder* enc = (FLAC__StreamEncoder*)flacEnc;
            if (!FLAC__stream_encoder_finish(enc)) {
                flog::error("FLAC__stream_encoder_finish() failed");
            }
            FLAC__stream_encoder_delete(enc);
            flacEnc = NULL;
        }
        else {
            // Finish data chunk
            rw.endChunk();

            // Close the file
            rw.close();
        }

        // Free buffers
        freeBuffers();
    }

    void Writer::freeBuffers() {
        if (bufU8) {
            dsp::buffer::free(bufU8);
            bufU8 = NULL;
        }
        if (bufI16) {
            dsp::buffer::free(bufI16);
            bufI16 = NULL;
        }
        if (bufI24) {
            dsp::buffer::free(bufI24);
            bufI24 = NULL;
        }
        if (bufI32) {
            dsp::buffer::free(bufI32);
            bufI32 = NULL;
        }
    }

    void Writer::setChannels(int channels) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }

        // Validate channel count
        if (channels < 1) { throw std::runtime_error("Channel count must be greater or equal to 1"); }
        _channels = channels;
    }

    void Writer::setSamplerate(uint64_t samplerate) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }

        // Validate samplerate
        if (!samplerate) { throw std::runtime_error("Samplerate must be non-zero"); }
        _samplerate = samplerate;
    }

    void Writer::setFormat(Format format) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }
        _format = format;
    }

    void Writer::setSampleType(SampleType type) {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        // Do not allow settings to change while open
        if (isOpenInt()) { throw std::runtime_error("Cannot change parameters while file is open"); }
        _type = type;
    }

    std::string Writer::getFileExtension() {
        std::lock_guard<std::recursive_mutex> lck(mtx);
        switch (_format) {
        case FORMAT_FLAC: return ".flac";
        default:          return ".wav";
        }
    }

    void Writer::write(float* samples, int count) {
        std::lock_guard<std::recursive_mutex> lck(mtx);

        int tcount = count * _channels;
        int tbytes = count * bytesPerSamp;

        if (flacEnc) {
            for (int i = 0; i < tcount; i++) {
                bufI32[i] = (int32_t)llround((double)std::clamp<float>(samples[i], -1.0f, 1.0f) * intScale);
            }
            FLAC__StreamEncoder* enc = (FLAC__StreamEncoder*)flacEnc;
            if (!FLAC__stream_encoder_process_interleaved(enc, bufI32, count)) {
                // Log the first failure only (this runs per DSP block)
                if (!writeErrorLogged) {
                    auto state = FLAC__stream_encoder_get_state(enc);
                    flog::error("FLAC__stream_encoder_process_interleaved() failed: {}", FLAC__StreamEncoderStateString[state]);
                    writeErrorLogged = true;
                }
                return;
            }
            samplesWritten += count;
            return;
        }

        if (!rw.isOpen()) { return; }

        // Select different writer function depending on the chose depth
        switch (_type) {
        case SAMP_TYPE_UINT8:
            // Volk doesn't support unsigned ints yet :/
            for (int i = 0; i < tcount; i++) {
                bufU8[i] = (samples[i] * 127.0f) + 128.0f;
            }
            rw.write(bufU8, tbytes);
            break;
        case SAMP_TYPE_INT16:
            volk_32f_s32f_convert_16i(bufI16, samples, 32767.0f, tcount);
            rw.write((uint8_t*)bufI16, tbytes);
            break;
        case SAMP_TYPE_INT24:
        {
            // Little-endian 3-byte packing
            uint8_t* dst = bufI24;
            for (int i = 0; i < tcount; i++) {
                int32_t sval = (int32_t)llround((double)std::clamp<float>(samples[i], -1.0f, 1.0f) * intScale);
                *(dst++) = (uint8_t)sval;
                *(dst++) = (uint8_t)(sval >> 8);
                *(dst++) = (uint8_t)(sval >> 16);
            }
            rw.write(bufI24, tbytes);
            break;
        }
        case SAMP_TYPE_INT32:
            volk_32f_s32f_convert_32i(bufI32, samples, 2147483647.0f, tcount);
            rw.write((uint8_t*)bufI32, tbytes);
            break;
        case SAMP_TYPE_FLOAT32:
            rw.write((uint8_t*)samples, tbytes);
            break;
        default:
            break;
        }

        // Increment sample counter
        samplesWritten += count;
    }
}
