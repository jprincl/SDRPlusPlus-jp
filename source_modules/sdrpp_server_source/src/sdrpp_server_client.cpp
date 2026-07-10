#include "sdrpp_server_client.h"
#include <volk/volk.h>
#include <cstring>
#include <cmath>
#include <utils/flog.h>
#include <core.h>
#include <signal_path/signal_path.h>

using namespace std::chrono_literals;

namespace server {
    Client::Client(std::shared_ptr<net::Socket> sock, dsp::stream<dsp::complex_t>* out) {
        this->sock = sock;
        output = out;

        // Allocate buffers
        rbuffer = new uint8_t[SERVER_MAX_PACKET_SIZE];
        sbuffer = new uint8_t[SERVER_MAX_PACKET_SIZE];

        // Initialize headers
        r_pkt_hdr = (PacketHeader*)rbuffer;
        r_pkt_data = &rbuffer[sizeof(PacketHeader)];
        r_cmd_hdr = (CommandHeader*)r_pkt_data;
        r_cmd_data = &rbuffer[sizeof(PacketHeader) + sizeof(CommandHeader)];

        s_pkt_hdr = (PacketHeader*)sbuffer;
        s_pkt_data = &sbuffer[sizeof(PacketHeader)];
        s_cmd_hdr = (CommandHeader*)s_pkt_data;
        s_cmd_data = &sbuffer[sizeof(PacketHeader) + sizeof(CommandHeader)];

        // Initialize decompressor
        dctx = ZSTD_createDCtx();

        // Initialize DSP
        decompIn.setBufferSize(STREAM_BUFFER_SIZE*sizeof(dsp::complex_t) + 8);
        decompIn.clearWriteStop();
        decomp.init(&decompIn);
        link.init(&decomp.out, output);
        decomp.start();
        link.start();

        // Start worker thread
        workerThread = std::thread(&Client::worker, this);

        // Ask for a UI
        int res = getUI();
        if (res < 0) {
            // Close client
            close();

            // Throw error
            switch (res) {
            case CONN_ERR_TIMEOUT:
                throw std::runtime_error("Timed out");
            case CONN_ERR_BUSY:
                throw std::runtime_error("Server busy");
            default:
                throw std::runtime_error("Unknown error");
            }
        }
    }

    Client::~Client() {
        close();
        ZSTD_freeDCtx(dctx);
        delete[] rbuffer;
        delete[] sbuffer;
    }

    void Client::showMenu() {
        std::string diffId = "";
        SmGui::DrawListElem diffValue;
        bool syncRequired = false;
        {
            std::lock_guard<std::mutex> lck(dlMtx);
            dl.draw(diffId, diffValue, syncRequired);
        }

        if (!diffId.empty()) {
            // Save ID
            SmGui::DrawListElem elemId;
            elemId.type = SmGui::DRAW_LIST_ELEM_TYPE_STRING;
            elemId.str = diffId;

            // Encore packet
            int size = 0;
            s_cmd_data[size++] = syncRequired;
            size += SmGui::DrawList::storeItem(elemId, &s_cmd_data[size], SERVER_MAX_PACKET_SIZE - size);
            size += SmGui::DrawList::storeItem(diffValue, &s_cmd_data[size], SERVER_MAX_PACKET_SIZE - size);

            // Send
            if (syncRequired) {
                flog::warn("Action requires resync");
                auto waiter = awaitCommandAck(COMMAND_UI_ACTION);
                sendCommand(COMMAND_UI_ACTION, size);
                if (waiter->await(PROTOCOL_TIMEOUT_MS)) {
                    const auto& payload = waiter->data();
                    std::lock_guard lck(dlMtx);
                    dl.load((void*)payload.data(), (int)payload.size());
                }
                else {
                    // Drop the abandoned waiter, otherwise a late ACK for this
                    // request would be delivered to a later same-command waiter.
                    forgetCommandAck(waiter);
                    flog::error("Timeout out after asking for UI");
                }
                flog::warn("Resync done");
            }
            else {
                flog::warn("Action does not require resync");
                sendCommand(COMMAND_UI_ACTION, size);
            }
        }
    }

    void Client::syncRemoteState(bool force) {
        // Consume the dirty flags before snapshotting, so an update the
        // worker publishes in between is not lost: it sets the flag again
        // and the next frame picks it up.
        bool applySamplerate = samplerateDirty.exchange(false) || force;
        bool applyLimits = tuningLimitsDirty.exchange(false) || force;
        if (!applySamplerate && !applyLimits) { return; }

        double samplerate;
        bool limitsEnabled;
        double limitMin, limitMax;
        {
            std::lock_guard lck(pushedStateMtx);
            samplerate = currentSampleRate;
            limitsEnabled = remoteTuningLimitsEnabled;
            limitMin = remoteTuningLimitMin;
            limitMax = remoteTuningLimitMax;
        }

        // Apply outside the mutex: setInputSampleRate reconfigures the DSP
        // chain and setTuningLimits touches GUI state.
        if (applySamplerate) {
            core::setInputSampleRate(samplerate);
        }
        if (applyLimits) {
            if (limitsEnabled) {
                // Native domain; the local SourceManager shifts by the local
                // tuning offset before constraining gui::freqSelect.
                sigpath::sourceManager.setTuningLimits(limitMin, limitMax);
            }
            else {
                sigpath::sourceManager.clearTuningLimits();
            }
        }
    }

    void Client::setFrequency(double freq) {
        if (!isOpen()) { return; }
        // Register the waiter before sending, so an ACK arriving right away
        // cannot be missed.
        auto waiter = awaitCommandAck(COMMAND_SET_FREQUENCY);
        // memcpy: s_cmd_data is not 8-byte aligned (header offsets).
        memcpy(s_cmd_data, &freq, sizeof(double));
        sendCommand(COMMAND_SET_FREQUENCY, sizeof(double));
        if (!waiter->await(PROTOCOL_TIMEOUT_MS)) { forgetCommandAck(waiter); }
    }

    double Client::getSampleRate() {
        std::lock_guard lck(pushedStateMtx);
        return currentSampleRate;
    }

    void Client::setSampleType(dsp::compression::PCMType type) {
        if (!isOpen()) { return; }
        s_cmd_data[0] = type;
        sendCommand(COMMAND_SET_SAMPLE_TYPE, 1);
    }

    void Client::setCompression(bool enabled) {
        if (!isOpen()) { return; }
         s_cmd_data[0] = enabled;
        sendCommand(COMMAND_SET_COMPRESSION, 1);
    }

    void Client::start() {
        if (!isOpen()) { return; }
        sendCommand(COMMAND_START, 0);
        getUI();
    }

    void Client::stop() {
        if (!isOpen()) { return; }
        sendCommand(COMMAND_STOP, 0);
        getUI();
    }

    void Client::close() {
        // Stop worker
        decompIn.stopWriter();
        if (sock) { sock->close(); }
        if (workerThread.joinable()) { workerThread.join(); }
        decompIn.clearWriteStop();

        // Stop DSP
        decomp.stop();
        link.stop();
    }

    bool Client::isOpen() {
        return sock && sock->isOpen();
    }

    void Client::worker() {
        while (true) {
            // Receive header
            if (sock->recv(rbuffer, sizeof(PacketHeader), true) <= 0) {
                break;
            }

            // Validate the server-supplied size before using it as a receive
            // length: rbuffer is a fixed SERVER_MAX_PACKET_SIZE allocation, so
            // an out-of-range size would overflow it (and a value below the
            // header size would wrap the unsigned subtraction below).
            if (r_pkt_hdr->size < sizeof(PacketHeader) || r_pkt_hdr->size > SERVER_MAX_PACKET_SIZE) {
                flog::error("Invalid packet size from server: {0}", r_pkt_hdr->size);
                break;
            }

            // Receive remaining data
            if (sock->recv(&rbuffer[sizeof(PacketHeader)], r_pkt_hdr->size - sizeof(PacketHeader), true, PROTOCOL_TIMEOUT_MS) <= 0) {
                break;
            }

            // Increment data counter
            bytes += r_pkt_hdr->size;

            // Decode packet. Worker discipline: only parse and cache into
            // mutex-protected client state; SourceManager/GUI applies happen
            // on the GUI thread via syncRemoteState().
            if (r_pkt_hdr->type == PACKET_TYPE_COMMAND) {
                // TODO: Move to command handler
                if (r_cmd_hdr->cmd == COMMAND_SET_SAMPLERATE && r_pkt_hdr->size == sizeof(PacketHeader) + sizeof(CommandHeader) + sizeof(double)) {
                    // memcpy: r_cmd_data is not 8-byte aligned (header offsets).
                    double samplerate;
                    memcpy(&samplerate, r_cmd_data, sizeof(double));
                    {
                        std::lock_guard lck(pushedStateMtx);
                        currentSampleRate = samplerate;
                    }
                    samplerateDirty = true;
                }
                else if (r_cmd_hdr->cmd == COMMAND_SET_TUNING_LIMITS && r_pkt_hdr->size == sizeof(PacketHeader) + sizeof(CommandHeader) + 1 + 2 * sizeof(double)) {
                    bool enabled = r_cmd_data[0];
                    double limitMin, limitMax;
                    memcpy(&limitMin, &r_cmd_data[1], sizeof(double));
                    memcpy(&limitMax, &r_cmd_data[1 + sizeof(double)], sizeof(double));
                    // Don't trust the server: non-finite or inverted limits
                    // would become garbage (uint64_t) casts in the frequency
                    // selector and could lock the user out of tuning.
                    if (enabled && (!std::isfinite(limitMin) || !std::isfinite(limitMax) || limitMin > limitMax)) {
                        flog::error("Ignoring invalid tuning limits from server: [{0}, {1}]", limitMin, limitMax);
                    }
                    else {
                        {
                            std::lock_guard lck(pushedStateMtx);
                            remoteTuningLimitsEnabled = enabled;
                            remoteTuningLimitMin = limitMin;
                            remoteTuningLimitMax = limitMax;
                        }
                        tuningLimitsDirty = true;
                    }
                }
                else if (r_cmd_hdr->cmd == COMMAND_DISCONNECT) {
                    flog::error("Asked to disconnect by the server");
                    serverBusy = true;
                    cancelAllWaiters();
                }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_COMMAND_ACK) {
                // Copy the payload into the matching waiters and wake them.
                int payloadSize = (int)r_pkt_hdr->size - (int)(sizeof(PacketHeader) + sizeof(CommandHeader));
                if (payloadSize < 0) { payloadSize = 0; }
                std::lock_guard lck(waitersMtx);
                for (auto it = commandAckWaiters.begin(); it != commandAckWaiters.end();) {
                    if (it->second == r_cmd_hdr->cmd) {
                        it->first->notify(r_cmd_data, payloadSize);
                        it = commandAckWaiters.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_BASEBAND) {
                memcpy(decompIn.writeBuf, &rbuffer[sizeof(PacketHeader)], r_pkt_hdr->size - sizeof(PacketHeader));
                if (!decompIn.swap(r_pkt_hdr->size - sizeof(PacketHeader))) { break; }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_BASEBAND_COMPRESSED) {
                size_t outCount = ZSTD_decompressDCtx(dctx, decompIn.writeBuf, STREAM_BUFFER_SIZE*sizeof(dsp::complex_t)+8, r_pkt_data, r_pkt_hdr->size - sizeof(PacketHeader));
                if (outCount) {
                    if (!decompIn.swap(outCount)) { break; }
                };
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_ERROR) {
                flog::error("SDR++ Server Error: {0}", rbuffer[sizeof(PacketHeader)]);
            }
            else {
                flog::error("Invalid packet type: {0}", r_pkt_hdr->type);
            }
        }

        // Connection is gone; release anyone still waiting for an ACK.
        cancelAllWaiters();
    }

    int Client::getUI() {
        if (!isOpen()) { return -1; }
        auto waiter = awaitCommandAck(COMMAND_GET_UI);
        sendCommand(COMMAND_GET_UI, 0);
        if (!waiter->await(PROTOCOL_TIMEOUT_MS)) {
            // Drop the abandoned waiter, otherwise a late ACK for this request
            // would be delivered to a later same-command waiter.
            forgetCommandAck(waiter);
            if (!serverBusy) { flog::error("Timeout out after asking for UI"); };
            return serverBusy ? CONN_ERR_BUSY : CONN_ERR_TIMEOUT;
        }
        const auto& payload = waiter->data();
        std::lock_guard lck(dlMtx);
        dl.load((void*)payload.data(), (int)payload.size());
        return 0;
    }

    void Client::sendPacket(PacketType type, int len) {
        s_pkt_hdr->type = type;
        s_pkt_hdr->size = sizeof(PacketHeader) + len;
        sock->send(sbuffer, s_pkt_hdr->size);
    }

    void Client::sendCommand(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND, sizeof(CommandHeader) + len);
    }

    void Client::sendCommandAck(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND_ACK, sizeof(CommandHeader) + len);
    }

    std::shared_ptr<PacketWaiter> Client::awaitCommandAck(Command cmd) {
        auto waiter = std::make_shared<PacketWaiter>();
        std::lock_guard lck(waitersMtx);
        commandAckWaiters[waiter] = cmd;
        return waiter;
    }

    void Client::cancelAllWaiters() {
        std::lock_guard lck(waitersMtx);
        for (auto& [waiter, cmd] : commandAckWaiters) {
            waiter->cancel();
        }
        commandAckWaiters.clear();
    }

    void Client::forgetCommandAck(const std::shared_ptr<PacketWaiter>& waiter) {
        std::lock_guard lck(waitersMtx);
        commandAckWaiters.erase(waiter);
    }

    void Client::dHandler(dsp::complex_t *data, int count, void *ctx) {
        Client* _this = (Client*)ctx;
        memcpy(_this->output->writeBuf, data, count * sizeof(dsp::complex_t));
        _this->output->swap(count);
    }

    std::shared_ptr<Client> connect(std::string host, uint16_t port, dsp::stream<dsp::complex_t>* out) {
        return std::make_shared<Client>(net::connect(host, port), out);
    }
}
