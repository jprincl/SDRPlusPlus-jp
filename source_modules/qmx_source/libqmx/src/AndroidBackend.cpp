#ifdef __ANDROID__

#include "QmxDevice_internal.h"
#include "QmxCatStatus.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cinttypes>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <libusb.h>
#include <sys/time.h>

namespace {
    float convert24(const std::uint8_t* data) {
        std::int32_t value = (static_cast<std::int32_t>(data[2]) << 24) |
                             (static_cast<std::int32_t>(data[1]) << 16) |
                             (static_cast<std::int32_t>(data[0]) << 8);
        value >>= 8;
        return static_cast<float>(value) / 8388608.0f;
    }

    int initAndroidUsbContext(libusb_context** ctx) {
#ifndef LIBUSB_API_VERSION
#error "LIBUSB_API_VERSION is not defined, please update libusb"
#endif

#if LIBUSB_API_VERSION >= 0x0100010A
        libusb_init_option option{};
        option.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY;
        return libusb_init_context(ctx, &option, 1);
#elif LIBUSB_API_VERSION >= 0x01000109
        int rc = libusb_set_option(nullptr, LIBUSB_OPTION_NO_DEVICE_DISCOVERY, nullptr);
        if (rc != LIBUSB_SUCCESS) {
            return rc;
        }
        return libusb_init(ctx);
#else
        return libusb_init(ctx);
#endif
    }
}

namespace qmx::detail {
    class AndroidUsbImpl : public DeviceImpl {
    public:
        ~AndroidUsbImpl() override {
            stop();
        }

        bool start(const StartOptions& options,
                   StreamCallback callback,
                   void* ctx,
                   StatusCallback statusCallback,
                   void* statusCtx,
                   std::string& error) override {
            stop();

            if (!options.androidUsb.valid()) {
                error = "No authorized QMX USB device available on Android";
                return false;
            }
            if (!callback) {
                error = "No IQ callback supplied";
                return false;
            }

            int rc = initAndroidUsbContext(&usbContext);
            if (rc < 0) {
                error = std::string("libusb_init failed: ") + libusb_error_name(rc);
                return false;
            }

            rc = libusb_wrap_sys_device(usbContext, static_cast<intptr_t>(options.androidUsb.fd), &usbHandle);
            if (rc < 0) {
                error = std::string("Failed to open Android USB device: ") + libusb_error_name(rc);
                cleanup();
                return false;
            }
            if (!usbHandle) {
                error = "libusb_wrap_sys_device returned no handle";
                cleanup();
                return false;
            }

            for (int iface : { 0, 1, 2, 3, 4 }) {
                if (!claimInterface(iface, error)) {
                    cleanup();
                    return false;
                }
            }

            rc = libusb_set_interface_alt_setting(usbHandle, 3, 1);
            if (rc < 0) {
                error = std::string("Failed to select QMX audio alt setting: ") + libusb_error_name(rc);
                cleanup();
                return false;
            }

            callbackFn = callback;
            callbackCtx = ctx;
            statusParser.setStatusCallback(statusCallback, statusCtx);
            pending.assign(kStreamBlockSize, {});
            pendingCount = 0;
            statusParser.reset();

            discoverCatEndpoints();
            running.store(true);

            if (options.enableIqMode && !sendCatCommand("Q91;")) {
                running.store(false);
                error = "Failed to enable QMX IQ mode over USB CAT";
                cleanup();
                return false;
            }

            if (!prepareTransfers(error)) {
                running.store(false);
                cleanup();
                return false;
            }

            eventThread = std::thread(&AndroidUsbImpl::run, this);
            if (statusCallback) {
                catThread = std::thread(&AndroidUsbImpl::pollCatLoop, this);
            }
            return true;
        }

        void stop() override {
            bool wasRunning = running.exchange(false);
            if (wasRunning && usbHandle) {
                sendCatCommand("Q90;");
            }

            for (auto* transfer : transfers) {
                if (transfer) {
                    libusb_cancel_transfer(transfer);
                }
            }

            if (catThread.joinable()) {
                catThread.join();
            }
            if (eventThread.joinable()) {
                eventThread.join();
            }

            cleanup();
            pendingCount = 0;
            statusParser.reset();
        }

        bool isStreaming() const override {
            return running.load();
        }

        bool setFrequency(std::int64_t hz, std::string& error) override {
            char cmd[32];
            std::snprintf(cmd, sizeof(cmd), "FA%011" PRId64 ";", hz);
            if (!sendCatCommand(cmd)) {
                error = "Failed to send QMX USB CAT frequency command";
                return false;
            }
            return true;
        }

    private:
        static constexpr int kNumIsoTransfers = 5;
        static constexpr int kIsoPacketSize = 300;
        static constexpr int kNumIsoPackets = 20;

        bool claimInterface(int iface, std::string& error) {
            int rc = libusb_kernel_driver_active(usbHandle, iface);
            if (rc == 1) {
                rc = libusb_detach_kernel_driver(usbHandle, iface);
                if (rc < 0 && rc != LIBUSB_ERROR_NOT_FOUND && rc != LIBUSB_ERROR_NOT_SUPPORTED) {
                    error = std::string("Failed to detach QMX USB kernel driver: ") + libusb_error_name(rc);
                    return false;
                }
            }
            else if (rc < 0 && rc != LIBUSB_ERROR_NOT_SUPPORTED) {
                error = std::string("Failed to query QMX USB kernel driver state: ") + libusb_error_name(rc);
                return false;
            }

            rc = libusb_claim_interface(usbHandle, iface);
            if (rc < 0) {
                error = std::string("Failed to claim QMX USB interface: ") + libusb_error_name(rc);
                return false;
            }
            return true;
        }

        void discoverCatEndpoints() {
            catOutEndpoint = 0x01;
            catInEndpoint = 0x82;

            libusb_config_descriptor* config = nullptr;
            libusb_device* device = usbHandle ? libusb_get_device(usbHandle) : nullptr;
            if (!device || libusb_get_active_config_descriptor(device, &config) != LIBUSB_SUCCESS || !config) {
                return;
            }

            unsigned char bulkOut = 0;
            unsigned char bulkIn = 0;

            for (int ifaceIndex = 0; ifaceIndex < config->bNumInterfaces; ++ifaceIndex) {
                const libusb_interface& iface = config->interface[ifaceIndex];
                for (int altIndex = 0; altIndex < iface.num_altsetting; ++altIndex) {
                    const libusb_interface_descriptor& alt = iface.altsetting[altIndex];
                    for (int epIndex = 0; epIndex < alt.bNumEndpoints; ++epIndex) {
                        const libusb_endpoint_descriptor& ep = alt.endpoint[epIndex];
                        const unsigned char addr = ep.bEndpointAddress;
                        const unsigned char type = ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                        if (addr == 0x83 || type != LIBUSB_TRANSFER_TYPE_BULK) {
                            continue;
                        }

                        const bool isIn = (addr & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN;
                        if (isIn && !bulkIn) {
                            bulkIn = addr;
                        }
                        else if (!isIn && !bulkOut) {
                            bulkOut = addr;
                        }
                    }
                }
            }

            libusb_free_config_descriptor(config);

            if (bulkOut) {
                catOutEndpoint = bulkOut;
            }
            if (bulkIn) {
                catInEndpoint = bulkIn;
            }
        }

        static void transferCallback(libusb_transfer* transfer) {
            auto* self = static_cast<AndroidUsbImpl*>(transfer->user_data);
            if (self) {
                self->handleTransfer(transfer);
            }
        }

        bool prepareTransfers(std::string& error) {
            for (int i = 0; i < kNumIsoTransfers; ++i) {
                transfers[i] = libusb_alloc_transfer(kNumIsoPackets);
                if (!transfers[i]) {
                    error = "Failed to allocate QMX isochronous transfer";
                    return false;
                }

                libusb_fill_iso_transfer(
                    transfers[i],
                    usbHandle,
                    0x83,
                    transferBuffers[i].data(),
                    static_cast<int>(transferBuffers[i].size()),
                    kNumIsoPackets,
                    &AndroidUsbImpl::transferCallback,
                    this,
                    1000);
                libusb_set_iso_packet_lengths(transfers[i], kIsoPacketSize);

                int rc = libusb_submit_transfer(transfers[i]);
                if (rc < 0) {
                    error = std::string("Failed to submit QMX isochronous transfer: ") + libusb_error_name(rc);
                    return false;
                }
            }
            return true;
        }

        bool sendCatCommand(const std::string& command) {
            if (!usbHandle) {
                return false;
            }

            std::lock_guard<std::mutex> lock(catMutex);
            int transferred = 0;
            int rc = libusb_bulk_transfer(
                usbHandle,
                catOutEndpoint,
                reinterpret_cast<unsigned char*>(const_cast<char*>(command.data())),
                static_cast<int>(command.size()),
                &transferred,
                100);
            return rc == LIBUSB_SUCCESS && transferred == static_cast<int>(command.size());
        }

        int readCatChunk(unsigned char* buffer, int bufferSize, int timeoutMs) {
            if (!usbHandle) {
                return LIBUSB_ERROR_NO_DEVICE;
            }

            std::lock_guard<std::mutex> lock(catMutex);
            int transferred = 0;
            int rc = libusb_bulk_transfer(usbHandle, catInEndpoint, buffer, bufferSize, &transferred, timeoutMs);
            if (rc == LIBUSB_SUCCESS) {
                return transferred;
            }
            return rc;
        }

        void pollCatLoop() {
            using namespace std::chrono_literals;

            auto nextIfPoll = std::chrono::steady_clock::now();
            auto nextMeterPoll = nextIfPoll;
            auto nextSlowPoll = nextIfPoll;
            auto nextMenuPoll = nextIfPoll;

            while (running.load()) {
                const auto now = std::chrono::steady_clock::now();
                std::string commands;
                const QmxStatus currentStatus = statusParser.snapshot();
                const bool txActive = currentStatus.hasTransmit && currentStatus.transmit;

                if (now >= nextIfPoll) {
                    commands += "IF;";
                    nextIfPoll = now + 100ms;
                }
                if (now >= nextMeterPoll) {
                    commands += txActive ? "PC;SW;" : "Q1;SM;";
                    nextMeterPoll = now + 250ms;
                }
                if (now >= nextSlowPoll) {
                    commands += "FA;FB;FR;FT;SP;MD;TQ;";
                    nextSlowPoll = now + 1s;
                }
                if (now >= nextMenuPoll && !statusParser.hasPendingMenuValue()) {
                    statusParser.armPendingMenuValue(PendingMenuValue::CwOffset);
                    commands += "MMCW|CW offset;";
                    nextMenuPoll = now + 5s;
                }

                if (!commands.empty()) {
                    if (!sendCatCommand(commands)) {
                        statusParser.clearPendingMenuValue();
                        std::this_thread::sleep_for(25ms);
                        continue;
                    }
                    readResponsesFor(120ms);
                    continue;
                }

                readResponsesFor(25ms);
                std::this_thread::sleep_for(10ms);
            }
        }

        void readResponsesFor(std::chrono::milliseconds maxDuration) {
            const auto start = std::chrono::steady_clock::now();
            std::array<unsigned char, 256> buffer{};
            int idleReads = 0;

            while (running.load() && std::chrono::steady_clock::now() - start < maxDuration && idleReads < 2) {
                int rc = readCatChunk(buffer.data(), static_cast<int>(buffer.size()), 40);
                if (rc == LIBUSB_ERROR_TIMEOUT || rc == 0) {
                    ++idleReads;
                    continue;
                }
                if (rc < 0) {
                    return;
                }

                idleReads = 0;
                statusParser.feedBytes(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(rc));
            }
        }

        void push(float i, float q) {
            pending[pendingCount++] = { i, q };
            if (pendingCount == pending.size()) {
                callbackFn(pending.data(), pending.size(), callbackCtx);
                pendingCount = 0;
            }
        }

        void handleTransfer(libusb_transfer* transfer) {
            if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
                running.store(false);
                return;
            }

            for (int packet = 0; packet < transfer->num_iso_packets; ++packet) {
                libusb_iso_packet_descriptor* desc = &transfer->iso_packet_desc[packet];
                if (desc->status != LIBUSB_TRANSFER_COMPLETED || desc->actual_length <= 0) {
                    continue;
                }

                const std::uint8_t* data = libusb_get_iso_packet_buffer_simple(transfer, packet);
                int frames = desc->actual_length / 6;
                for (int i = 0; i < frames; ++i) {
                    push(convert24(data), convert24(data + 3));
                    data += 6;
                }
            }

            if (running.load()) {
                int rc = libusb_submit_transfer(transfer);
                if (rc < 0) {
                    running.store(false);
                }
            }
        }

        void run() {
            while (running.load()) {
                timeval timeout = { 0, 100000 };
                int rc = libusb_handle_events_timeout_completed(usbContext, &timeout, nullptr);
                if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_TIMEOUT) {
                    break;
                }
            }
        }

        void cleanup() {
            if (usbContext) {
                for (int i = 0; i < 50; ++i) {
                    struct timeval tv = { 0, 50000 };
                    libusb_handle_events_timeout_completed(usbContext, &tv, nullptr);
                }
                for (auto*& transfer : transfers) {
                    if (transfer) {
                        libusb_free_transfer(transfer);
                        transfer = nullptr;
                    }
                }

                if (usbHandle) {
                    for (int iface : { 0, 1, 2, 3, 4 }) {
                        libusb_release_interface(usbHandle, iface);
                    }
                    libusb_close(usbHandle);
                    usbHandle = nullptr;
                }
                libusb_exit(usbContext);
                usbContext = nullptr;
            }
        }

        libusb_context* usbContext = nullptr;
        libusb_device_handle* usbHandle = nullptr;
        unsigned char catOutEndpoint = 0x01;
        unsigned char catInEndpoint = 0x82;
        std::array<std::array<unsigned char, kIsoPacketSize * kNumIsoPackets>, kNumIsoTransfers> transferBuffers{};
        std::array<libusb_transfer*, kNumIsoTransfers> transfers{};
        StreamCallback callbackFn = nullptr;
        void* callbackCtx = nullptr;
        std::vector<IQSample> pending;
        std::size_t pendingCount = 0;
        std::thread eventThread;
        std::thread catThread;
        std::mutex catMutex;
        QmxCatStatusParser statusParser;
        std::atomic<bool> running = false;
    };

    std::vector<AudioDeviceInfo> listPlatformAudioDevices() {
        return {};
    }

    std::vector<SerialPortInfo> listPlatformSerialPorts() {
        return {};
    }

    std::unique_ptr<DeviceImpl> createDeviceImpl() {
        return std::make_unique<AndroidUsbImpl>();
    }
}

#endif
