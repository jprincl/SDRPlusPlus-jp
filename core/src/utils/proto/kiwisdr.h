#pragma once

#include <dsp/types.h>
#include <complex>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <core.h>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "utils/proto/websock.h"

struct KiwiSDRClient {
    using Clock = std::chrono::steady_clock;

    enum Modulation {
        TUNE_IQ = 1,
        TUNE_REAL = 2,           // only real data, -3 .. +3 kHz
    };

    net::websock::WSClient wsClient;
    std::string hostPort;
    std::atomic<bool> connected{false};

private:
    char connectionStatus[100] = "Not connected";
    mutable std::mutex connectionStatusLock;

public:
    Clock::time_point lastPing;
    std::atomic<bool> running{false};
    std::atomic<Modulation> currentModulation{TUNE_IQ};
    std::vector<Clock::time_point> times;

    std::function<void()> onConnected = []() {};
    std::function<void()> onDisconnected = []() {};
    std::vector<std::complex<float>> iqData;
    std::mutex iqDataLock;
    std::thread looperThread;

    static int16_t readBE16(const char* data) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(data);
        const uint16_t value = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
        if (value < 0x8000) {
            return static_cast<int16_t>(value);
        }
        return static_cast<int16_t>(static_cast<int>(value) - 0x10000);
    }

    void setConnectionStatus(const char* status) {
        std::lock_guard<std::mutex> lock(connectionStatusLock);
        snprintf(connectionStatus, sizeof connectionStatus, "%s", status);
    }

    std::string getConnectionStatus() const {
        std::lock_guard<std::mutex> lock(connectionStatusLock);
        return connectionStatus;
    }

    virtual ~KiwiSDRClient() {
        flog::info("KiwiSDRClient: destructor");
        stop();
    }

    int IQDATA_FREQUENCY = 12000;
    int NETWORK_BUFFER_SECONDS = 2;
    int NETWORK_BUFFER_SIZE = NETWORK_BUFFER_SECONDS * IQDATA_FREQUENCY;

    void init(const std::string& hostport) {

        this->hostPort = hostport;
        setConnectionStatus("Not connected");
        static std::atomic_int outCount;

        outCount = 0;

        wsClient.onDisconnected = [&]() {
            connected = false;
            this->onDisconnected();
            setConnectionStatus("Disconnected");
        };

        wsClient.onConnected = [&]() {
            // x.sendString("SET mod=usb low_cut=300 high_cut=2700 freq=14100.000");
            wsClient.sendString("SET auth t=kiwi p=#");
            wsClient.sendString("SET AR OK in=" + std::to_string(IQDATA_FREQUENCY) + " out=48000");
            //            x.sendString("SET mod=am low_cut=-4900 high_cut=4900 freq=119604.33");
            wsClient.sendString("SERVER DE CLIENT sdr++brown SND");
            wsClient.sendString("SET compression=0");
            wsClient.sendString("SET agc=0 hang=0 thresh=-100 slope=6 decay=1000 manGain=50");
            connected = true;
            if (this->onConnected) {
                onConnected();
            }
            setConnectionStatus("Connected, waiting data...");
            //            wsClient.sendString("SET mod=iq low_cut=-5000 high_cut=5000 freq=14074.000");
        };
        wsClient.onTextMessage = [&](const std::string& msg) {
            flog::info("TEXT: {}", msg);
        };

        wsClient.onBinaryMessage = [&](const std::string& msg) {
            std::string start = "???";
            if (msg.size() > 3) {
                start = msg.substr(0, 3);
            }
            if (start == "MSG") {
//                flog::info("=> BIN/MSG: {} text: {}", (int64_t)msg.size(), msg);
            }
            else if (start == "SND") {
                if ((outCount++) % 50 == 0) {
//                    flog::info("=> SND (each 50 packets)");
                }
                using namespace std::chrono_literals;
                auto ctm = Clock::now();
                times.emplace_back(ctm);
                int lastSecondCount = 0;
                for (int q = times.size() - 1; q >= 0; q--) {
                    if (times[q] < ctm - 1s) {
                        break;
                    }
                    lastSecondCount++;
                }
                while (!times.empty() && times.front() < ctm - 2s) {
                    times.erase(times.begin());
                }
                char status[100];
                snprintf(status, sizeof status, "Receiving. %d KB/sec (%d)", (lastSecondCount * ((int)msg.size())) / 1024, lastSecondCount);
                setConnectionStatus(status);
                int IQ_HEADER_SIZE = 20;
                int REAL_HEADER_SIZE = 10;
                const Modulation modulation = currentModulation.load();
                if (modulation == TUNE_REAL && msg.size() == 1024 + REAL_HEADER_SIZE) { // REAL data
                    const char* samples = msg.data() + REAL_HEADER_SIZE;
                    setConnectionStatus("Storing real..");
                    {
                        std::lock_guard<std::mutex> lock(iqDataLock);
                        for (int z = 0; z < 512; z++) {
                            const int16_t sample = readBE16(samples + (z * 2));
                            iqData.emplace_back(sample / 32767.0f, 0.0f);
                        }
                        while (iqData.size() > NETWORK_BUFFER_SIZE * 1.5) {
                            iqData.erase(iqData.begin(), iqData.begin() + 200);
                        }
                    }
                    snprintf(status, sizeof status, "Cont Recv. %d KB/sec (%d)", (lastSecondCount * ((int)msg.size())) / 1024, lastSecondCount);
                    setConnectionStatus(status);
                }
                if (modulation == TUNE_IQ && msg.size() == 2048 + IQ_HEADER_SIZE && msg[3] == 0x08) { // IQ data
                    const char* samples = msg.data() + IQ_HEADER_SIZE;
                    {
                        std::lock_guard<std::mutex> lock(iqDataLock);
                        for (int z = 0; z < 512; z++) {
                            const char* iqsample = samples + (z * 4);
                            const int16_t i = readBE16(iqsample);
                            const int16_t q = readBE16(iqsample + 2);
                            iqData.emplace_back(i / 32767.0f, q / 32767.0f);
                        }
                        while (iqData.size() > NETWORK_BUFFER_SIZE * 1.5) {
                            iqData.erase(iqData.begin(), iqData.begin() + 200);
                        }
                    }
                    //                    flog::info("{} Got sound: bytes={} , {} samples, buflen now = {} (erased {})", (int64_t)currentTimeMillis(), msg.size(), (msg.size() - HEADER_SIZE) / 4, buflen, erased);
                }
            }
            else {
                if (msg.size() >= 70) {
                    char buf[100];
                    for (int q = 3; q < 30; q++) {
                        snprintf(buf, sizeof buf,"%02x ", (unsigned char)msg[q]);
                        start += buf;
                    }
                    start += "... ";
                    for (int q = -20; q < 0; q++) {
                        snprintf(buf, sizeof buf,"%02x ", (unsigned char)msg[msg.size() - 1 + q]);
                        start += buf;
                    }
                }
//                flog::info("=> BIN: {} bytes: {}", (int64_t)msg.size(), start);
            }
        };
        using namespace std::chrono_literals;
        lastPing = Clock::now();
        wsClient.onEveryReceive = [&]() {
            auto now = Clock::now();
            if (now - lastPing > 4s) {
                wsClient.sendString("SET keepalive");
                lastPing = now;
            }
        };
    }


    void tune(double freq, Modulation mod) {
        char buf[1024];
        switch (mod) {
        case Modulation::TUNE_IQ:
            currentModulation.store(mod);
            snprintf(buf, sizeof buf, "SET mod=iq low_cut=-7000 high_cut=7000 freq=%0.3f", freq / 1000.0);
            break;
        case Modulation::TUNE_REAL:
            currentModulation.store(mod);
            snprintf(buf, sizeof(buf), "SET mod=usb low_cut=0 high_cut=8000 freq=%0.3f", (freq - 3000) / 1000.0);
            break;
        }
        wsClient.sendString(buf);
    }

    void stop() {
        if (running.load() || looperThread.joinable()) {
            setConnectionStatus("Disconnecting..");
        }
        running = false;
        wsClient.stopSocket();
        if (looperThread.joinable()) {
            if (looperThread.get_id() != std::this_thread::get_id()) {
                looperThread.join();
            }
        }
        connected = false;
        setConnectionStatus("Disconnected.");
    }

    void start() {
        if (running.load()) { return; }
        if (looperThread.joinable()) {
            looperThread.join();
        }
        setConnectionStatus("Connecting..");
        running = true;
        connected = false;
        wsClient.stopped = false;
        looperThread = std::thread([this]() {
            flog::info("calling x.connectAndReceiveLoop..");
            try {
                std::string hostName;
                int port;
                std::size_t colonPosition = hostPort.find(":");
                if (colonPosition != std::string::npos) {
                    hostName = hostPort.substr(0, colonPosition);
                    port = std::stoi(hostPort.substr(colonPosition + 1));
                }
                else {
                    hostName = hostPort;
                    port = 0;
                }
                using namespace std::chrono;
                auto epochMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
                wsClient.connectAndReceiveLoop(hostName, port, "/kiwi/" + std::to_string(epochMs) + "/SND");
                flog::info("x.connectAndReceiveLoop exited.");
                setConnectionStatus("Disconnected");
                connected = false;
                running = false;
            }
            catch (const std::runtime_error& e) {
                flog::error("KiwiSDRSourceModule: Exception: {}", e.what());
                char status[100];
                snprintf(status, sizeof status, "Error: %s", e.what());
                setConnectionStatus(status);
                connected = false;
                running = false;
            }
        });
    }
};
