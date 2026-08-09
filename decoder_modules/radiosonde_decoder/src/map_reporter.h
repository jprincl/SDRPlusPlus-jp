#pragma once

// Universal, standalone TCP JSON-lines position reporter for decoders.
//
// Purpose: a decoder (radiosonde, FT8/WSPR, ADS-B, ACARS, whatever comes
// next) has a position and wants to send it to a live map -- whether
// that's web_map in this SDR++, or entirely different software on another
// machine. This class is the "universal" half of that hookup: the
// protocol itself (newline JSON, F4JTV-compatible envelope -- see
// web_map/src/tcp_collector.h for the exact shape) doesn't care who's
// listening on the other end. Where it goes is just a host:port that each
// decoder owns and exposes to the user as an ordinary config field (same
// as how radiosonde_decoder already handles its GPX/log files) -- no
// dependency on any specific consumer.
//
// Copy this pair of files (map_reporter.h/.cpp) unchanged into src/ of any
// other decoder you want to wire up this way.
//
// Design note: send() never blocks the caller -- it only queues the line
// and returns immediately. The actual (potentially slow, potentially
// blocking) network I/O, including connecting/reconnecting, runs on its
// own thread. This matters because decoders typically call send() right
// from the DSP/decode callback -- if networking blocked there, the whole
// decode chain would stall whenever the receiver isn't running or is slow.

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

class MapReporter {
public:
    MapReporter() = default;
    ~MapReporter();

    MapReporter(const MapReporter&) = delete;
    MapReporter& operator=(const MapReporter&) = delete;

    // Starts the sender thread. Safe to call repeatedly -- a second call
    // without a preceding stop() is a no-op.
    void start(const std::string& host, int port);
    void stop();
    bool isRunning() const { return running_.load(); }

    // Fire-and-forget: queues ONE JSON line (no trailing '\n' needed, it's
    // added automatically) for sending. Safe to call from any thread,
    // including directly from a DSP/decode callback. If there's currently
    // no connection (still connecting, or start() was never called), the
    // line is simply queued -- the queue is bounded (kMaxQueued), oldest
    // entries are dropped on overflow so a long network outage doesn't
    // grow memory unbounded.
    void send(const std::string& jsonLine);

private:
    void senderLoop();

    // A single connection attempt (blocking connect with a short timeout
    // via select/poll). Returns true = connected, false = try again later.
    bool connectOnce();
    void closeSocket();

    std::string host_;
    int port_ = 0;

    std::atomic<bool> running_{false};
    std::thread thread_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    static constexpr size_t kMaxQueued = 50;

#if defined(_WIN32)
    SOCKET sock_ = INVALID_SOCKET;
#else
    int sock_ = -1;
#endif
};
