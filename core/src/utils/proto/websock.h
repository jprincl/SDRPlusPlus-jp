#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace net { class Socket; }

namespace net::websock {

    struct WSClient {
        WSClient();
        ~WSClient();
        WSClient(const WSClient&) = delete;
        WSClient& operator=(const WSClient&) = delete;

        // RFC 6455 client handshake followed by the receive loop. Drives the
        // on*Message / onConnected / onDisconnected / onEveryReceive callbacks
        // until stopSocket() is called or the peer closes. Throws on protocol
        // error; the caller is expected to surface that to the user.
        void connectAndReceiveLoop(const std::string& host, int port, const std::string& path);

        // Both sends are thread-safe (serialized by an internal mutex) and
        // perform short-write retry until the full frame is on the wire.
        void sendString(const std::string& str);
        void sendBinary(const std::vector<uint8_t>& data);

        // Signals the receive loop to exit. Safe to call from any thread,
        // including from inside a callback. The loop notices `stopped` on its
        // next 100 ms recv-timeout tick, then closes the socket under the
        // same mutex used by send/recv.
        void stopSocket();

        // External "user asked stop" flag; reset to false before calling
        // connectAndReceiveLoop() again on the same instance.
        std::atomic<bool> stopped{false};

        std::function<void(const std::string&)> onTextMessage   = [](auto){};
        std::function<void(const std::string&)> onBinaryMessage = [](auto){};
        std::function<void()> onConnected     = []{};
        std::function<void()> onDisconnected  = []{};
        std::function<void()> onEveryReceive  = []{};

    private:
        // Classification of an inbound frame returned by getFrame(). These
        // values are sentinels only; they never appear on the wire. Outgoing
        // wire bytes live as WIRE_* constants in the .cpp.
        enum class Inbound : int;

        static constexpr int64_t MAX_FRAME_PAYLOAD = 1024 * 1024;

        // socketMutex guards both pointer lifetime and net::Socket method
        // calls because net::Socket keeps non-atomic state internally.
        std::unique_ptr<::net::Socket> socket;
        std::mutex socketMutex;

        std::string path;
        std::random_device rd;
        std::default_random_engine e1;
        std::uniform_int_distribution<int> uniform_dist;
        std::mutex sendMutex;

        std::string secKey;
        std::string fragmentBuffer;
        int fragmentOpcode = 0;
        int count = 0;

        int maybeDecodeBuffer(const std::vector<uint8_t>& data);

        int makeFrame(uint8_t firstByte, unsigned char* msg, int msg_length,
                      unsigned char* buffer, int buffer_size);
        Inbound getFrame(unsigned char* in_buffer, int in_length,
                         unsigned char* out_buffer, int out_size,
                         int* out_length, int* skipSize);

        std::unique_ptr<::net::Socket> connectSocket(const std::string& host, int port);
        void setSocket(std::unique_ptr<::net::Socket> s);
        void closeCurrentSocket(bool markStopped);
        int recvSocket(uint8_t* data, size_t len, int timeout);
        // Handshake path: takes sendMutex, then writes raw bytes (no frame wrapping).
        void sendAllRaw(const uint8_t* data, size_t len);
        // Caller must hold sendMutex. Pure short-write retry loop.
        void sendAllRawLocked(const uint8_t* data, size_t len);
        // Builds a websocket frame and writes it under sendMutex. The mutex
        // covers makeFrame() too, since makeFrame mutates the shared random
        // engine while generating mask bytes.
        void sendFrame(uint8_t firstByte, const uint8_t* payload, size_t len);
        void sendPong(const uint8_t* payload, size_t len);
        void sendClose();
    };

}
