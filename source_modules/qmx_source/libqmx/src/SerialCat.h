#pragma once

#include <qmx/QmxDevice.h>

#include "QmxCatStatus.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace qmx::detail {
    class SerialCatPort {
    public:
        ~SerialCatPort();

        bool open(const std::string& portName);
        void close();
        bool isOpen() const;

        void setStatusCallback(StatusCallback callback, void* ctx);

        bool setIQMode(bool enabled);
        bool setFrequency(std::int64_t frequency);

        static std::vector<SerialPortInfo> listPorts();

    private:
        bool send(const std::string& command);
        std::size_t readSome(char* buffer, std::size_t bufferSize);
        void pollLoop();
        void readResponsesFor(std::chrono::milliseconds maxDuration);

        void* handle = nullptr;
        mutable std::mutex ioMutex;
        std::thread pollWorker;
        std::atomic<bool> polling = false;
        QmxCatStatusParser statusParser;
    };
}
