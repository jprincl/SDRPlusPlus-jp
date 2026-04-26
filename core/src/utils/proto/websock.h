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

        // Stops the receive loop and closes the underlying socket. Safe to
        // call from any thread, including from inside a callback.
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
        enum WebSocketFrameType : int;

        static constexpr int64_t MAX_FRAME_PAYLOAD = 1024 * 1024;

        std::shared_ptr<::net::Socket> socket;
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

        int makeFrame(WebSocketFrameType frame_type, unsigned char* msg, int msg_length,
                      unsigned char* buffer, int buffer_size);
        WebSocketFrameType getFrame(unsigned char* in_buffer, int in_length,
                                    unsigned char* out_buffer, int out_size,
                                    int* out_length, int* skipSize);

        std::shared_ptr<::net::Socket> connectSocket(const std::string& host, int port);
        void sendAllRaw(const uint8_t* data, size_t len);
        void sendPong();
        void sendClose();
    };

}
