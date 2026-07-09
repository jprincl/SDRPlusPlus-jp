#pragma once

#include <atomic>
#include <chrono>
#include <complex>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

    // Last user-requested tune; remembered so we can re-issue the SET freq
    // command after the server reports its hardware-converter offset.
    std::atomic<double> currentFrequency{0.0};

    // Server-reported hardware-converter offset (Hz). Subtracted from the
    // user-requested frequency before being sent. Updated by MSG/freq_offset.
    std::atomic<int64_t> serverFrequencyOffset{0};

    // Server-reported ADC passband width (Hz), from the MSG/bandwidth field.
    // 0 = not yet reported. Combined with serverFrequencyOffset this yields
    // the tunable dial span [freq_offset, freq_offset + bandwidth]. The
    // 'center_freq' MSG is deliberately ignored: it is the cosmetic ADC
    // passband center, not part of the tuning span.
    std::atomic<int64_t> serverBandwidth{0};

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
    // deque, not vector: front entries are popped on every packet, and a
    // vector::erase(begin) shift would be O(n) per packet.
    std::deque<Clock::time_point> times;

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

    static int16_t readBE16(const char* data);

    void setConnectionStatus(const char* status);
    std::string getConnectionStatus() const;

    virtual ~KiwiSDRClient();

    int IQDATA_FREQUENCY = 12000;
    // Backstop cap for iqData growth (consumers pace/trim themselves; this
    // only bounds memory when nobody drains, e.g. an idle tester session).
    int NETWORK_BUFFER_SECONDS = 5;
    int NETWORK_BUFFER_SIZE = NETWORK_BUFFER_SECONDS * IQDATA_FREQUENCY;

    void init(const std::string& hostport);

    void tune(double freq, Modulation mod);

    // Apply new AGC settings. Stored regardless of connection state (so the
    // values persist across reconnects); sent to the server immediately if
    // currently connected. Out-of-range numeric inputs are clamped.
    void setAgc(AgcSettings s);

    // Coherent snapshot of the current AGC settings.
    AgcSettings getAgc() const;

    struct ServedRange {
        int64_t minHz;   // freq_offset
        int64_t maxHz;   // freq_offset + bandwidth
    };
    // Live tunable dial span derived from the server's reported freq_offset
    // and bandwidth, or nullopt until the server has reported its bandwidth.
    std::optional<ServedRange> getServedRange() const;

private:
    // Fire-and-forget command send. tune() and setAgc() run on the GUI
    // thread and can race the socket dying on a flaky link; a failed
    // command must not throw into the GUI thread (uncaught => terminate).
    // The receive loop notices the dead socket and tears the session down.
    bool trySend(const std::string& cmd);

    // Build the SET freq command, applying the server-reported hardware
    // offset. Used both by tune() and by the freq_offset MSG handler when it
    // re-issues the last user-requested tune.
    void sendTuneCommand(double freq, Modulation mod);

    void sendAgcLine();

    // Tokenize a "MSG key1=v1 key2=v2 ..." text frame from the binary channel
    // and dispatch each pair through onKeyValue. Values are URL-encoded.
    void parseMsgFrame(const std::string& msg);

    void onKeyValue(const std::string& key, const std::string& value);

    static int64_t parseKhzToHz(const std::string& value);

    // Synchronously GET http://host:port/VER and return its 'ts' field.
    // KiwiSDR uses this value in the websocket path (/kiwi/<ts>/SND); some
    // servers reject paths whose timestamp drifts too far from their own
    // clock. Falls back to the local epoch ms if the fetch or parse fails —
    // local-clock ms is still unique enough for session disambiguation.
    uint64_t fetchServerTimestamp(const std::string& host, int port);

    void resetSessionState();

public:
    void stop();
    void start();
};
