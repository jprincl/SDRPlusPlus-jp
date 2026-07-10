#pragma once
#include "../block.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace dsp::buffer {
    template <class T>
    class Prebuffer : public block {
        using base_type = block;
        using Clock = std::chrono::steady_clock;

    public:
        Prebuffer() = default;
        Prebuffer(stream<T>* in) { init(in); }

        void init() {
            input = nullptr;
            registerOutput(&out);
            _block_init = true;
        }

        void init(stream<T>* in) {
            input = in;
            registerInput(input);
            registerOutput(&out);
            _block_init = true;
        }

        void setInput(stream<T>* in) {
            assert(_block_init);
            std::lock_guard<std::recursive_mutex> lck(ctrlMtx);
            tempStop();
            if (input) { unregisterInput(input); }
            input = in;
            if (input) { registerInput(input); }
            clear();
            tempStart();
        }

        void push(const T* data, int count) {
            if (count <= 0) { return; }
            {
                std::lock_guard lck(bufferMtx);
                pushLocked(data, (size_t)count);
            }
            bufferCnd.notify_all();
        }

        void setPrebufferMsec(int msec) {
            {
                std::lock_guard lck(bufferMtx);
                prebufferMsec = std::max(0, msec);
                trimLocked();
                resizeRingLocked(maxBufferedSamplesLocked());
            }
            bufferCnd.notify_all();
        }

        void setSampleRate(double sr) {
            {
                std::lock_guard lck(bufferMtx);
                sampleRate = std::max(1.0, sr);
                clearLocked();
                resizeRingLocked(maxBufferedSamplesLocked());
            }
            bufferCnd.notify_all();
        }

        void clear() {
            {
                std::lock_guard lck(bufferMtx);
                clearLocked();
            }
            bufferCnd.notify_all();
        }

        int getPercentFull() {
            std::lock_guard lck(bufferMtx);
            size_t target = targetSamplesLocked();
            if (target == 0) { return 100; }
            return (int)std::min<size_t>(100, (bufferedSamples * 100) / target);
        }

        int run() override {
            auto nextTick = Clock::now();

            while (!stopRequested.load()) {
                size_t target = 0;
                size_t slice = 0;
                size_t available = 0;
                bool primedNow = false;
                {
                    std::lock_guard lck(bufferMtx);
                    target = targetSamplesLocked();
                    slice = sliceSamplesLocked();
                    available = bufferedSamples;

                    if (target == 0) {
                        primed = available > 0;
                    }
                    else if (!primed && available >= target) {
                        primed = true;
                        nextTick = Clock::now();
                    }
                    primedNow = primed;
                }

                if (target == 0) {
                    int count = 0;
                    {
                        std::lock_guard lck(bufferMtx);
                        count = (int)popLocked(out.writeBuf, std::min(bufferedSamples, (size_t)STREAM_BUFFER_SIZE));
                    }
                    if (count > 0) {
                        if (!out.swap(count)) { return -1; }
                        continue;
                    }
                    if (waitForProducer(std::chrono::milliseconds(TICK_MS)) < 0) { return -1; }
                    continue;
                }

                if (!primedNow) {
                    if (waitForProducer(std::chrono::milliseconds(TICK_MS)) < 0) { return -1; }
                    continue;
                }

                auto now = Clock::now();
                if (now < nextTick) {
                    if (waitForProducer(std::chrono::duration_cast<std::chrono::milliseconds>(nextTick - now)) < 0) { return -1; }
                    continue;
                }

                int count = 0;
                {
                    std::lock_guard lck(bufferMtx);
                    count = (int)popLocked(out.writeBuf, std::min({ bufferedSamples, slice, (size_t)STREAM_BUFFER_SIZE }));
                    if (bufferedSamples < slice) { primed = false; }
                }
                if (count <= 0) {
                    continue;
                }

                nextTick += sliceDuration(count);
                if (!out.swap(count)) { return -1; }
            }

            return -1;
        }

        stream<T> out;

    private:
        void doStart() override {
            stopRequested = false;
            base_type::doStart();
        }

        void doStop() override {
            {
                std::lock_guard lck(bufferMtx);
                stopRequested = true;
            }
            bufferCnd.notify_all();
            base_type::doStop();
            {
                std::lock_guard lck(bufferMtx);
                stopRequested = false;
                clearLocked();
            }
        }

        int waitForProducer(std::chrono::milliseconds timeout) {
            if (input) {
                int count = input->read_for(timeout);
                if (count < 0) { return -1; }
                if (count > 0) {
                    {
                        std::lock_guard lck(bufferMtx);
                        pushLocked(input->readBuf, (size_t)count);
                    }
                    input->flush();
                    bufferCnd.notify_all();
                }
                return count;
            }

            std::unique_lock lck(bufferMtx);
            bufferCnd.wait_for(lck, timeout, [this]() {
                return stopRequested.load() || bufferedSamples > 0;
            });
            return stopRequested.load() ? -1 : (bufferedSamples > 0 ? 1 : 0);
        }

        std::chrono::steady_clock::duration sliceDuration(size_t samples) const {
            return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(samples / sampleRate));
        }

        size_t targetSamplesLocked() const {
            return (size_t)((sampleRate * (double)prebufferMsec) / 1000.0);
        }

        size_t durationSamplesLocked(int msec) const {
            return std::max<size_t>(1, (size_t)((sampleRate * (double)msec) / 1000.0));
        }

        size_t sliceSamplesLocked() const {
            return durationSamplesLocked(TICK_MS);
        }

        size_t maxBufferedSamplesLocked() const {
            size_t target = targetSamplesLocked();
            size_t slice = sliceSamplesLocked();
            if (target == 0) {
                return std::max<size_t>(4096, slice * 4);
            }

            // Keep a bounded amount of backlog over the requested prebuffer.
            // The slack is expressed in time, not raw samples, so high-rate
            // streams do not silently reserve an extra quarter second.
            size_t burstSlack = std::max(durationSamplesLocked(MIN_SLACK_MS),
                std::min(target / 2, durationSamplesLocked(MAX_SLACK_MS)));
            return std::max<size_t>(slice * 8, target + burstSlack);
        }

        void clearLocked() {
            head = 0;
            bufferedSamples = 0;
            primed = false;
        }

        void resizeRingLocked(size_t capacity) {
            capacity = std::max<size_t>(1, capacity);
            if (ring.size() == capacity) { return; }

            std::vector<T> newRing(capacity);
            size_t copied = popLocked(newRing.data(), std::min(bufferedSamples, capacity), false);
            ring.swap(newRing);
            head = 0;
            bufferedSamples = copied;
        }

        void ensureCapacityLocked(size_t minCapacity) {
            if (ring.size() >= minCapacity) { return; }
            resizeRingLocked(minCapacity);
        }

        void dropOldestLocked(size_t count) {
            if (count >= bufferedSamples) {
                clearLocked();
                return;
            }
            head = (head + count) % ring.size();
            bufferedSamples -= count;
        }

        void trimLocked() {
            size_t maxBuffered = maxBufferedSamplesLocked();
            if (bufferedSamples > maxBuffered) {
                dropOldestLocked(bufferedSamples - maxBuffered);
            }
        }

        void pushLocked(const T* data, size_t count) {
            if (count == 0) { return; }

            size_t maxBuffered = maxBufferedSamplesLocked();
            if (count > maxBuffered) {
                data += count - maxBuffered;
                count = maxBuffered;
                clearLocked();
            }

            ensureCapacityLocked(std::max(maxBuffered, bufferedSamples + count));
            if (bufferedSamples + count > maxBuffered) {
                dropOldestLocked(bufferedSamples + count - maxBuffered);
            }

            size_t tail = (head + bufferedSamples) % ring.size();
            size_t first = std::min(count, ring.size() - tail);
            std::copy_n(data, first, ring.data() + tail);
            if (count > first) {
                std::copy_n(data + first, count - first, ring.data());
            }
            bufferedSamples += count;
        }

        size_t popLocked(T* outBuf, size_t count, bool consume = true) {
            if (count == 0 || bufferedSamples == 0 || ring.empty()) { return 0; }

            size_t actual = std::min(count, bufferedSamples);
            size_t first = std::min(actual, ring.size() - head);
            std::copy_n(ring.data() + head, first, outBuf);
            if (actual > first) {
                std::copy_n(ring.data(), actual - first, outBuf + first);
            }

            if (consume) {
                head = (head + actual) % ring.size();
                bufferedSamples -= actual;
                if (bufferedSamples == 0) { head = 0; }
            }
            return actual;
        }

        static constexpr int TICK_MS = 10;
        static constexpr int MIN_SLACK_MS = 40;
        static constexpr int MAX_SLACK_MS = 100;

        stream<T>* input = nullptr;
        int prebufferMsec = 100;
        double sampleRate = 1000000.0;
        std::atomic<bool> stopRequested{false};
        bool primed = false;

        std::vector<T> ring;
        size_t head = 0;
        size_t bufferedSamples = 0;
        std::mutex bufferMtx;
        std::condition_variable bufferCnd;
    };
}
