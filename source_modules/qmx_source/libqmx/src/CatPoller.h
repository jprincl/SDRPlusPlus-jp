#pragma once

#include <qmx/QmxDevice.h>

#include "QmxCatStatus.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>

namespace qmx::detail {

    // Interface for QMX CAT command transform, platform dependent.
    // Namely, on Android a direct libusb connection is used, while on other platforms a system USB CDC driver is used
    // by opening a virtual serial line.
    class CatTransport {
    public:
        virtual ~CatTransport() = default;
        virtual bool sendCommand(const std::string& command) = 0;
        virtual std::size_t readBytes(char* buffer, std::size_t size) = 0;

#if QMX_CAT_DEBUG_TIMING || QMX_CAT_RAW_LOG
        void setDebugParser(QmxCatStatusParser* p) { debugParser = p; }

    protected:
        QmxCatStatusParser* debugParser = nullptr;
#endif
    };

    struct PendingCommand {
        std::string command;
        std::promise<bool> result;
    };

    // Worker thread that continuously polls CAT responses from QMX and feeds them to a QmxCatStatusParser, 
    // which in turn triggers status updates to the main QmxDevice class.
    class CatPoller {
    public:
        ~CatPoller() { stop(); }

        // Enqueue a command to be sent by the poller thread.
        // Blocks the caller until the command has been sent and returns the send result.
        std::future<bool> enqueueCommand(std::string command);

        void start(CatTransport* transport, StatusCallback statusCallback, void* statusCtx
#if QMX_CAT_RAW_LOG
                   , CatLogCallback catLogCallback = nullptr, void* catLogCtx = nullptr
#endif
        );
        void stop();
        void requestStop() { polling.store(false); }
        bool isRunning() const { return polling.load(); }

        QmxCatStatusParser& parser() { return statusParser; }
        const QmxCatStatusParser& parser() const { return statusParser; }

    private:
        void pollLoop();
        void readResponsesFor(std::chrono::milliseconds maxDuration);

        CatTransport* transport = nullptr;
        QmxCatStatusParser statusParser;
        std::thread pollWorker;
        std::atomic<bool> polling{false};

        std::mutex queueMutex;
        std::deque<PendingCommand> commandQueue;
    };

}
