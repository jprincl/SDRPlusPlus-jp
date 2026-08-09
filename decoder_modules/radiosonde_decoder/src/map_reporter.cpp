#include "map_reporter.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
    // SOCKET/INVALID_SOCKET etc. already come from map_reporter.h (winsock2.h)
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

MapReporter::~MapReporter() { stop(); }

void MapReporter::start(const std::string& host, int port) {
    if (running_.load()) return;
    host_ = host;
    port_ = port;
    running_ = true;
    thread_ = std::thread(&MapReporter::senderLoop, this);
}

void MapReporter::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();  // wake senderLoop in case it's waiting on the queue
    if (thread_.joinable()) thread_.join();

    std::lock_guard<std::mutex> lg(mtx_);
    queue_.clear();
}

void MapReporter::send(const std::string& jsonLine) {
    std::lock_guard<std::mutex> lg(mtx_);
    if (queue_.size() >= kMaxQueued) queue_.pop_front();  // drop the oldest
    queue_.push_back(jsonLine);
    cv_.notify_one();
}

void MapReporter::closeSocket() {
#if defined(_WIN32)
    if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; }
#else
    if (sock_ >= 0) { close(sock_); sock_ = -1; }
#endif
}

bool MapReporter::connectOnce() {
    closeSocket();

#if defined(_WIN32)
    sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;
    u_long nb = 1;
    ioctlsocket(sock_, FIONBIO, &nb);
#else
    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) return false;
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        closeSocket();
        return false;  // invalid address -- no point retrying this one
    }

    int r = ::connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool connected = false;
    if (r == 0) {
        connected = true;
    }
    else {
#if defined(_WIN32)
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set wfd; FD_ZERO(&wfd); FD_SET(sock_, &wfd);
            timeval tv{2, 0};  // 2s timeout to establish the connection
            if (select(0, nullptr, &wfd, nullptr, &tv) > 0) connected = true;
        }
#else
        if (errno == EINPROGRESS) {
            fd_set wfd; FD_ZERO(&wfd); FD_SET(sock_, &wfd);
            timeval tv{2, 0};
            if (select(sock_ + 1, nullptr, &wfd, nullptr, &tv) > 0) {
                int err = 0; socklen_t len = sizeof(err);
                getsockopt(sock_, SOL_SOCKET, SO_ERROR, &err, &len);
                connected = (err == 0);
            }
        }
#endif
    }

    if (!connected) { closeSocket(); return false; }

    // Back to a blocking socket -- sending individual short JSON lines
    // with a blocking write/send is fine (we're not on the caller's
    // thread), and it simplifies error handling compared to keeping the
    // non-blocking state for the whole lifetime of the connection.
#if defined(_WIN32)
    u_long nb0 = 0;
    ioctlsocket(sock_, FIONBIO, &nb0);
#else
    fcntl(sock_, F_SETFL, flags);
#endif
    return true;
}

void MapReporter::senderLoop() {
    bool connected = false;
    int backoffMs = 500;
    const int maxBackoffMs = 5000;

    while (running_.load()) {
        if (!connected) {
            connected = connectOnce();
            if (!connected) {
                // Short 100ms steps so stop() doesn't have to wait out the
                // whole backoff -- it reacts practically immediately.
                for (int waited = 0; waited < backoffMs && running_.load(); waited += 100) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                backoffMs = std::min(backoffMs * 2, maxBackoffMs);
                continue;
            }
            backoffMs = 500;  // successful connection -- reset backoff for next outage
        }

        std::string line;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait_for(lock, std::chrono::milliseconds(500), [&] {
                return !queue_.empty() || !running_.load();
            });
            if (!running_.load()) break;
            if (queue_.empty()) continue;  // just a periodic running_ check
            line = std::move(queue_.front());
            queue_.pop_front();
        }

        line += "\n";
#if defined(_WIN32)
        int sent = ::send(sock_, line.c_str(), static_cast<int>(line.size()), 0);
        bool ok = (sent == static_cast<int>(line.size()));
#else
        ssize_t sent = ::send(sock_, line.c_str(), line.size(), 0);
        bool ok = (sent == static_cast<ssize_t>(line.size()));
#endif
        if (!ok) {
            // Connection dropped -- the line is lost (fire-and-forget,
            // same as the TCP collector on the receiving end has no
            // acknowledgement either), retry connecting on the next pass.
            connected = false;
        }
    }

    closeSocket();
}
