#include "CatPoller.h"

#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace qmx::detail {

    std::future<bool> CatPoller::enqueueCommand(std::string command, QmxStatusFlags clearFlags) {
        PendingCommand pc;
        pc.command = std::move(command);
        pc.clearFlags = clearFlags;
        auto f = pc.result.get_future();
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            commandQueue.push_back(std::move(pc));
        }
        return f;
    }

    void CatPoller::start(CatTransport* transport, StatusCallback statusCallback, void* statusCtx
#if QMX_CAT_RAW_LOG
                          , CatLogCallback catLogCallback, void* catLogCtx
#endif
    ) {
        stop();
        this->transport = transport;
        statusParser.reset();
        statusParser.setStatusCallback(statusCallback, statusCtx);
#if QMX_CAT_RAW_LOG
        statusParser.setCatLogCallback(catLogCallback, catLogCtx);
#endif
#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        transport->setDebugParser(&statusParser);
#endif
        polling.store(true);
        pollWorker = std::thread(&CatPoller::pollLoop, this);
    }

    void CatPoller::stop() {
        polling.store(false);
        if (pollWorker.joinable())
            pollWorker.join();

        // Reject any remaining queued commands.
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (auto& pc : commandQueue) {
                pc.result.set_value(false);
            }
            commandQueue.clear();
        }

#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        if (transport) {
            transport->setDebugParser(nullptr);
        }
#endif
        statusParser.reset();
        transport = nullptr;
    }

    void CatPoller::pollLoop() {
        using namespace std::chrono_literals;

        auto nextIfPoll = std::chrono::steady_clock::now();
        auto nextMeterPoll = nextIfPoll;
        auto nextSlowPoll = nextIfPoll;
        auto nextMenuPoll = nextIfPoll;

        while (polling.load()) {
            // Drain queued commands from the main thread and send them.
            // Setter commands (FA, MD, Q9) produce no response when accepted.
            {
                std::deque<PendingCommand> queued;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    queued.swap(commandQueue);
                }
                if (!queued.empty()) {
                    for (auto& pc : queued) {
                        const bool ok = transport->sendCommand(pc.command);
                        if (ok && pc.clearFlags != 0)
                            statusParser.clearStatusFlags(pc.clearFlags);
                        pc.result.set_value(ok);
                    }
                    // Wait for QMX to process the commands so that the polling will not return the old values.
                    std::this_thread::sleep_for(10ms);
                }
            }

            const auto now = std::chrono::steady_clock::now();
            std::string commands;
            size_t      expectedReplies = 0;

            if (now >= nextIfPoll) {
                commands += "IF;";
                expectedReplies += 1;
                nextIfPoll = now + 100ms;
            }
            if (now >= nextMeterPoll) {
                const QmxStatus currentStatus   = statusParser.snapshot();
                const bool      txActive        = currentStatus.hasTransmit() && currentStatus.transmit;
                commands += txActive ? "PC;SW;" : "SM;";
                expectedReplies += txActive ? 2 : 1;
                nextMeterPoll = now + 250ms;
            }
            if (now >= nextSlowPoll) {
                commands += "FA;FB;FT;";
                expectedReplies += 3;
                nextSlowPoll = now + 1s;
            }
            if (now >= nextMenuPoll && !statusParser.hasPendingMenuValue()) {
                statusParser.armPendingMenuValue(PendingMenuValue::CwOffset);
                commands += "MMCW|CW offset;";
                expectedReplies += 1;
                nextMenuPoll = now + 5s;
            }

            if (expectedReplies > 0) {
                if (!transport->sendCommand(commands)) {
                    statusParser.clearPendingMenuValue();
                    std::this_thread::sleep_for(25ms);
                    continue;
                }
                statusParser.beginBatch(expectedReplies);
                readResponsesFor(120ms);
                statusParser.publishBatch();
                continue;
            }

            statusParser.beginBatch(0);
            readResponsesFor(25ms);
            statusParser.publishBatch();
            std::this_thread::sleep_for(10ms);
        }
    }

    void CatPoller::readResponsesFor(std::chrono::milliseconds maxDuration) {
        const auto start = std::chrono::steady_clock::now();
        char buffer[256];
        int idleReads = 0;

        while (polling.load() && std::chrono::steady_clock::now() - start < maxDuration && idleReads < 1) {
            if (statusParser.batchComplete())
                break;

            const std::size_t count = transport->readBytes(buffer, sizeof(buffer));
            if (count == 0) {
                ++idleReads;
                continue;
            }

            idleReads = 0;
            statusParser.feedBytes(buffer, count);
        }
    }

}
