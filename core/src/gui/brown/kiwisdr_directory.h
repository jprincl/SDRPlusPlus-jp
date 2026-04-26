#pragma once

#include <atomic>
#include <imgui.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct ServerEntry {
    ImVec2 gps;
    std::string name;
    std::string loc;
    std::string url;
    std::string antenna;
    float maxSnr = 0.0f;
    float secondSnr = 0.0f;
    int users = 0;
    int usersmax = 0;
    int extApi = 0;
    bool selected = false;
};

/**
 * Fetches and caches the public KiwiSDR receiver list from rx.linkfanel.net.
 * The HTTP fetch runs on a background thread; the UI thread polls for the
 * parsed result via takeIfReady().
 */
class KiwiSDRDirectoryClient {
public:
    explicit KiwiSDRDirectoryClient(std::string root);
    ~KiwiSDRDirectoryClient();

    KiwiSDRDirectoryClient(const KiwiSDRDirectoryClient&) = delete;
    KiwiSDRDirectoryClient& operator=(const KiwiSDRDirectoryClient&) = delete;

    /// Idempotent: starts a background fetch the first time it is called and
    /// after that does nothing until the result has been delivered.
    void requestRefresh();

    bool isLoading() const;
    std::string errorMessage() const;

    /// Returns the parsed result the first time it is ready and resets the
    /// internal state; subsequent calls return std::nullopt.
    std::optional<std::vector<ServerEntry>> takeIfReady();

private:
    void fetchThreadFn();

    const std::string root;
    std::thread fetchThread;
    mutable std::mutex stateMutex;
    bool loading = false;
    bool fetchAttempted = false;
    std::string errorMsg;
    std::optional<std::vector<ServerEntry>> result;
};

/**
 * Probes a KiwiSDR server: connects, tunes 14.074 MHz IQ, waits up to 5 s
 * for the first IQ packet, reports OK / NOT OK. Status text is observable
 * from the UI thread via the snapshot accessors.
 */
class KiwiSDRTester {
public:
    KiwiSDRTester() = default;
    ~KiwiSDRTester();

    KiwiSDRTester(const KiwiSDRTester&) = delete;
    KiwiSDRTester& operator=(const KiwiSDRTester&) = delete;

    struct OkServer {
        std::string hostPort;
        std::string loc;
    };

    /// Start probing the given URL. No-op if a probe is already in flight.
    void start(std::string url, std::string loc);

    /// Request cancellation of any in-flight probe.
    void cancel();

    bool isInProgress() const;
    std::string statusText() const;
    std::string errorText() const;

    /// Returns the most recently successfully-tested server, if any.
    /// Persists across calls until a new test starts.
    std::optional<OkServer> lastOk() const;

private:
    void testThreadFn(std::string url, std::string loc);

    std::thread testThread;
    mutable std::mutex stateMutex;
    bool inProgress = false;
    std::atomic<bool> cancelRequested{false};
    std::string status;
    std::string error;
    std::optional<OkServer> lastOkServer;
};
