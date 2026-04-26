#pragma once
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include "../base64.h"
#include "../net.h"
#include "../url.h"
#include "http.h"
#include "picohash.h"
#include <cctype>
#include <stdio.h>
#include <random>
#include <stdexcept>
#include <vector>
#include <utils/flog.h>

namespace net::websock {

    inline std::string sha1Base64(const std::string& in) {
        _picohash_sha1_ctx_t ctx;
        _picohash_sha1_init(&ctx);
        _picohash_sha1_update(&ctx, in.data(), in.size());
        uint8_t digest[PICOHASH_SHA1_DIGEST_LENGTH];
        _picohash_sha1_final(&ctx, digest);
        return base64::encode(digest, PICOHASH_SHA1_DIGEST_LENGTH);
    }

    inline void splitStringV(const std::string& input, const std::string& delimiter, std::vector<std::string>& output) {
        output.clear();
        size_t start = 0;
        while (start <= input.size()) {
            size_t end = input.find(delimiter, start);
            if (end == std::string::npos) {
                output.push_back(input.substr(start));
                break;
            }
            output.push_back(input.substr(start, end - start));
            start = end + delimiter.size();
        }
    }

    struct WSClient {
        std::shared_ptr<::net::Socket> socket;
        std::string path;
        std::random_device rd;
        std::default_random_engine e1;
        std::uniform_int_distribution<int> uniform_dist;
        std::atomic<bool> stopped{false};
        static constexpr int64_t MAX_FRAME_PAYLOAD = 1024 * 1024;

        std::string secKey;        // generated per upgrade attempt
        int fragmentOpcode = 0;    // 0=none, 1=text, 2=binary
        std::string fragmentBuffer;
        std::mutex sendMutex;      // serializes concurrent senders on this->socket

        WSClient() : socket(), rd(), e1(rd()), uniform_dist(0, 255) {
            flog::info("WSClient instance: {}", (uint64_t)(uintptr_t)this);
        }

        int count = 0;

        int maybeDecodeBuffer(const std::vector<uint8_t> &data) {
            std::string buffer;
            if (data.empty()) {
                return 0;
            }
            buffer.resize(data.size() + 200, ' ');
            int outLen = 0;
            int skipSize = 0;
            int frameType = getFrame((unsigned char*)data.data(), (int)data.size(), (unsigned char*)buffer.data(), (int)buffer.length(), &outLen, &skipSize);
//            printf("(%d) Handling frame type: %x, skipsize = %d ...\n", count, frameType, skipSize);
            auto appendFragment = [&]() {
                if (outLen > 1) {
                    if (fragmentBuffer.size() + (size_t)(outLen - 1) > (size_t)MAX_FRAME_PAYLOAD) {
                        throw std::runtime_error("websock: fragmented message too large");
                    }
                    fragmentBuffer.append(buffer.data(), outLen - 1);
                }
            };
            switch(frameType) {
            case TEXT_FRAME:
                if (fragmentOpcode != 0) {
                    throw std::runtime_error("websock: TEXT frame received while fragment in progress");
                }
                if (outLen > 3) {
                    buffer.resize(outLen - 1);
                    onTextMessage(buffer);
                }
                break;
            case BINARY_FRAME:
                if (fragmentOpcode != 0) {
                    throw std::runtime_error("websock: BINARY frame received while fragment in progress");
                }
                if (outLen > 3) {
                    buffer.resize(outLen - 1);
                    onBinaryMessage(buffer);
                }
                break;
            case INCOMPLETE_TEXT_FRAME:
                if (fragmentOpcode != 0) {
                    throw std::runtime_error("websock: TEXT fragment-start while fragment in progress");
                }
                fragmentOpcode = 1;
                appendFragment();
                break;
            case INCOMPLETE_BINARY_FRAME:
                if (fragmentOpcode != 0) {
                    throw std::runtime_error("websock: BINARY fragment-start while fragment in progress");
                }
                fragmentOpcode = 2;
                appendFragment();
                break;
            case CONTINUATION_FRAGMENT:
                if (fragmentOpcode == 0) {
                    throw std::runtime_error("websock: CONTINUATION frame without fragment in progress");
                }
                appendFragment();
                break;
            case CONTINUATION_FINAL:
                if (fragmentOpcode == 0) {
                    throw std::runtime_error("websock: CONTINUATION-final frame without fragment in progress");
                }
                appendFragment();
                if (fragmentBuffer.size() > 2) {
                    if (fragmentOpcode == 1) { onTextMessage(fragmentBuffer); }
                    else if (fragmentOpcode == 2) { onBinaryMessage(fragmentBuffer); }
                }
                fragmentOpcode = 0;
                fragmentBuffer.clear();
                break;
            case INCOMPLETE_FRAME:
                return 0;
            case ERROR_FRAME:
                throw std::runtime_error("websock: invalid frame");
            case PING_FRAME:
                sendPong();
                break;
            case CLOSE_FRAME:
                sendClose();
                stopped = true;
                break;
            default:
                printf("Unknown frame type: %x\n", frameType);
                break;
            }
            count++;
            return skipSize;
        }

        // Loop over Socket::send to handle short writes and serialize concurrent
        // senders. Throws if the socket has been closed (or closes mid-send).
        void sendAllRaw(const uint8_t* data, size_t len) {
            std::lock_guard<std::mutex> lock(sendMutex);
            auto sock = socket;
            if (!sock) {
                throw std::runtime_error("websock: send on closed socket");
            }
            size_t sent = 0;
            while (sent < len) {
                int n = sock->send(data + sent, len - sent);
                if (n > 0) {
                    sent += (size_t)n;
                    continue;
                }
                if (!sock->isOpen()) {
                    throw std::runtime_error("websock: send failed, socket closed");
                }
                // Non-blocking socket reported WOULD_BLOCK; brief backoff and retry.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void sendPong() {
            std::string str = " ";
            std::string buffer;
            buffer.resize(200, ' ');
            int len = makeFrame(PONG_FRAME, (unsigned char*)str.c_str(), 0, (unsigned char *)buffer.data(), buffer.size());
            sendAllRaw((const uint8_t*)buffer.data(), (size_t)len);
        }

        void sendClose() {
            std::string buffer;
            buffer.resize(200, ' ');
            int len = makeFrame(CLOSE_FRAME, nullptr, 0, (unsigned char *)buffer.data(), buffer.size());
            sendAllRaw((const uint8_t*)buffer.data(), (size_t)len);
        }

        void sendString(const std::string &str) {
            std::string buffer;
            buffer.resize(str.length() + 200, ' ');
            int len = makeFrame(TEXT_FRAME, (unsigned char*)str.c_str(), str.length(), (unsigned char *)buffer.data(), buffer.size());
            sendAllRaw((const uint8_t*)buffer.data(), (size_t)len);
//            flog::info("<= {}", str);
        }

        void sendBinary(const std::vector<uint8_t>& data) {
            std::string buffer;
            buffer.resize(data.size() + 200, ' ');
            int len = makeFrame(BINARY_FRAME, (unsigned char*)data.data(), data.size(), (unsigned char *)buffer.data(), buffer.size());
            sendAllRaw((const uint8_t*)buffer.data(), (size_t)len);
        }

        // PARTS were taken from https://github.com/katzarsky/WebSocket

        enum WebSocketFrameType {
            ERROR_FRAME=0xFF00,
            INCOMPLETE_FRAME=0xFE00,

            OPENING_FRAME=0x3300,
            CLOSING_FRAME=0x3400,

            INCOMPLETE_TEXT_FRAME=0x01,
            INCOMPLETE_BINARY_FRAME=0x02,
            CONTINUATION_FRAGMENT=0x03,
            CONTINUATION_FINAL=0x04,

            TEXT_FRAME=0x81,
            BINARY_FRAME=0x82,
            CLOSE_FRAME=0x88,

            PING_FRAME=0x19,
            PONG_FRAME=0x1A
        };

        int makeFrame(WebSocketFrameType frame_type, unsigned char* msg, int msg_length, unsigned char* buffer, int buffer_size)
        {
            int pos = 0;
            int size = msg_length;
            buffer[pos++] = (unsigned char)frame_type; // fin included

            if(size <= 125) {
                buffer[pos++] = size; // set mask bit
            }
            else if(size <= 65535) {
                buffer[pos++] = 126; //16 bit length follows

                buffer[pos++] = (size >> 8) & 0xFF; // leftmost first
                buffer[pos++] = size & 0xFF;
            }
            else { // >2^16-1 (65535)
                buffer[pos++] = 127; //64 bit length follows

                // write 8 bytes length (significant first)

                // since msg_length is int it can be no longer than 4 bytes = 2^32-1
                // padd zeroes for the first 4 bytes
                for(int i=3; i>=0; i--) {
                    buffer[pos++] = 0;
                }
                // write the actual 32bit msg_length in the next 4 bytes
                for(int i=3; i>=0; i--) {
                    buffer[pos++] = ((size >> 8*i) & 0xFF);
                }
            }
            if (frame_type != PONG_FRAME) {
                buffer[1] |= 0x80; // set mask bit
                auto maskIndex = pos;
                buffer[pos++] = uniform_dist(e1);
                buffer[pos++] = uniform_dist(e1);
                buffer[pos++] = uniform_dist(e1);
                buffer[pos++] = uniform_dist(e1);
                if (size > 0) {
                    memcpy((void*)(buffer+pos), msg, size);
                }
                for(int q=0; q<size; q++) {
                    buffer[pos+q] ^= buffer[maskIndex + (q%4)];
                }
            }
            return (size+pos);
        }

        WebSocketFrameType getFrame(unsigned char* in_buffer, int in_length, unsigned char* out_buffer, int out_size, int* out_length, int *skipSize)
        {
            //printf("getTextFrame()\n");
            if(in_length < 2) return INCOMPLETE_FRAME;

            unsigned char msg_opcode = in_buffer[0] & 0x0F;
            unsigned char msg_fin = (in_buffer[0] >> 7) & 0x01;
            unsigned char msg_masked = (in_buffer[1] >> 7) & 0x01;

            // *** message decoding

            int64_t payload_length = 0;
            int pos = 2;
            int length_field = in_buffer[1] & (~0x80);
            unsigned int mask = 0;

            //printf("IN:"); for(int i=0; i<20; i++) printf("%02x ",buffer[i]); printf("\n");

            if(length_field <= 125) {
                payload_length = length_field;
            }
            else if(length_field == 126) { //msglen is 16bit!
                if(in_length < pos + 2) {
                    return INCOMPLETE_FRAME;
                }
                //payload_length = in_buffer[2] + (in_buffer[3]<<8);
                payload_length = (
                    ((int64_t)in_buffer[2] << 8) |
                    ((int64_t)in_buffer[3])
                );
                pos += 2;
            }
            else if(length_field == 127) { //msglen is 64bit!
                if(in_length < pos + 8) {
                    return INCOMPLETE_FRAME;
                }
                payload_length = (
                    ((int64_t)in_buffer[2] << 56) |
                    ((int64_t)in_buffer[3] << 48) |
                    ((int64_t)in_buffer[4] << 40) |
                    ((int64_t)in_buffer[5] << 32) |
                    ((int64_t)in_buffer[6] << 24) |
                    ((int64_t)in_buffer[7] << 16) |
                    ((int64_t)in_buffer[8] << 8) |
                    ((int64_t)in_buffer[9])
                );
                pos += 8;
            }

            //printf("PAYLOAD_LEN: %08x\n", payload_length);
            if(payload_length < 0) {
                return ERROR_FRAME;
            }
            if(payload_length > MAX_FRAME_PAYLOAD) {
                flog::error("ERROR: websocket payload is too large: {}", payload_length);
                return ERROR_FRAME;
            }

            if(msg_masked && in_length < pos + 4) {
                return INCOMPLETE_FRAME;
            }

            if(msg_masked) {
                mask = *((unsigned int*)(in_buffer+pos));
                //printf("MASK: %08x\n", mask);
                pos += 4;

                if(payload_length > in_length - pos) {
                    return INCOMPLETE_FRAME;
                }

                // unmask data:
                unsigned char* c = in_buffer+pos;
                for(int i=0; i<payload_length; i++) {
                    c[i] = c[i] ^ ((unsigned char*)(&mask))[i%4];
                }
            }

            if(payload_length > in_length - pos) {
                return INCOMPLETE_FRAME;
            }

            if(payload_length > out_size - 1) {
                flog::error("ERROR: output buffer is too small for the payload");
                return ERROR_FRAME;
            }

            memcpy((void*)out_buffer, (void*)(in_buffer+pos), payload_length);
            out_buffer[payload_length] = 0;
            *out_length = payload_length+1;
            *skipSize = payload_length+pos;

            //printf("TEXT: %s\n", out_buffer);

            if(msg_opcode >= 0x8) {
                if(!msg_fin || payload_length > 125) { return ERROR_FRAME; }
                if(msg_opcode == 0x8 && payload_length == 1) { return ERROR_FRAME; }
            }

            if(msg_opcode == 0x0) return (msg_fin)?CONTINUATION_FINAL:CONTINUATION_FRAGMENT;
            if(msg_opcode == 0x1) return (msg_fin)?TEXT_FRAME:INCOMPLETE_TEXT_FRAME;
            if(msg_opcode == 0x2) return (msg_fin)?BINARY_FRAME:INCOMPLETE_BINARY_FRAME;
            if(msg_opcode == 0x8) return CLOSE_FRAME;
            if(msg_opcode == 0x9) return PING_FRAME;
            if(msg_opcode == 0xA) return PONG_FRAME;

            return ERROR_FRAME;
        }

        std::function<void(const std::string&)> onTextMessage = [](auto){};
        std::function<void(const std::string&)> onBinaryMessage = [](auto){};
        std::function<void()> onConnected = [](){};
        std::function<void()> onDisconnected = [](){};
        std::function<void()> onEveryReceive = [](){};

        void closeRawSocket(::net::SockHandle_t sock) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
        }

        std::shared_ptr<::net::Socket> connectSocket(const std::string& host, int port) {
            ::net::Address addr(host, port);
            ::net::SockHandle_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
            if (sock == INVALID_SOCKET) {
#else
            if (sock < 0) {
#endif
                throw std::runtime_error("Could not create socket");
            }

#ifdef _WIN32
            u_long enabled = 1;
            ioctlsocket(sock, FIONBIO, &enabled);
#else
            fcntl(sock, F_SETFL, O_NONBLOCK);
#endif

            int result = ::connect(sock, (sockaddr*)&addr.addr, sizeof(sockaddr_in));
            if (result) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
#else
                if (errno != EINPROGRESS) {
#endif
                    closeRawSocket(sock);
                    throw std::runtime_error("Could not connect");
                }
            }

            while (!stopped) {
                fd_set writeSet;
                fd_set errorSet;
                FD_ZERO(&writeSet);
                FD_ZERO(&errorSet);
                FD_SET(sock, &writeSet);
                FD_SET(sock, &errorSet);

                timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000;
#ifdef _WIN32
                int ready = select(0, NULL, &writeSet, &errorSet, &tv);
#else
                int ready = select(sock + 1, NULL, &writeSet, &errorSet, &tv);
#endif
                if (ready < 0) {
                    closeRawSocket(sock);
                    throw std::runtime_error("Could not connect");
                }
                if (ready == 0) {
                    continue;
                }

                int connectError = 0;
#ifdef _WIN32
                int connectErrorLen = sizeof(connectError);
#else
                socklen_t connectErrorLen = sizeof(connectError);
#endif
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&connectError, &connectErrorLen) || connectError) {
                    closeRawSocket(sock);
                    throw std::runtime_error("Could not connect");
                }
                return std::make_shared<::net::Socket>(sock, &addr);
            }

            closeRawSocket(sock);
            return {};
        }

        static int parseStatusCode(const std::string& statusLine) {
            auto sp1 = statusLine.find(' ');
            if (sp1 == std::string::npos) return -1;
            auto sp2 = statusLine.find(' ', sp1 + 1);
            std::string code = (sp2 == std::string::npos)
                ? statusLine.substr(sp1 + 1)
                : statusLine.substr(sp1 + 1, sp2 - sp1 - 1);
            try { return std::stoi(code); } catch (...) { return -1; }
        }

        static std::string findHeaderValue(const std::vector<std::string>& headers, const std::string& name) {
            for (const auto& h : headers) {
                auto colon = h.find(':');
                if (colon == std::string::npos || colon != name.size()) continue;
                bool match = true;
                for (size_t i = 0; i < name.size(); i++) {
                    if (std::tolower((unsigned char)h[i]) != std::tolower((unsigned char)name[i])) { match = false; break; }
                }
                if (!match) continue;
                auto val = h.substr(colon + 1);
                auto start = val.find_first_not_of(" \t");
                return start == std::string::npos ? std::string() : val.substr(start);
            }
            return std::string();
        }

        static bool equalsIgnoreAsciiCase(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) { return false; }
            for (size_t i = 0; i < a.size(); i++) {
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) {
                    return false;
                }
            }
            return true;
        }

        void connectAndReceiveLoop(const std::string& host, int port, const std::string& path) {
            flog::info("WSClient connectAndReceiveLoop: inst={}", (uint64_t)(uintptr_t)this);

            std::string currentHost = host;
            int currentPort = port;
            std::string currentPath = path;
            constexpr int MAX_REDIRECTS = 5;
            int redirectsLeft = MAX_REDIRECTS;

            bool shouldNotifyDisconnected = true;
            std::vector<uint8_t> buf(100000);
            std::vector<uint8_t> data;
            int recvd = 0;

            while (true) {
                auto z = connectSocket(currentHost, currentPort);
                if (!z) {
                    onDisconnected();
                    return;
                }
                socket = z;
                if (stopped) {
                    socket->close();
                    socket.reset();
                    onDisconnected();
                    return;
                }
                flog::info("WSClient socket connected to {}:{}", currentHost, currentPort);

                {
                    uint8_t keyBytes[16];
                    for (int i = 0; i < 16; i++) { keyBytes[i] = (uint8_t)uniform_dist(e1); }
                    secKey = base64::encode(keyBytes, 16);
                }
                fragmentOpcode = 0;
                fragmentBuffer.clear();

                std::string initHeaders =
                    "Accept-Encoding: gzip, deflate\r\n"
                    "Accept-Language: en-US,en\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Connection: Upgrade\r\n"
                    "Cookie: ident=\r\n"
                    "Pragma: no-cache\r\n"
                    "Sec-GPC: 1\r\n"
                    "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
                    "Sec-WebSocket-Version: 13\r\n"
                    "Upgrade: websocket\r\n"
                    "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/108.0.0.0 Safari/537.36\r\n";
                initHeaders = "GET " + currentPath + " HTTP/1.1\r\n" + initHeaders;
                initHeaders += "Sec-WebSocket-Key: " + secKey + "\r\n";
                // RFC 7230 §5.4: omit the port from Host/Origin when it matches the scheme default,
                // otherwise reverse proxies (e.g. *.proxy.kiwisdr.com) fail to match server_name.
                const std::string hostHeader = (currentPort == 80) ? currentHost : (currentHost + ":" + std::to_string(currentPort));
                initHeaders += "Host: " + hostHeader + "\r\n";
                initHeaders += "Origin: http://" + hostHeader + "\r\n";
                initHeaders += "\r\n";

                sendAllRaw((const uint8_t*)initHeaders.data(), initHeaders.size());
                int senderr = errno;
                flog::info("sent {} bytes of upgrade request", (int64_t)initHeaders.size());

                // Accumulate the upgrade response until we see the \r\n\r\n header terminator.
                // The response can be split across multiple TCP segments (e.g. direct KiwiSDRs
                // send the 101 in fragments), so a single recv is not enough.
                recvd = 0;
                size_t pos = std::string::npos;
                while (!stopped) {
                    if ((size_t)recvd + 1 >= buf.size()) {
                        socket->close();
                        throw std::runtime_error("websock: upgrade response headers too large");
                    }
                    int n = socket->recv(buf.data() + recvd, buf.size() - 1 - recvd, false, 100);
                    if (n < 0) {
                        std::string msg = "websock: recv failed, errno=" + std::to_string(errno)+" (recvd="+std::to_string(recvd)+
                                          " senderr="+std::to_string(senderr)+")";
                        socket->close();
                        throw std::runtime_error(msg);
                    }
                    if (n == 0) {
                        continue; // recv timeout; keep waiting until stopped
                    }
                    recvd += n;
                    buf[recvd] = 0;
                    std::string view((char*)buf.data(), recvd);
                    pos = view.find("\r\n\r\n");
                    if (pos != std::string::npos) {
                        break;
                    }
                }
                if (stopped) {
                    socket->close();
                    onDisconnected();
                    return;
                }
                flog::info("recvd: {}", recvd);
                std::vector<std::string> recvHeaders;
                std::string bufs((char*)buf.data(), recvd);
                bufs.resize(pos + 2);
                splitStringV(bufs, "\r\n", recvHeaders);
                for (size_t i = 0; i < recvHeaders.size(); i++) {
                    printf("%s\n", recvHeaders[i].c_str());
                }

                int status = recvHeaders.empty() ? -1 : parseStatusCode(recvHeaders[0]);
                if (status == 101) {
                    std::string upgradeHdr = findHeaderValue(recvHeaders, "Upgrade");
                    if (!equalsIgnoreAsciiCase(upgradeHdr, "websocket")) {
                        socket->close();
                        throw std::runtime_error("websock: handshake failed: missing or wrong Upgrade header (got '" + upgradeHdr + "')");
                    }
                    std::string accept = findHeaderValue(recvHeaders, "Sec-WebSocket-Accept");
                    std::string expected = sha1Base64(secKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
                    if (accept != expected) {
                        socket->close();
                        throw std::runtime_error("websock: handshake failed: bad Sec-WebSocket-Accept (got '" + accept + "', expected '" + expected + "')");
                    }
                    data.clear();
                    for (int i = (int)pos + 4; i < recvd; i++) {
                        data.push_back(buf[i]);
                    }
                    break;
                }

                const bool isRedirect = (status == 301 || status == 302 || status == 303 ||
                                         status == 307 || status == 308);
                if (isRedirect && redirectsLeft > 0) {
                    std::string location = findHeaderValue(recvHeaders, "Location");
                    if (location.empty()) {
                        socket->close();
                        throw std::runtime_error("websock: redirect without Location header");
                    }
                    auto parsed = url::parseHttpHostPort(location);
                    if (!parsed) {
                        socket->close();
                        throw std::runtime_error("websock: cannot parse redirect Location: " + location);
                    }
                    socket->close();
                    socket.reset();
                    std::string redirectPath = parsed->path;
                    if (redirectPath != currentPath && equalsIgnoreAsciiCase(redirectPath, currentPath)) {
                        flog::info("websock: preserving redirect path case: {} -> {}", redirectPath, currentPath);
                        redirectPath = currentPath;
                    }
                    flog::info("websock: following redirect ({} left) to {}:{}{}", redirectsLeft, parsed->host, parsed->port, redirectPath);
                    currentHost = parsed->host;
                    currentPort = parsed->port;
                    currentPath = redirectPath;
                    redirectsLeft--;
                    continue;
                }

                socket->close();
                throw std::runtime_error("websock: upgrade failed: " +
                    (recvHeaders.empty() ? std::string("(no status line)") : recvHeaders[0]));
            }

            onConnected();
            while (!stopped) {
                int len0 = 0;
                try {
                    len0 = maybeDecodeBuffer(data);
                }
                catch (...) {
                    socket->close();
                    onDisconnected();
                    shouldNotifyDisconnected = false;
                    throw;
                }
                if (len0 > 0) {
//                    printf("decoded/dropping bytes: %d\n", len0);
                    data.erase(data.begin(), data.begin() + len0);
                    continue;
                }
                recvd = socket->recv(buf.data(), buf.size(), false, 100); // 100 msec
                if (stopped) {
                    break;
                }
                if (recvd == 0) {
                    continue;
                }
//                printf("recvd bytes in loop: %d\n", recvd);
                if (recvd <= 0) {
                    socket->close();
                    onDisconnected();
                    shouldNotifyDisconnected = false;
                    break;
                }
                onEveryReceive();
                if (data.size() + recvd > MAX_FRAME_PAYLOAD + 16) {
                    socket->close();
                    onDisconnected();
                    shouldNotifyDisconnected = false;
                    throw std::runtime_error("websock: frame too large");
                }
                for (int i = 0; i < recvd; i++) {
                    data.push_back(buf[i]);
                }
            }
            if (shouldNotifyDisconnected) {
                socket->close();
                onDisconnected();
            }
        }
        void stopSocket() {
            stopped = true;
            if (socket) {
                socket->close();
            }
        }
    };

}
