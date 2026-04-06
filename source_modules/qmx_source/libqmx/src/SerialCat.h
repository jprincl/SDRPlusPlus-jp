#pragma once

#include <qmx/QmxDevice.h>

#include "CatPoller.h"

#include <cstdint>
#include <string>
#include <vector>

namespace qmx::detail {
    class SerialCatPort : public CatTransport {
    public:
        ~SerialCatPort();

        bool open(const std::string& portName);
        void close();
        bool isOpen() const;

        void setStatusCallback(StatusCallback callback, void* ctx);
#if QMX_CAT_RAW_LOG
        void setCatLogCallback(CatLogCallback callback, void* ctx);
#endif

        bool setIQMode(bool enabled);
        bool setFrequency(std::int64_t frequency, int vfo = 0);
        bool setMode(QmxMode mode);

        static std::vector<SerialPortInfo> listPorts();

        // CatTransport
        bool sendCommand(const std::string& command) override;
        std::size_t readBytes(char* buffer, std::size_t size) override;

    private:
        void* handle = nullptr;
        CatPoller poller;
        StatusCallback storedStatusCallback = nullptr;
        void* storedStatusCtx = nullptr;
#if QMX_CAT_RAW_LOG
        CatLogCallback storedCatLogCallback = nullptr;
        void* storedCatLogCtx = nullptr;
#endif
    };
}
