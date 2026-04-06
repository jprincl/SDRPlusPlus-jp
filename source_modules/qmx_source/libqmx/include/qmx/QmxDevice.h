#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <qmx/QmxCatDebug.h>

namespace qmx {
    namespace detail {
        class DeviceImpl;
    }

    constexpr int kSampleRate = 48000;
    // Serial baud rate is a placeholder with no real meaning for QMX CDC.
    constexpr int kSerialBaudRate = 115200;
    // Audio input buffer for one second of audio at 48 kHz, stereo, 24 bit signed int samples,
    // aligned to the QMX USB audio endpoint packet size.
    constexpr std::size_t kStreamBlockSize = 512;

    struct AudioDeviceInfo {
        std::string id;
        std::string label;
    };

    struct SerialPortInfo {
        std::string path;
        std::string label;
    };

    struct AndroidUsbDeviceInfo {
        int fd = -1;
        int vid = -1;
        int pid = -1;
        std::string path;

        bool valid() const { return fd >= 0; }
    };

    struct StartOptions {
        std::string audioDeviceId;
        std::string serialPort;
        AndroidUsbDeviceInfo androidUsb;
        bool enableIqMode = true;
    };

    struct IQSample {
        float i = 0.0f;
        float q = 0.0f;
    };

    enum class QmxMode {
        UNKNOWN,
        CW,
        CWR,
        DIGI,
        USB,
        LSB,
        FM, // Parsed for Kenwood MD compatibility; QMX does not use this yet.
        AM  // Parsed for Kenwood MD compatibility; QMX does not use this yet.
    };

    enum class QmxSideband {
        UNKNOWN,
        USB,
        LSB
    };

    // Queried regularly from QMX over CAT (USB CDC connection).
    struct QmxStatus {
        bool hasFrequency = false;
        std::int64_t frequency = 0;
        bool hasVfoAFrequency = false;
        std::int64_t vfoAFrequency = 0;
        bool hasVfoBFrequency = false;
        std::int64_t vfoBFrequency = 0;
        bool hasTransmit = false;
        bool transmit = false;
        bool hasMode = false;
        QmxMode mode = QmxMode::UNKNOWN;
        bool hasSideband = false;
        QmxSideband sideband = QmxSideband::UNKNOWN;
        bool hasRxVfo = false;
        int rxVfo = -1;
        bool hasTxVfo = false;
        int txVfo = -1;
        bool hasSplit = false;
        bool split = false;
        bool hasRit = false;
        int ritHz = 0;
        bool hasRitEnabled = false;
        bool ritEnabled = false;
        bool hasSMeter = false;
        int sMeterDb = 0;
        bool hasPower = false;
        int powerTenthsW = 0;
        bool hasSWR = false;
        int swrHundredths = 0;
        bool hasCwOffset = false;
        int cwOffsetHz = 0;
        std::uint64_t sequence = 0;
#if QMX_CAT_DEBUG_TIMING
        QmxCatTimingDebug catDebug;
#endif
    };

    // Called by QMX driver when a new IQ sample block is available.
    using StreamCallback = void (*)(const IQSample* samples, std::size_t count, void* ctx);
    // Called by QMX driver when a new status update is available from CAT.
    using StatusCallback = void (*)(const QmxStatus& status, void* ctx);
#if QMX_CAT_RAW_LOG
    using CatLogCallback = void (*)(const QmxCatLogEntry& entry, void* ctx);
#endif

    class QmxDevice {
    public:
        QmxDevice();
        ~QmxDevice();

        QmxDevice(const QmxDevice&) = delete;
        QmxDevice& operator=(const QmxDevice&) = delete;

        static bool isSupported();
        static std::vector<AudioDeviceInfo> listAudioDevices();
        static std::vector<SerialPortInfo> listSerialPorts();

        bool start(const StartOptions& options,
                   StreamCallback callback,
                   void* ctx,
                   StatusCallback statusCallback = nullptr,
                   void* statusCtx = nullptr,
#if QMX_CAT_RAW_LOG
                   CatLogCallback catLogCallback = nullptr,
                   void* catLogCtx = nullptr,
#endif
                   std::string* error = nullptr);
        void stop();

        bool isStreaming() const;
        // Set QMX radio frequency by sending "FA" or "FB" Kenwood 480 compatible CAT command over USB CDC,
        // matching the active receive VFO (0 = VFO A → FA, 1 = VFO B → FB).
        // The frequency is the QMX radio dial frequency, accounting for mode-specific offsets like CW pitch.
        bool setFrequency(std::int64_t hz, int vfo = 0, std::string* error = nullptr);
        // Set QMX radio mode by sending "MD" Kenwood 480 compatible CAT command over USB CDC.
        bool setMode(QmxMode mode, std::string* error = nullptr);

        std::string lastError() const;

    private:
        void setError(const std::string& error, std::string* out);

        std::unique_ptr<detail::DeviceImpl> impl;
        std::string error;
    };
}
