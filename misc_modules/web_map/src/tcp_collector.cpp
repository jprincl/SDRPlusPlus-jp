#include "tcp_collector.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
    // SOCKET/INVALID_SOCKET etc. already come from tcp_collector.h (winsock2.h)
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

TcpCollector::~TcpCollector() { stop(); }

std::string TcpCollector::start(const std::string& host, int port, ObjectHandler onObject) {
    if (running_.load()) return "";
    onObject_ = std::move(onObject);

#if defined(_WIN32)
    listenSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock_ == INVALID_SOCKET) return "socket() failed";
#else
    listenSock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock_ < 0) return std::string("socket() failed: ") + strerror(errno);
#endif

    int yes = 1;
    ::setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    else if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = INADDR_ANY;  // invalid address -> safe fallback
    }

    if (::bind(listenSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#if defined(_WIN32)
        std::string err = "bind failed (port busy?)";
        closesocket(listenSock_);
        listenSock_ = INVALID_SOCKET;
#else
        std::string err = std::string("bind failed: ") + strerror(errno);
        close(listenSock_);
        listenSock_ = -1;
#endif
        return err;
    }

    if (::listen(listenSock_, 16) != 0) {
#if defined(_WIN32)
        std::string err = "listen failed";
        closesocket(listenSock_);
        listenSock_ = INVALID_SOCKET;
#else
        std::string err = std::string("listen failed: ") + strerror(errno);
        close(listenSock_);
        listenSock_ = -1;
#endif
        return err;
    }

    running_ = true;
    acceptThread_ = std::thread(&TcpCollector::acceptLoop, this);
    return "";
}

void TcpCollector::stop() {
    bool wasRunning = running_.exchange(false);

#if defined(_WIN32)
    if (listenSock_ != INVALID_SOCKET) {
        closesocket(listenSock_);  // unblocks accept()
        listenSock_ = INVALID_SOCKET;
    }
#else
    if (listenSock_ >= 0) {
        shutdown(listenSock_, SHUT_RDWR);
        close(listenSock_);  // unblocks accept()
        listenSock_ = -1;
    }
#endif
    if (acceptThread_.joinable()) acceptThread_.join();

    // force-close every live client socket -- unblocks their recv()
    {
        std::lock_guard<std::mutex> lg(clientsMutex_);
        for (auto fd : clientSockets_) {
#if defined(_WIN32)
            shutdown(fd, SD_BOTH);
            closesocket(fd);
#else
            shutdown(fd, SHUT_RDWR);
            close(fd);
#endif
        }
        clientSockets_.clear();
    }
    for (auto& t : clientThreads_) {
        if (t.joinable()) t.join();
    }
    clientThreads_.clear();
    clientCount_ = 0;

    (void)wasRunning;
}

void TcpCollector::acceptLoop() {
    while (running_.load()) {
        sockaddr_in peer{};
#if defined(_WIN32)
        int peerLen = sizeof(peer);
        SOCKET fd = ::accept(listenSock_, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (fd == INVALID_SOCKET) {
            if (!running_.load()) break;  // stop() just closed listenSock_
            continue;                      // transient error, try again
        }
#else
        socklen_t peerLen = sizeof(peer);
        int fd = ::accept(listenSock_, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (fd < 0) {
            if (!running_.load()) break;
            continue;
        }
#endif
        std::lock_guard<std::mutex> lg(clientsMutex_);
        clientSockets_.push_back(fd);
        clientThreads_.emplace_back(&TcpCollector::clientLoop, this, fd);
    }
}

void TcpCollector::clientLoop(
#if defined(_WIN32)
    SOCKET fd
#else
    int fd
#endif
) {
    clientCount_++;
    std::string buf;
    char chunk[4096];

    while (running_.load()) {
#if defined(_WIN32)
        int n = ::recv(fd, chunk, sizeof(chunk), 0);
#else
        ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
#endif
        if (n <= 0) break;  // connection closed or error
        buf.append(chunk, static_cast<size_t>(n));

        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && onObject_) onObject_(line);
        }
    }

#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
    clientCount_--;

    // Remove our own socket from the list for force-close in stop() --
    // the thread does NOT remove itself from clientThreads_ (that's only
    // safe from stop(), which then also calls join() on it).
    std::lock_guard<std::mutex> lg(clientsMutex_);
    auto it = std::find(clientSockets_.begin(), clientSockets_.end(), fd);
    if (it != clientSockets_.end()) clientSockets_.erase(it);
}
