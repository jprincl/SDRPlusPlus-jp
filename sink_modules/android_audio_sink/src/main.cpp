#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <dsp/buffer/packer.h>
#include <utils/flog.h>
#include <config.h>
#include <utils/optionlist.h>
#include <android_backend.h>
#include <core.h>

#include <android/api-level.h>
#include <oboe/Oboe.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

SDRPP_MOD_INFO{
    /* Name:            */ "audio_sink",
    /* Description:     */ "Android audio sink module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

static_assert(sizeof(dsp::stereo_t) == sizeof(float) * 2, "AudioSink expects packed stereo float frames");

class AudioSink : SinkManager::Sink,
                  private oboe::AudioStreamDataCallback,
                  private oboe::AudioStreamErrorCallback {
public:
    AudioSink(SinkManager::Stream* stream, std::string streamName) {
        _stream = stream;
        _streamName = std::move(streamName);

        packer.init(_stream->sinkOut, 512);
        sampleRate = 48000;
        _stream->setSampleRate(sampleRate);
        audioFifo.resize(kAudioFifoCapacity);
    }

    ~AudioSink() {
        stop();
    }

    void start() {
        std::lock_guard<std::mutex> lck(stateMtx);
        if (running) { return; }
        running = openAndStartStreamLocked();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lck(stateMtx);
            stopLocked(true);
        }
        joinRestartThread();
    }

    void menuHandler() {
        // Draw menu here
    }

private:
    static constexpr size_t kAudioFifoCapacity = 32769;

    bool openAndStartStreamLocked() {
        const oboe::AudioApi preferredApi = preferredAudioApi();
        if (tryStartStreamLocked(preferredApi)) {
            return true;
        }

        if (preferredApi != oboe::AudioApi::OpenSLES) {
            flog::warn("AudioSink: preferred backend failed, retrying with OpenSL ES");
            return tryStartStreamLocked(oboe::AudioApi::OpenSLES);
        }

        return false;
    }

    bool tryStartStreamLocked(oboe::AudioApi audioApi) {
        bufferSize = std::max<int>(1, (int)std::lround(sampleRate / 60.0));
        packer.setSampleCount(bufferSize);
        resetAudioFifo();

        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output);
        builder.setSharingMode(oboe::SharingMode::Shared);
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        builder.setUsage(oboe::Usage::Media);
        builder.setFormat(oboe::AudioFormat::Float);
        builder.setChannelCount(2);
        builder.setSampleRate((int32_t)sampleRate);
        builder.setFramesPerCallback(bufferSize);
        builder.setDataCallback(this);
        builder.setErrorCallback(this);
        builder.setAudioApi(audioApi);

        const int preferredOutputDeviceId = backend::getPreferredAudioOutputDeviceId();
        if (preferredOutputDeviceId > 0) {
            builder.setDeviceId(preferredOutputDeviceId);
        }

        std::shared_ptr<oboe::AudioStream> stream;
        const oboe::Result openResult = builder.openStream(stream);
        if (openResult != oboe::Result::OK || !stream) {
            flog::error("AudioSink: failed to open {} stream: {}",
                        audioApiName(audioApi),
                        oboe::convertToText(openResult));
            return false;
        }

        packer.start();
        producerThread = std::thread(&AudioSink::producerLoop, this);
        callbackStream.store(stream.get(), std::memory_order_release);

        const oboe::Result startResult = stream->requestStart();
        if (startResult != oboe::Result::OK) {
            flog::error("AudioSink: failed to start {} stream: {}",
                        audioApiName(stream->getAudioApi()),
                        oboe::convertToText(startResult));
            callbackStream.store(nullptr, std::memory_order_release);
            stopProducerLocked();
            stream->close();
            return false;
        }

        audioStream = std::move(stream);
        if (preferredOutputDeviceId > 0) {
            flog::info("AudioSink: backend = {}, preferred output device requested {}",
                       audioApiName(audioStream->getAudioApi()),
                       preferredOutputDeviceId);
        }
        else {
            flog::info("AudioSink: backend = {}", audioApiName(audioStream->getAudioApi()));
        }
        return true;
    }

    void stopLocked(bool closeStream) {
        if (!running && !audioStream && !producerThread.joinable()) {
            return;
        }

        shuttingDown.store(true, std::memory_order_release);
        callbackStream.store(nullptr, std::memory_order_release);

        std::shared_ptr<oboe::AudioStream> stream = std::move(audioStream);
        stopProducerLocked();
        running = false;

        if (closeStream && stream) {
            const oboe::Result stopResult = stream->requestStop();
            if (stopResult != oboe::Result::OK && stopResult != oboe::Result::ErrorClosed) {
                flog::warn("AudioSink: requestStop failed: {}", oboe::convertToText(stopResult));
            }

            const oboe::Result closeResult = stream->close();
            if (closeResult != oboe::Result::OK && closeResult != oboe::Result::ErrorClosed) {
                flog::warn("AudioSink: close failed: {}", oboe::convertToText(closeResult));
            }
        }

        shuttingDown.store(false, std::memory_order_release);
    }

    void stopProducerLocked() {
        packer.out.stopReader();
        packer.stop();
        if (producerThread.joinable()) { producerThread.join(); }
        packer.out.clearReadStop();
        resetAudioFifo();
    }

    void producerLoop() {
        while (true) {
            int count = packer.out.read();
            if (count < 0) { return; }

            pushAudioFrames(packer.out.readBuf, count);
            packer.out.flush();
        }
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override {
        auto* out = static_cast<dsp::stereo_t*>(audioData);
        if (shuttingDown.load(std::memory_order_acquire) ||
            stream != callbackStream.load(std::memory_order_acquire)) {
            std::memset(out, 0, numFrames * sizeof(dsp::stereo_t));
            return oboe::DataCallbackResult::Stop;
        }

        const int copied = popAudioFrames(out, numFrames);
        if (copied < numFrames) {
            std::memset(out + copied, 0, (numFrames - copied) * sizeof(dsp::stereo_t));
        }

        return oboe::DataCallbackResult::Continue;
    }

    bool onError(oboe::AudioStream*, oboe::Result) override {
        // Let Oboe stop and close the failed stream before we rebuild it.
        return false;
    }

    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override {
        if (shuttingDown.load(std::memory_order_acquire)) { return; }
        if (stream != callbackStream.load(std::memory_order_acquire)) { return; }

        flog::warn("AudioSink: {} stream closed with error {}",
                   audioApiName(stream->getAudioApi()),
                   oboe::convertToText(error));

        if (error == oboe::Result::ErrorDisconnected) {
            scheduleRestart();
        }
    }

    void scheduleRestart() {
        bool expected = false;
        if (!restartPending.compare_exchange_strong(expected, true)) {
            return;
        }

        std::lock_guard<std::mutex> lck(restartThreadMtx);
        if (restartThread.joinable()) {
            restartThread.join();
        }
        restartThread = std::thread(&AudioSink::restartWorker, this);
    }

    void restartWorker() {
        {
            std::lock_guard<std::mutex> lck(stateMtx);
            if (!running) {
                restartPending = false;
                return;
            }

            stopLocked(false);
            running = openAndStartStreamLocked();
            if (!running) {
                flog::error("AudioSink: restart failed, audio unavailable");
            }
        }

        restartPending = false;
    }

    void joinRestartThread() {
        std::lock_guard<std::mutex> lck(restartThreadMtx);
        if (restartThread.joinable() && restartThread.get_id() != std::this_thread::get_id()) {
            restartThread.join();
        }
    }

    oboe::AudioApi preferredAudioApi() const {
        return android_get_device_api_level() <= 27
            ? oboe::AudioApi::OpenSLES
            : oboe::AudioApi::Unspecified;
    }

    static const char* audioApiName(oboe::AudioApi audioApi) {
        switch (audioApi) {
        case oboe::AudioApi::AAudio:
            return "AAudio";
        case oboe::AudioApi::OpenSLES:
            return "OpenSL ES";
        default:
            return "Oboe default";
        }
    }

    static size_t fifoReadable(size_t readIndex, size_t writeIndex) {
        return (writeIndex >= readIndex)
            ? (writeIndex - readIndex)
            : (kAudioFifoCapacity - (readIndex - writeIndex));
    }

    static size_t fifoWritable(size_t readIndex, size_t writeIndex) {
        return kAudioFifoCapacity - fifoReadable(readIndex, writeIndex) - 1;
    }

    void resetAudioFifo() {
        fifoReadIndex.store(0, std::memory_order_release);
        fifoWriteIndex.store(0, std::memory_order_release);
    }

    void pushAudioFrames(const dsp::stereo_t* frames, int frameCount) {
        if (frameCount <= 0) { return; }

        size_t readIndex = fifoReadIndex.load(std::memory_order_acquire);
        const size_t writeIndex = fifoWriteIndex.load(std::memory_order_relaxed);
        const size_t writable = fifoWritable(readIndex, writeIndex);
        const size_t count = (size_t)frameCount;

        if (count > writable) {
            const size_t drop = count - writable;
            readIndex = (readIndex + drop) % kAudioFifoCapacity;
            fifoReadIndex.store(readIndex, std::memory_order_release);
        }

        const size_t first = std::min(count, kAudioFifoCapacity - writeIndex);
        std::memcpy(audioFifo.data() + writeIndex, frames, first * sizeof(dsp::stereo_t));
        if (count > first) {
            std::memcpy(audioFifo.data(), frames + first, (count - first) * sizeof(dsp::stereo_t));
        }

        fifoWriteIndex.store((writeIndex + count) % kAudioFifoCapacity, std::memory_order_release);
    }

    int popAudioFrames(dsp::stereo_t* out, int frameCount) {
        if (frameCount <= 0) { return 0; }

        const size_t readIndex = fifoReadIndex.load(std::memory_order_relaxed);
        const size_t writeIndex = fifoWriteIndex.load(std::memory_order_acquire);
        const size_t available = fifoReadable(readIndex, writeIndex);
        const size_t count = std::min<size_t>((size_t)frameCount, available);
        if (count == 0) { return 0; }

        const size_t first = std::min(count, kAudioFifoCapacity - readIndex);
        std::memcpy(out, audioFifo.data() + readIndex, first * sizeof(dsp::stereo_t));
        if (count > first) {
            std::memcpy(out + first, audioFifo.data(), (count - first) * sizeof(dsp::stereo_t));
        }

        fifoReadIndex.store((readIndex + count) % kAudioFifoCapacity, std::memory_order_release);
        return (int)count;
    }

    std::mutex stateMtx;
    std::thread producerThread;
    std::mutex restartThreadMtx;
    std::thread restartThread;
    std::atomic<bool> restartPending = false;
    std::atomic<bool> shuttingDown = false;
    std::atomic<oboe::AudioStream*> callbackStream{nullptr};
    std::shared_ptr<oboe::AudioStream> audioStream;

    std::atomic<size_t> fifoReadIndex{0};
    std::atomic<size_t> fifoWriteIndex{0};
    std::vector<dsp::stereo_t> audioFifo;

    SinkManager::Stream* _stream = nullptr;
    dsp::buffer::Packer<dsp::stereo_t> packer;
    std::string _streamName;
    double sampleRate = 48000;
    int bufferSize = 0;
    bool running = false;
};

class AudioSinkModule : public ModuleManager::Instance {
public:
    AudioSinkModule(std::string name) {
        this->name = std::move(name);
        provider.create = create_sink;
        provider.ctx = this;

        sigpath::sinkManager.registerSinkProvider("Audio", provider);
    }

    ~AudioSinkModule() {
        sigpath::sinkManager.unregisterSinkProvider("Audio");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static SinkManager::Sink* create_sink(SinkManager::Stream* stream, std::string streamName, void*) {
        return (SinkManager::Sink*)(new AudioSink(stream, std::move(streamName)));
    }

    std::string name;
    bool enabled = true;
    SinkManager::SinkProvider provider;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/audio_sink_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    AudioSinkModule* instance = new AudioSinkModule(std::move(name));
    return instance;
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (AudioSinkModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
