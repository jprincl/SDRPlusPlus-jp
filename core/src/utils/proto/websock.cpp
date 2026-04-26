#include "websock.h"

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <stdio.h>
#include <thread>
#include <utility>

#include "../base64.h"
#include "../net.h"
#include "../url.h"
#include "picohash.h"
#include <utils/flog.h>

namespace net::websock {

    // Inbound classification returned by getFrame(). These values never go on
    // the wire — they only tell maybeDecodeBuffer() what the decoded frame is.
    enum class WSClient::Inbound : int {
        Text,
        Binary,
        IncompleteText,     // first fragment of a TEXT message (FIN=0, opcode=1)
        IncompleteBinary,   // first fragment of a BINARY message (FIN=0, opcode=2)
        Continuation,       // mid-fragment (FIN=0, opcode=0)
        ContinuationFinal,  // final fragment (FIN=1, opcode=0)
        Ping,
        Close,
        Pong,
        Incomplete,         // not enough bytes buffered yet
        Error,              // protocol violation
    };

    namespace {

        // Outgoing wire bytes (FIN=1, RSV=000, opcode in low nibble).
        // RFC 6455 §5.2/§5.4. Fed directly to makeFrame() as buffer[0].
        constexpr uint8_t WIRE_TEXT   = 0x81;
        constexpr uint8_t WIRE_BINARY = 0x82;
        constexpr uint8_t WIRE_CLOSE  = 0x88;
        constexpr uint8_t WIRE_PONG   = 0x8A;

        std::string sha1Base64(const std::string& in) {
            _picohash_sha1_ctx_t ctx;
            _picohash_sha1_init(&ctx);
            _picohash_sha1_update(&ctx, in.data(), in.size());
            uint8_t digest[PICOHASH_SHA1_DIGEST_LENGTH];
            _picohash_sha1_final(&ctx, digest);
            return base64::encode(digest, PICOHASH_SHA1_DIGEST_LENGTH);
        }

        void splitStringV(const std::string& input, const std::string& delimiter, std::vector<std::string>& output) {
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

        int parseStatusCode(const std::string& statusLine) {
            auto sp1 = statusLine.find(' ');
            if (sp1 == std::string::npos) return -1;
            auto sp2 = statusLine.find(' ', sp1 + 1);
            std::string code = (sp2 == std::string::npos)
                ? statusLine.substr(sp1 + 1)
                : statusLine.substr(sp1 + 1, sp2 - sp1 - 1);
            try { return std::stoi(code); } catch (...) { return -1; }
        }

        std::string findHeaderValue(const std::vector<std::string>& headers, const std::string& name) {
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

        bool equalsIgnoreAsciiCase(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) { return false; }
            for (size_t i = 0; i < a.size(); i++) {
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) {
                    return false;
                }
            }
            return true;
        }

        void closeRawSocket(::net::SockHandle_t sock) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
        }

    } // namespace

    WSClient::WSClient() : socket(), rd(), e1(rd()), uniform_dist(0, 255) {
        flog::info("WSClient instance: {}", (uint64_t)(uintptr_t)this);
    }

    WSClient::~WSClient() = default;

    void WSClient::setSocket(std::unique_ptr<::net::Socket> s) {
        std::lock_guard<std::mutex> lock(socketMutex);
        socket = std::move(s);
    }

    void WSClient::closeCurrentSocket(bool markStopped) {
        websocketReady = false;
        if (markStopped) {
            stopped = true;
        }

        std::lock_guard<std::mutex> lock(socketMutex);
        if (socket) {
            socket->close();
            socket.reset();
        }
    }

    int WSClient::recvSocket(uint8_t* data, size_t len, int timeout) {
        std::lock_guard<std::mutex> lock(socketMutex);
        if (!socket) {
            return -1;
        }

        if (stopped) {
            return 0;
        }

        int n = socket->recv(data, len, false, timeout);
        if (!socket->isOpen()) {
            stopped = true;
        }
        return n;
    }

    int WSClient::maybeDecodeBuffer(const std::vector<uint8_t>& data) {
        std::string buffer;
        if (data.empty()) {
            return 0;
        }
        buffer.resize(data.size() + 200, ' ');
        int outLen = 0;
        int skipSize = 0;
        Inbound kind = getFrame((unsigned char*)data.data(), (int)data.size(),
                                (unsigned char*)buffer.data(), (int)buffer.length(),
                                &outLen, &skipSize);
//        printf("(%d) Handling frame kind: %d, skipsize = %d ...\n", count, (int)kind, skipSize);
        auto appendFragment = [&]() {
            if (outLen > 1) {
                if (fragmentBuffer.size() + (size_t)(outLen - 1) > (size_t)MAX_FRAME_PAYLOAD) {
                    throw std::runtime_error("websock: fragmented message too large");
                }
                fragmentBuffer.append(buffer.data(), outLen - 1);
            }
        };
        switch (kind) {
        case Inbound::Text:
            if (fragmentOpcode != 0) {
                throw std::runtime_error("websock: TEXT frame received while fragment in progress");
            }
            // outLen == payload_length + 1 (trailing NUL); deliver every
            // valid message including 0/1/2-byte payloads.
            buffer.resize(outLen - 1);
            onTextMessage(buffer);
            break;
        case Inbound::Binary:
            if (fragmentOpcode != 0) {
                throw std::runtime_error("websock: BINARY frame received while fragment in progress");
            }
            buffer.resize(outLen - 1);
            onBinaryMessage(buffer);
            break;
        case Inbound::IncompleteText:
            if (fragmentOpcode != 0) {
                throw std::runtime_error("websock: TEXT fragment-start while fragment in progress");
            }
            fragmentOpcode = 1;
            appendFragment();
            break;
        case Inbound::IncompleteBinary:
            if (fragmentOpcode != 0) {
                throw std::runtime_error("websock: BINARY fragment-start while fragment in progress");
            }
            fragmentOpcode = 2;
            appendFragment();
            break;
        case Inbound::Continuation:
            if (fragmentOpcode == 0) {
                throw std::runtime_error("websock: CONTINUATION frame without fragment in progress");
            }
            appendFragment();
            break;
        case Inbound::ContinuationFinal:
            if (fragmentOpcode == 0) {
                throw std::runtime_error("websock: CONTINUATION-final frame without fragment in progress");
            }
            appendFragment();
            if (fragmentOpcode == 1) { onTextMessage(fragmentBuffer); }
            else if (fragmentOpcode == 2) { onBinaryMessage(fragmentBuffer); }
            fragmentOpcode = 0;
            fragmentBuffer.clear();
            break;
        case Inbound::Incomplete:
            return 0;
        case Inbound::Error:
            throw std::runtime_error("websock: invalid frame");
        case Inbound::Ping:
            // outLen = payload_length + 1 (getFrame appends a NUL terminator),
            // so outLen-1 is the actual ping payload size to echo.
            sendPong((const uint8_t*)buffer.data(), (size_t)(outLen - 1));
            break;
        case Inbound::Close:
            sendClose();
            stopped = true;
            break;
        case Inbound::Pong:
            // RFC 6455 §5.5.4: unsolicited Pongs are spec-allowed; this
            // client never sends Pings, so any Pong is ignored.
            break;
        }
        count++;
        return skipSize;
    }

    // Short-write retry loop. Caller MUST hold sendMutex.
    void WSClient::sendAllRawLocked(const uint8_t* data, size_t len) {
        if (stopped) {
            throw std::runtime_error("websock: send aborted, client stopped");
        }
        size_t sent = 0;
        while (sent < len) {
            if (stopped) {
                throw std::runtime_error("websock: send aborted, client stopped");
            }

            int n = 0;
            bool open = true;
            {
                std::lock_guard<std::mutex> lock(socketMutex);
                if (!socket) {
                    throw std::runtime_error("websock: send on closed socket");
                }
                if (stopped) {
                    throw std::runtime_error("websock: send aborted, client stopped");
                }
                n = socket->send(data + sent, len - sent);
                open = socket->isOpen();
                if (!open) {
                    stopped = true;
                }
            }
            if (n > 0) {
                sent += (size_t)n;
                continue;
            }
            if (!open) {
                throw std::runtime_error("websock: send failed, socket closed");
            }
            // Non-blocking socket reported WOULD_BLOCK; brief backoff and retry.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Used for the upgrade-request bytes during the handshake. No frame
    // wrapping, no mask, doesn't touch the random engine.
    void WSClient::sendAllRaw(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(sendMutex);
        sendAllRawLocked(data, len);
    }

    // Build a websocket frame and write it. The mutex MUST cover makeFrame()
    // because makeFrame() mutates the shared random engine while generating
    // mask bytes — without this the four sender entry points would race.
    void WSClient::sendFrame(uint8_t firstByte, const uint8_t* payload, size_t len) {
        std::lock_guard<std::mutex> lock(sendMutex);
        if (!websocketReady) {
            throw std::runtime_error("websock: send before websocket handshake completed");
        }
        std::string buffer;
        buffer.resize(len + 200, ' ');
        int frameLen = makeFrame(firstByte, (unsigned char*)payload, (int)len,
                                 (unsigned char*)buffer.data(), buffer.size());
        sendAllRawLocked((const uint8_t*)buffer.data(), (size_t)frameLen);
    }

    // RFC 6455 §5.5.3: a Pong sent in response to a Ping MUST carry the
    // same Application data as the Ping. getFrame() rejects control frames
    // with payload >125 bytes, so `len` is always small here.
    void WSClient::sendPong(const uint8_t* payload, size_t len) {
        sendFrame(WIRE_PONG, payload, len);
    }

    void WSClient::sendClose() {
        sendFrame(WIRE_CLOSE, nullptr, 0);
    }

    void WSClient::sendString(const std::string& str) {
        sendFrame(WIRE_TEXT, (const uint8_t*)str.data(), str.size());
//        flog::info("<= {}", str);
    }

    void WSClient::sendBinary(const std::vector<uint8_t>& data) {
        sendFrame(WIRE_BINARY, data.data(), data.size());
    }

    // PARTS were taken from https://github.com/katzarsky/WebSocket
    int WSClient::makeFrame(uint8_t firstByte, unsigned char* msg, int msg_length,
                            unsigned char* buffer, int buffer_size) {
        int pos = 0;
        int size = msg_length;
        buffer[pos++] = firstByte; // FIN/RSV/opcode — see WIRE_* constants

        if (size <= 125) {
            buffer[pos++] = size; // set mask bit
        }
        else if (size <= 65535) {
            buffer[pos++] = 126; //16 bit length follows

            buffer[pos++] = (size >> 8) & 0xFF; // leftmost first
            buffer[pos++] = size & 0xFF;
        }
        else { // >2^16-1 (65535)
            buffer[pos++] = 127; //64 bit length follows

            // write 8 bytes length (significant first)

            // since msg_length is int it can be no longer than 4 bytes = 2^32-1
            // padd zeroes for the first 4 bytes
            for (int i = 3; i >= 0; i--) {
                buffer[pos++] = 0;
            }
            // write the actual 32bit msg_length in the next 4 bytes
            for (int i = 3; i >= 0; i--) {
                buffer[pos++] = ((size >> 8 * i) & 0xFF);
            }
        }
        // RFC 6455 §5.1: a client MUST mask every frame it sends, including
        // control frames such as PONG.
        buffer[1] |= 0x80;
        auto maskIndex = pos;
        buffer[pos++] = uniform_dist(e1);
        buffer[pos++] = uniform_dist(e1);
        buffer[pos++] = uniform_dist(e1);
        buffer[pos++] = uniform_dist(e1);
        if (size > 0) {
            memcpy((void*)(buffer + pos), msg, size);
        }
        for (int q = 0; q < size; q++) {
            buffer[pos + q] ^= buffer[maskIndex + (q % 4)];
        }
        return (size + pos);
    }

    WSClient::Inbound WSClient::getFrame(unsigned char* in_buffer, int in_length,
                                         unsigned char* out_buffer, int out_size,
                                         int* out_length, int* skipSize) {
        //printf("getTextFrame()\n");
        if (in_length < 2) return Inbound::Incomplete;

        unsigned char msg_opcode = in_buffer[0] & 0x0F;
        unsigned char msg_fin = (in_buffer[0] >> 7) & 0x01;
        unsigned char msg_masked = (in_buffer[1] >> 7) & 0x01;
        if (in_buffer[0] & 0x70) {
            return Inbound::Error;
        }
        if (msg_masked) {
            return Inbound::Error;
        }

        // *** message decoding

        uint64_t payload_length = 0;
        int pos = 2;
        int length_field = in_buffer[1] & 0x7F;

        //printf("IN:"); for(int i=0; i<20; i++) printf("%02x ",buffer[i]); printf("\n");

        if (length_field <= 125) {
            payload_length = length_field;
        }
        else if (length_field == 126) { //msglen is 16bit!
            if (in_length < pos + 2) {
                return Inbound::Incomplete;
            }
            payload_length = (
                ((uint64_t)in_buffer[pos] << 8) |
                ((uint64_t)in_buffer[pos + 1])
            );
            if (payload_length < 126) {
                return Inbound::Error;
            }
            pos += 2;
        }
        else if (length_field == 127) { //msglen is 64bit!
            if (in_length < pos + 8) {
                return Inbound::Incomplete;
            }
            if (in_buffer[pos] & 0x80) {
                return Inbound::Error;
            }
            for (int i = 0; i < 8; i++) {
                payload_length = (payload_length << 8) | in_buffer[pos + i];
            }
            if (payload_length < 65536) {
                return Inbound::Error;
            }
            pos += 8;
        }

        //printf("PAYLOAD_LEN: %08x\n", payload_length);
        if (payload_length > (uint64_t)MAX_FRAME_PAYLOAD) {
            flog::error("ERROR: websocket payload is too large: {}", payload_length);
            return Inbound::Error;
        }

        if (payload_length > (uint64_t)(in_length - pos)) {
            return Inbound::Incomplete;
        }

        if (payload_length > (uint64_t)(out_size - 1)) {
            flog::error("ERROR: output buffer is too small for the payload");
            return Inbound::Error;
        }

        const int payloadSize = (int)payload_length;
        memcpy((void*)out_buffer, (void*)(in_buffer + pos), payloadSize);
        out_buffer[payloadSize] = 0;
        *out_length = payloadSize + 1;
        *skipSize = payloadSize + pos;

        //printf("TEXT: %s\n", out_buffer);

        if (msg_opcode >= 0x8) {
            if (!msg_fin || payload_length > 125) { return Inbound::Error; }
            if (msg_opcode == 0x8 && payload_length == 1) { return Inbound::Error; }
        }

        if (msg_opcode == 0x0) return msg_fin ? Inbound::ContinuationFinal : Inbound::Continuation;
        if (msg_opcode == 0x1) return msg_fin ? Inbound::Text   : Inbound::IncompleteText;
        if (msg_opcode == 0x2) return msg_fin ? Inbound::Binary : Inbound::IncompleteBinary;
        if (msg_opcode == 0x8) return Inbound::Close;
        if (msg_opcode == 0x9) return Inbound::Ping;
        if (msg_opcode == 0xA) return Inbound::Pong;

        return Inbound::Error;
    }

    std::unique_ptr<::net::Socket> WSClient::connectSocket(const std::string& host, int port) {
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
            return std::make_unique<::net::Socket>(sock, &addr);
        }

        closeRawSocket(sock);
        return {};
    }

    void WSClient::connectAndReceiveLoop(const std::string& host, int port, const std::string& path) {
        flog::info("WSClient connectAndReceiveLoop: inst={}", (uint64_t)(uintptr_t)this);

        websocketReady = false;
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
            setSocket(std::move(z));
            if (stopped) {
                closeCurrentSocket(false);
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
                // permessage-deflate (RFC 7692) is intentionally NOT advertised: getFrame()
                // ignores RSV1 and there is no inflate path, so accepting the extension
                // would silently feed compressed bytes to the message callbacks.
                // "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
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
                    closeCurrentSocket(true);
                    throw std::runtime_error("websock: upgrade response headers too large");
                }
                int n = recvSocket(buf.data() + recvd, buf.size() - 1 - recvd, 100);
                if (n < 0) {
                    std::string msg = "websock: recv failed, errno=" + std::to_string(errno) + " (recvd=" + std::to_string(recvd) +
                                      " senderr=" + std::to_string(senderr) + ")";
                    closeCurrentSocket(true);
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
                closeCurrentSocket(false);
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
                    closeCurrentSocket(true);
                    throw std::runtime_error("websock: handshake failed: missing or wrong Upgrade header (got '" + upgradeHdr + "')");
                }
                std::string accept = findHeaderValue(recvHeaders, "Sec-WebSocket-Accept");
                std::string expected = sha1Base64(secKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
                if (accept != expected) {
                    closeCurrentSocket(true);
                    throw std::runtime_error("websock: handshake failed: bad Sec-WebSocket-Accept (got '" + accept + "', expected '" + expected + "')");
                }
                data.clear();
                for (int i = (int)pos + 4; i < recvd; i++) {
                    data.push_back(buf[i]);
                }
                websocketReady = true;
                break;
            }

            const bool isRedirect = (status == 301 || status == 302 || status == 303 ||
                                     status == 307 || status == 308);
            if (isRedirect && redirectsLeft > 0) {
                std::string location = findHeaderValue(recvHeaders, "Location");
                if (location.empty()) {
                    closeCurrentSocket(true);
                    throw std::runtime_error("websock: redirect without Location header");
                }
                auto parsed = url::parseHttpHostPort(location);
                if (!parsed) {
                    closeCurrentSocket(true);
                    throw std::runtime_error("websock: cannot parse redirect Location: " + location);
                }
                closeCurrentSocket(false);
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

            closeCurrentSocket(true);
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
                closeCurrentSocket(true);
                onDisconnected();
                shouldNotifyDisconnected = false;
                throw;
            }
            if (len0 > 0) {
//                printf("decoded/dropping bytes: %d\n", len0);
                data.erase(data.begin(), data.begin() + len0);
                continue;
            }
            recvd = recvSocket(buf.data(), buf.size(), 100); // 100 msec
            if (stopped) {
                break;
            }
            if (recvd == 0) {
                continue;
            }
//            printf("recvd bytes in loop: %d\n", recvd);
            if (recvd <= 0) {
                closeCurrentSocket(true);
                onDisconnected();
                shouldNotifyDisconnected = false;
                break;
            }
            onEveryReceive();
            if (data.size() + recvd > MAX_FRAME_PAYLOAD + 16) {
                closeCurrentSocket(true);
                onDisconnected();
                shouldNotifyDisconnected = false;
                throw std::runtime_error("websock: frame too large");
            }
            for (int i = 0; i < recvd; i++) {
                data.push_back(buf[i]);
            }
        }
        if (shouldNotifyDisconnected) {
            closeCurrentSocket(true);
            onDisconnected();
        }
    }

    void WSClient::stopSocket() {
        // Set the flag and let the receive loop close the socket on its way
        // out. Socket method calls are serialized by socketMutex, so no
        // thread closes while another is inside send/recv.
        stopped = true;
    }

}
