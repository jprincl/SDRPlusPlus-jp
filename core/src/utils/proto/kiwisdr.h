#pragma once

#include <dsp/types.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <core.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <json.hpp>
#include "utils/net.h"
#include "utils/proto/http.h"
#include "utils/proto/websock.h"
#include "utils/url.h"

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

    // Last user-requested tune; remembered so we can re-issue the SET freq
    // command after the server reports its hardware-converter offset.
    std::atomic<double> currentFrequency{0.0};

    // Server-reported hardware-converter offset (Hz). Subtracted from the
    // user-requested frequency before being sent. Updated by MSG/freq_offset.
    std::atomic<int64_t> serverFrequencyOffset{0};

    // KiwiSDR text-protocol metadata (key=value tokens from MSG frames).
    // Touched only on the receive thread, so no synchronization needed.
    std::map<std::string, std::string> keyValues;

public:
    // AGC configuration; persisted across reconnects and re-sent on
    // (re)connect or live via setAgc(). Bundled because the six fields
    // are always read and written as one coherent snapshot — the SET line
    // sent to the server must reflect a single, consistent setAgc() call.
    struct AgcSettings {
        bool enabled = false;       // matches pre-merge default (AGC off)
        bool hang = false;
        int  thresholdDb = -100;
        int  slopeDb = 6;
        int  decayMs = 1000;
        int  manualGainDb = 50;
    };

private:
    AgcSettings agc;
    mutable std::mutex agcMutex;

public:
    Clock::time_point lastPing;
    std::atomic<bool> running{false};
    std::atomic<Modulation> currentModulation{TUNE_IQ};
    std::vector<Clock::time_point> times;

    std::function<void()> onConnected = []() {};
    std::function<void()> onDisconnected = []() {};
    // Fired on hard failures (handshake, connect, /VER lookup) — separate
    // from onDisconnected so a tester or UI can distinguish "never came up"
    // from "came up and then went away". The wsClient still fires
    // onDisconnected too, so handlers don't need to subscribe to both.
    std::function<void(const std::string&)> onError = [](const std::string&) {};
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
            wsClient.sendString("SERVER DE CLIENT sdr++iak SND");
            wsClient.sendString("SET compression=0");
            sendAgcLine();
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
                parseMsgFrame(msg);
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
        currentFrequency.store(freq);
        currentModulation.store(mod);
        sendTuneCommand(freq, mod);
    }

    // Apply new AGC settings. Stored regardless of connection state (so the
    // values persist across reconnects); sent to the server immediately if
    // currently connected. Out-of-range numeric inputs are clamped.
    void setAgc(AgcSettings s) {
        s.thresholdDb  = std::clamp(s.thresholdDb,  -130, 0);
        s.slopeDb      = std::clamp(s.slopeDb,      0, 10);
        s.decayMs      = std::clamp(s.decayMs,      20, 5000);
        s.manualGainDb = std::clamp(s.manualGainDb, 0, 120);
        {
            std::lock_guard<std::mutex> lock(agcMutex);
            agc = s;
        }
        if (connected) {
            sendAgcLine();
        }
    }

    // Coherent snapshot of the current AGC settings.
    AgcSettings getAgc() const {
        std::lock_guard<std::mutex> lock(agcMutex);
        return agc;
    }

private:
    // Build the SET freq command, applying the server-reported hardware
    // offset. Used both by tune() and by the freq_offset MSG handler when it
    // re-issues the last user-requested tune.
    void sendTuneCommand(double freq, Modulation mod) {
        const double serverFreq = freq - static_cast<double>(serverFrequencyOffset.load());
        char buf[1024];
        switch (mod) {
        case Modulation::TUNE_IQ:
            snprintf(buf, sizeof buf, "SET mod=iq low_cut=-7000 high_cut=7000 freq=%0.3f", serverFreq / 1000.0);
            break;
        case Modulation::TUNE_REAL:
            snprintf(buf, sizeof buf, "SET mod=usb low_cut=0 high_cut=8000 freq=%0.3f", (serverFreq - 3000) / 1000.0);
            break;
        }
        wsClient.sendString(buf);
    }

    void sendAgcLine() {
        // Take a snapshot under the mutex so the SET line on the wire
        // always reflects a single, coherent setAgc() — never a mix of
        // before/after values from an in-flight update on another thread.
        AgcSettings snap = getAgc();
        char buf[256];
        snprintf(buf, sizeof buf, "SET agc=%d hang=%d thresh=%d slope=%d decay=%d manGain=%d",
                 snap.enabled ? 1 : 0,
                 snap.hang ? 1 : 0,
                 snap.thresholdDb,
                 snap.slopeDb,
                 snap.decayMs,
                 snap.manualGainDb);
        wsClient.sendString(buf);
    }

    // Tokenize a "MSG key1=v1 key2=v2 ..." text frame from the binary channel
    // and dispatch each pair through onKeyValue. Values are URL-encoded.
    void parseMsgFrame(const std::string& msg) {
        std::istringstream iss(msg);
        std::string token;
        iss >> token;  // consume the "MSG" prefix
        while (iss >> token) {
            const auto eq = token.find('=');
            if (eq == std::string::npos) continue;
            onKeyValue(token.substr(0, eq), url::decode(token.substr(eq + 1)));
        }
    }

    void onKeyValue(const std::string& key, const std::string& value) {
        auto it = keyValues.find(key);
        if (it != keyValues.end() && it->second == value) {
            return;
        }
        keyValues[key] = value;
        if (key == "freq_offset") {
            serverFrequencyOffset.store(parseKhzToHz(value));
            // Re-issue the last user-requested tune so the displayed
            // frequency stays correct once the offset is known. Skip if
            // no tune has happened yet (currentFrequency still default).
            const double last = currentFrequency.load();
            if (last != 0.0) {
                sendTuneCommand(last, currentModulation.load());
            }
        }
    }

    static int64_t parseKhzToHz(const std::string& value) {
        // KiwiSDR reports frequencies in kHz with up to 3 decimal places
        // (1 Hz precision). Use the classic locale to avoid surprises with
        // comma-as-decimal locales.
        std::istringstream iss(value);
        iss.imbue(std::locale::classic());
        double khz = 0.0;
        iss >> khz;
        if (!iss) return 0;
        return static_cast<int64_t>(std::llround(khz * 1000.0));
    }

    // Synchronously GET http://host:port/VER and return its 'ts' field.
    // KiwiSDR uses this value in the websocket path (/kiwi/<ts>/SND); some
    // servers reject paths whose timestamp drifts too far from their own
    // clock. Falls back to the local epoch ms if the fetch or parse fails —
    // local-clock ms is still unique enough for session disambiguation.
    uint64_t fetchServerTimestamp(const std::string& host, int port) {
        using namespace std::chrono;
        const uint64_t fallback = static_cast<uint64_t>(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        try {
            auto sock = net::connect(host, port);
            net::http::Client client(sock);
            net::http::RequestHeader rqhdr(net::http::METHOD_GET, "/VER", host);
            client.sendRequestHeader(rqhdr);
            net::http::ResponseHeader rshdr;
            client.recvResponseHeader(rshdr, 5000);
            std::string body;
            std::vector<uint8_t> buf(1024);
            while (true) {
                int n = sock->recv(buf.data(), buf.size());
                if (n < 1) break;
                body.append(reinterpret_cast<const char*>(buf.data()), n);
                if (body.size() > 64 * 1024) break;   // sanity cap; /VER is tiny
            }
            sock->close();
            auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
            if (!j.is_discarded() && j.contains("ts")) {
                const auto& ts = j["ts"];
                if (ts.is_number_unsigned()) {
                    return ts.get<uint64_t>();
                }
                if (ts.is_string()) {
                    try { return std::stoull(ts.get<std::string>()); } catch (...) {}
                }
            }
        }
        catch (const std::exception& e) {
            flog::warn("KiwiSDRClient: /VER fetch failed: {}", e.what());
        }
        catch (...) {
            flog::warn("KiwiSDRClient: /VER fetch failed: unknown");
        }
        return fallback;
    }

public:

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
                auto parsed = url::splitHostPort(hostPort);
                if (!parsed) {
                    throw std::runtime_error("KiwiSDRClient: malformed host:port: " + hostPort);
                }
                const uint64_t ts = fetchServerTimestamp(parsed->host, parsed->port);
                wsClient.connectAndReceiveLoop(parsed->host, parsed->port, "/kiwi/" + std::to_string(ts) + "/SND");
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
                onError(e.what());
            }
        });
    }
};
