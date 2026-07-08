#include "flac_reader.h"
#include <utils/flog.h>
#include <FLAC/stream_decoder.h>
#include <algorithm>
#include <mutex>
#include <vector>
#include <cstring>

namespace flac {
    // All decode state lives here so the public header stays free of FLAC types.
    // The libFLAC callbacks run synchronously inside process_single()/seek(),
    // which are only ever called while mtx is held, so they touch this struct
    // on the same thread that holds the lock.
    struct FlacReaderImpl {
        FLAC__StreamDecoder* dec = NULL;
        std::recursive_mutex mtx;

        bool valid = false;
        bool gotStreamInfo = false;
        bool eos = false;
        bool errLogged = false;

        uint32_t sampleRate = 0;
        uint16_t channels = 0;
        uint16_t bitDepth = 0;
        uint16_t bytesPerSample = 0;   // per channel
        uint64_t totalSamples = 0;
        uint64_t samplePos = 0;        // samples/channel already handed to the caller

        // Decoded-but-not-yet-returned PCM bytes. readSamples() drains from the
        // front via fifoRead, then compacts the consumed prefix out before it
        // returns, so fifoRead is 0 between calls.
        std::vector<uint8_t> fifo;
        size_t fifoRead = 0;

        size_t fifoAvail() const { return fifo.size() - fifoRead; }
    };

    // Append one interleaved sample as WAV-style little-endian PCM. FLAC samples
    // are always signed; WAV 8-bit is unsigned, so bias 8-bit by 128 and store
    // 16/24/32-bit as signed LE — byte-for-byte what a WAV PCM file would hold.
    static void pushSample(FlacReaderImpl* d, int32_t s) {
        switch (d->bytesPerSample) {
        case 1:
            d->fifo.push_back((uint8_t)(s + 128));
            break;
        case 2:
            d->fifo.push_back((uint8_t)s);
            d->fifo.push_back((uint8_t)(s >> 8));
            break;
        case 3:
            d->fifo.push_back((uint8_t)s);
            d->fifo.push_back((uint8_t)(s >> 8));
            d->fifo.push_back((uint8_t)(s >> 16));
            break;
        default: // 4
            d->fifo.push_back((uint8_t)s);
            d->fifo.push_back((uint8_t)(s >> 8));
            d->fifo.push_back((uint8_t)(s >> 16));
            d->fifo.push_back((uint8_t)(s >> 24));
            break;
        }
    }

    static FLAC__StreamDecoderWriteStatus writeCb(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                                  const FLAC__int32* const buffer[], void* client) {
        FlacReaderImpl* d = (FlacReaderImpl*)client;
        unsigned blockSize = frame->header.blocksize;
        // STREAMINFO has been parsed before any audio frame is delivered, so the
        // channel count is known; guard anyway against a malformed stream.
        unsigned ch = d->channels;
        if (frame->header.channels < ch) { ch = frame->header.channels; }
        d->fifo.reserve(d->fifo.size() + (size_t)blockSize * d->channels * d->bytesPerSample);
        for (unsigned i = 0; i < blockSize; i++) {
            for (unsigned c = 0; c < d->channels; c++) {
                pushSample(d, (c < ch) ? buffer[c][i] : 0);
            }
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    static void metadataCb(const FLAC__StreamDecoder*, const FLAC__StreamMetadata* meta, void* client) {
        FlacReaderImpl* d = (FlacReaderImpl*)client;
        if (meta->type != FLAC__METADATA_TYPE_STREAMINFO) { return; }
        const FLAC__StreamMetadata_StreamInfo& si = meta->data.stream_info;
        d->sampleRate = si.sample_rate;
        d->channels = (uint16_t)si.channels;
        d->bitDepth = (uint16_t)si.bits_per_sample;
        d->bytesPerSample = (uint16_t)((si.bits_per_sample + 7) / 8);
        d->totalSamples = si.total_samples;
        d->gotStreamInfo = true;
    }

    static void errorCb(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus status, void* client) {
        FlacReaderImpl* d = (FlacReaderImpl*)client;
        // The decoder resynchronizes after a recoverable frame error; log the
        // first only so a corrupt block does not spam the DSP thread.
        if (!d->errLogged) {
            flog::error("FlacReader: decode error: {}", FLAC__StreamDecoderErrorStatusString[status]);
            d->errLogged = true;
        }
    }

    FlacReader::FlacReader(const std::string& path) {
        FlacReaderImpl* d = new FlacReaderImpl();
        impl = d;

        d->dec = FLAC__stream_decoder_new();
        if (!d->dec) {
            flog::error("FLAC__stream_decoder_new() failed");
            return;
        }
        FLAC__stream_decoder_set_md5_checking(d->dec, false);

        auto st = FLAC__stream_decoder_init_file(d->dec, path.c_str(), writeCb, metadataCb, errorCb, d);
        if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            flog::error("FLAC__stream_decoder_init_file() failed: {}", FLAC__StreamDecoderInitStatusString[st]);
            return;
        }

        // Parse metadata up to (but not into) the first audio frame; fires metadataCb.
        if (!FLAC__stream_decoder_process_until_end_of_metadata(d->dec) || !d->gotStreamInfo) {
            flog::error("FlacReader: no STREAMINFO in '{}'", path);
            return;
        }

        // The recorder finalizes STREAMINFO on close (total sample count rewritten);
        // a zero here means the file was never finalized, whose length can only be
        // found by decoding it whole. Reject rather than mis-report the duration.
        if (d->totalSamples == 0) {
            flog::error("FlacReader: '{}' has no total sample count (unfinalized recording)", path);
            return;
        }
        if (!d->sampleRate || !d->channels ||
            d->bitDepth < 8 || d->bitDepth > 32) {
            flog::error("FlacReader: unsupported stream ({} Hz, {} ch, {} bit)",
                        d->sampleRate, d->channels, d->bitDepth);
            return;
        }

        d->valid = true;
    }

    FlacReader::~FlacReader() {
        close();
        delete (FlacReaderImpl*)impl;
    }

    bool FlacReader::isValid() {
        FlacReaderImpl* d = (FlacReaderImpl*)impl;
        std::lock_guard<std::recursive_mutex> lck(d->mtx);
        return d->valid;
    }

    uint32_t FlacReader::getSampleRate() { return ((FlacReaderImpl*)impl)->sampleRate; }
    uint16_t FlacReader::getChannelCount() { return ((FlacReaderImpl*)impl)->channels; }
    uint16_t FlacReader::getBitDepth() { return ((FlacReaderImpl*)impl)->bitDepth; }
    uint64_t FlacReader::getSampleCount() { return ((FlacReaderImpl*)impl)->totalSamples; }

    uint64_t FlacReader::getSamplePosition() {
        FlacReaderImpl* d = (FlacReaderImpl*)impl;
        std::lock_guard<std::recursive_mutex> lck(d->mtx);
        return d->samplePos;
    }

    void FlacReader::seek(uint64_t sampleNumber) {
        FlacReaderImpl* d = (FlacReaderImpl*)impl;
        std::lock_guard<std::recursive_mutex> lck(d->mtx);
        if (!d->valid) { return; }
        // The last seekable sample is totalSamples-1; libFLAC rejects a seek to
        // the sample count itself (one past the end). totalSamples is non-zero
        // here (finalized streams only), but guard the subtraction anyway.
        if (sampleNumber >= d->totalSamples) {
            sampleNumber = d->totalSamples ? d->totalSamples - 1 : 0;
        }

        // Drop pending decoded audio. libFLAC resumes decoding at sampleNumber;
        // file_source only ever seeks to 0 (loop rewind), which is always a frame
        // boundary, so the frame-granular nature of general seeks is moot here.
        d->fifo.clear();
        d->fifoRead = 0;
        d->eos = false;

        if (FLAC__stream_decoder_seek_absolute(d->dec, sampleNumber)) {
            // Only trust the new position once the decoder actually reached it.
            d->samplePos = sampleNumber;
        }
        else {
            flog::error("FLAC__stream_decoder_seek_absolute({}) failed", (unsigned long long)sampleNumber);
            // Recover the decoder from the seek-error state so playback can
            // continue; sampleNumber is a valid target, so keep it as the best
            // available estimate of where decoding resumes.
            FLAC__stream_decoder_flush(d->dec);
            d->samplePos = sampleNumber;
        }
    }

    void FlacReader::rewind() { seek(0); }

    size_t FlacReader::readSamples(void* data, size_t size) {
        FlacReaderImpl* d = (FlacReaderImpl*)impl;
        std::lock_guard<std::recursive_mutex> lck(d->mtx);
        if (!d->valid) { return 0; }

        // Pull-decode frames until the FIFO can satisfy the request or the stream ends.
        while (d->fifoAvail() < size) {
            FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(d->dec);
            if (state == FLAC__STREAM_DECODER_END_OF_STREAM) { d->eos = true; break; }
            if (state == FLAC__STREAM_DECODER_ABORTED) { break; }
            if (!FLAC__stream_decoder_process_single(d->dec)) { break; }
        }

        size_t n = std::min(size, d->fifoAvail());
        if (n) {
            memcpy(data, d->fifo.data() + d->fifoRead, n);
            d->fifoRead += n;
            size_t bytesPerFrame = (size_t)d->bytesPerSample * d->channels;
            if (bytesPerFrame) { d->samplePos += n / bytesPerFrame; }
        }
        // Drop the consumed prefix every call: writeCb keeps appending to the
        // back, so leaving it in place would grow the buffer without bound. The
        // surviving tail is at most a frame, so the compaction is cheap.
        if (d->fifoRead) {
            d->fifo.erase(d->fifo.begin(), d->fifo.begin() + d->fifoRead);
            d->fifoRead = 0;
        }
        return n;
    }

    void FlacReader::close() {
        FlacReaderImpl* d = (FlacReaderImpl*)impl;
        std::lock_guard<std::recursive_mutex> lck(d->mtx);
        if (d->dec) {
            FLAC__stream_decoder_finish(d->dec);
            FLAC__stream_decoder_delete(d->dec);
            d->dec = NULL;
        }
        d->valid = false;
        d->fifo.clear();
        d->fifoRead = 0;
    }
}
