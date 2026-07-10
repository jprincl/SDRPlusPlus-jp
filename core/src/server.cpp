#include "server.h"
#include "core.h"
#include <utils/flog.h>
#include <version.h>
#include <config.h>
#include <filesystem>
#include <dsp/types.h>
#include <signal_path/signal_path.h>
#include <gui/smgui.h>
#include <utils/optionlist.h>
#include "dsp/compression/sample_stream_compressor.h"
#include "dsp/sink/handler_sink.h"
#include <zstd.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>
#include <cstring>

namespace server {
    dsp::stream<dsp::complex_t> dummyInput;
    dsp::compression::SampleStreamCompressor comp;
    dsp::sink::Handler<uint8_t> hnd;

    // Baseband TX buffer. Only ever touched by the single DSP/baseband thread,
    // so it stays a plain global (separate from the per-session send buffer).
    uint8_t* bbuf = NULL;
    PacketHeader* bb_pkt_hdr = NULL;
    uint8_t* bb_pkt_data = NULL;

    SmGui::DrawListElem dummyElem;

    ZSTD_CCtx* cctx;

    net::Listener listener;

    OptionList<std::string, std::string> sourceList;
    int sourceId = 0;
    bool running = false;
    // Written by the command handler, read by the baseband DSP thread.
    std::atomic<bool> compression{false};

    // Cached server-side state, guarded by controlMtx, re-sent to every client
    // that connects (a later client still gets the current samplerate/limits).
    double sampleRate = 1000000.0;
    bool tuningLimitEnabled = false;
    double tuningLimitMin = 0.0;
    double tuningLimitMax = 0.0;

    // A connected client and everything scoped to that connection: the socket
    // plus its own receive and send buffers. Making the buffers per-session is
    // what keeps an old, still-draining callback from corrupting the buffers of
    // a freshly accepted connection, and routing replies through the session
    // (rather than a global handle) keeps them going to the origin socket.
    struct ClientSession {
        // Buffers are declared before `conn` on purpose: members are destroyed
        // in reverse order, so `conn` (destroyed first) joins its read worker
        // while these buffers — which a callback still running on that worker
        // may be reading/writing — are still alive.
        std::vector<uint8_t> rstore;
        std::vector<uint8_t> sstore;

        PacketHeader* r_pkt_hdr;
        uint8_t* r_pkt_data;
        CommandHeader* r_cmd_hdr;
        uint8_t* r_cmd_data;

        PacketHeader* s_pkt_hdr;
        uint8_t* s_pkt_data;
        CommandHeader* s_cmd_hdr;
        uint8_t* s_cmd_data;

        std::shared_ptr<net::ConnClass> conn;

        ClientSession(net::Conn c)
            : rstore(SERVER_MAX_PACKET_SIZE), sstore(SERVER_MAX_PACKET_SIZE), conn(std::move(c)) {
            uint8_t* rbuf = rstore.data();
            r_pkt_hdr = (PacketHeader*)rbuf;
            r_pkt_data = &rbuf[sizeof(PacketHeader)];
            r_cmd_hdr = (CommandHeader*)r_pkt_data;
            r_cmd_data = &rbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];

            uint8_t* sbuf = sstore.data();
            s_pkt_hdr = (PacketHeader*)sbuf;
            s_pkt_data = &sbuf[sizeof(PacketHeader)];
            s_cmd_hdr = (CommandHeader*)s_pkt_data;
            s_cmd_data = &sbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];
        }

        bool isOpen() { return conn && conn->isOpen(); }

        // Compose-and-send into this session's send buffer. The caller must
        // hold controlMtx (it serializes composition into sstore against the
        // other threads that send: the accept greeting and source modules).
        void sendPacket(PacketType type, int len) {
            s_pkt_hdr->type = type;
            s_pkt_hdr->size = sizeof(PacketHeader) + len;
            if (conn) { conn->write(s_pkt_hdr->size, sstore.data()); }
        }

        void sendCommand(Command cmd, int len) {
            s_cmd_hdr->cmd = cmd;
            sendPacket(PACKET_TYPE_COMMAND, sizeof(CommandHeader) + len);
        }

        void sendCommandAck(Command cmd, int len) {
            s_cmd_hdr->cmd = cmd;
            sendPacket(PACKET_TYPE_COMMAND_ACK, sizeof(CommandHeader) + len);
        }

        void sendError(Error err) {
            s_pkt_data[0] = err;
            sendPacket(PACKET_TYPE_ERROR, 1);
        }

        void sendSampleRate(double samplerate) {
            // memcpy: s_cmd_data is not 8-byte aligned (header offsets).
            memcpy(s_cmd_data, &samplerate, sizeof(double));
            sendCommand(COMMAND_SET_SAMPLERATE, sizeof(double));
        }

        void sendTuningLimits(bool enabled, double minFreq, double maxFreq) {
            s_cmd_data[0] = enabled;
            // memcpy: s_cmd_data is not 8-byte aligned (header offsets).
            memcpy(&s_cmd_data[1], &minFreq, sizeof(double));
            memcpy(&s_cmd_data[1 + sizeof(double)], &maxFreq, sizeof(double));
            sendCommand(COMMAND_SET_TUNING_LIMITS, 1 + 2 * sizeof(double));
        }
    };

    // The active session and the mutex guarding the handle itself. Every user
    // snapshots the shared_ptr under sessionMtx so a reconnect on the accept
    // thread can't free the connection out from under a send/receive in flight.
    std::shared_ptr<ClientSession> currentSession;
    std::mutex sessionMtx;

    static std::shared_ptr<ClientSession> getSession() {
        std::lock_guard lck(sessionMtx);
        return currentSession;
    }

    // Serializes the whole control plane: the global SmGui rendering
    // (renderUI/drawMenu and the source menus they invoke), the cached
    // samplerate/tuning-limit state, and all command/state sends into a
    // session's send buffer. Recursive because rendering a source menu can
    // re-enter this path (a source's menuSelected calls core::setInputSampleRate,
    // which sends). The baseband path deliberately does NOT take this lock: it
    // uses its own bbuf and only snapshots the session to write.
    std::recursive_mutex controlMtx;

    // Internal handlers/helpers (never called from outside this file).
    static void _clientHandler(net::Conn conn, void* ctx);
    static void _packetHandler(int count, uint8_t* buf, void* ctx);
    static void _testServerHandler(uint8_t* data, int count, void* ctx);
    static void commandHandler(ClientSession* s, Command cmd, uint8_t* data, int len);
    static void drawMenu();
    static void renderUI(SmGui::DrawList* dl, std::string diffId, SmGui::DrawListElem diffValue);
    static void sendUI(ClientSession* s, Command originCmd, std::string diffId, SmGui::DrawListElem diffValue);

    int main() {
        flog::info("=====| SERVER MODE |=====");

        // Init DSP
        comp.init(&dummyInput, dsp::compression::PCM_TYPE_I8);
        hnd.init(&comp.out, _testServerHandler, NULL);
        bbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        comp.start();
        hnd.start();

        // Initialize baseband header pointers
        bb_pkt_hdr = (PacketHeader*)bbuf;
        bb_pkt_data = &bbuf[sizeof(PacketHeader)];

        // Initialize compressor
        cctx = ZSTD_createCCtx();

        // Load config
        core::configManager.acquire();
        std::string modulesDir = core::getModulesDirectory();
        std::vector<std::string> modules = core::configManager.conf["modules"];
        auto modList = core::configManager.conf["moduleInstances"].items();
        std::string sourceName = core::configManager.conf["source"];
        core::configManager.release();
        modulesDir = std::filesystem::absolute(modulesDir).string();

        // Initialize SmGui in server mode
        SmGui::init(true);

        flog::info("Loading modules");
        // Load modules and check type to only load sources ( TODO: Have a proper type parameter int the info )
        // TODO LATER: Add whitelist/blacklist stuff
        if (std::filesystem::is_directory(modulesDir)) {
            for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
                std::string path = file.path().generic_string();
                std::string fn = file.path().filename().string();
                if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                    continue;
                }
                if (!file.is_regular_file()) { continue; }
                if (fn.find("source") == std::string::npos) { continue; }

                flog::info("Loading {0}", path);
                core::moduleManager.loadModule(path);
            }
        }
        else {
            flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
        }

        // Load additional modules through the config ( TODO: Have a proper type parameter int the info )
        // TODO LATER: Add whitelist/blacklist stuff
        for (auto const& apath : modules) {
            std::filesystem::path file = std::filesystem::absolute(apath);
            std::string path = file.generic_string();
            std::string fn = file.filename().string();
            if (file.extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!std::filesystem::is_regular_file(file)) { continue; }
            if (fn.find("source") == std::string::npos) { continue; }

            flog::info("Loading {0}", path);
            core::moduleManager.loadModule(path);
        }

        // Create module instances
        for (auto const& [name, _module] : modList) {
            std::string mod = _module["module"];
            bool enabled = _module["enabled"];
            if (core::moduleManager.modules.find(mod) == core::moduleManager.modules.end()) { continue; }
            flog::info("Initializing {0} ({1})", name, mod);
            core::moduleManager.createInstance(name, mod);
            if (!enabled) { core::moduleManager.disableInstance(name); }
        }

        // Do post-init
        core::moduleManager.doPostInitAll();

        // Generate source list
        auto list = sigpath::sourceManager.getSourceNames();
        for (auto& name : list) {
            sourceList.define(name, name);
        }

        // Load sourceId from config
        sourceId = 0;
        if (sourceList.keyExists(sourceName)) { sourceId = sourceList.keyId(sourceName); }
        sigpath::sourceManager.selectSource(sourceList[sourceId]);

        // TODO: Use command line option
        std::string host = (std::string)core::args["addr"];
        int port = (int)core::args["port"];
        listener = net::listen(host, port);
        listener->acceptAsync(_clientHandler, NULL);

        flog::info("Ready, listening on {0}:{1}", host, port);
        while(1) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

        return 0;
    }

    static void _clientHandler(net::Conn conn, void* ctx) {
        // Reject if someone else is already connected
        if (auto s = getSession(); s && s->isOpen()) {
            flog::info("REJECTED Connection from {0}:{1}, another client is already connected.", "TODO", "TODO");

            // Issue a disconnect command to the client
            uint8_t buf[sizeof(PacketHeader) + sizeof(CommandHeader)];
            PacketHeader* tmp_phdr = (PacketHeader*)buf;
            CommandHeader* tmp_chdr = (CommandHeader*)&buf[sizeof(PacketHeader)];
            tmp_phdr->size = sizeof(PacketHeader) + sizeof(CommandHeader);
            tmp_phdr->type = PACKET_TYPE_COMMAND;
            tmp_chdr->cmd = COMMAND_DISCONNECT;
            conn->write(tmp_phdr->size, buf);

            // TODO: Find something cleaner
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            conn->close();

            // Start another async accept
            listener->acceptAsync(_clientHandler, NULL);
            return;
        }

        flog::info("Connection from {0}:{1}", "TODO", "TODO");
        auto session = std::make_shared<ClientSession>(std::move(conn));

        // Publish the new session and drop the previous one outside the lock.
        // Destroying the old session joins its read worker, so any callback
        // still draining on it finishes (against its own buffers) before the
        // new session starts reading — old and new never render concurrently.
        std::shared_ptr<ClientSession> old;
        {
            std::lock_guard lck(sessionMtx);
            old = std::move(currentSession);
            currentSession = session;
        }
        old.reset();

        // Perform settings reset
        sigpath::sourceManager.stop();
        comp.setPCMType(dsp::compression::PCM_TYPE_I16);
        compression = false;

        // Greet the client with the current samplerate and tuning limits.
        {
            std::lock_guard lck(controlMtx);
            session->sendSampleRate(sampleRate);
            session->sendTuningLimits(tuningLimitEnabled, tuningLimitMin, tuningLimitMax);
        }

        // Start reading commands from the new client.
        session->conn->readAsync(sizeof(PacketHeader), session->rstore.data(), _packetHandler, session.get());

        listener->acceptAsync(_clientHandler, NULL);
    }

    static void _packetHandler(int count, uint8_t* buf, void* ctx) {
        // The session is passed as a raw ctx. It stays valid for this whole
        // call: a reconnect destroys the session on the accept thread, but that
        // runs the conn destructor first, which joins this very worker before
        // freeing the buffers below. Grab the conn pointer once here.
        ClientSession* s = (ClientSession*)ctx;
        net::ConnClass* conn = s->conn.get();
        PacketHeader* hdr = s->r_pkt_hdr;

        // Validate the client-supplied size before using it as a read length:
        // the receive buffer is a fixed SERVER_MAX_PACKET_SIZE, so an
        // out-of-range size would overflow it (and a value below the header
        // size would wrap the unsigned subtraction below).
        if (hdr->size < sizeof(PacketHeader) || hdr->size > SERVER_MAX_PACKET_SIZE) {
            flog::error("Invalid packet size from client: {0}", hdr->size);
            return;
        }

        // Read the rest of the data (TODO: ADD TIMEOUT)
        int len = 0;
        int read = 0;
        int goal = hdr->size - sizeof(PacketHeader);
        while (len < goal) {
            read = conn->read(goal - len, &s->r_pkt_data[len]);
            if (read < 0) { return; };
            len += read;
        }

        // Parse and process
        if (hdr->type == PACKET_TYPE_COMMAND && hdr->size >= sizeof(PacketHeader) + sizeof(CommandHeader)) {
            commandHandler(s, (Command)s->r_cmd_hdr->cmd, s->r_cmd_data, hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
        }
        else {
            std::lock_guard lck(controlMtx);
            s->sendError(ERROR_INVALID_PACKET);
        }

        // Start another async read
        conn->readAsync(sizeof(PacketHeader), s->rstore.data(), _packetHandler, s);
    }

    static void _testServerHandler(uint8_t* data, int count, void* ctx) {
        // Compress data if needed and fill out header fields
        if (compression) {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND_COMPRESSED;
            bb_pkt_hdr->size = sizeof(PacketHeader) + (uint32_t)ZSTD_compressCCtx(cctx, &bbuf[sizeof(PacketHeader)], SERVER_MAX_PACKET_SIZE-sizeof(PacketHeader), data, count, 1);
        }
        else {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND;
            bb_pkt_hdr->size = sizeof(PacketHeader) + count;
            memcpy(&bbuf[sizeof(PacketHeader)], data, count);
        }

        // Write to network. Snapshot the session so a concurrent reconnect on
        // the accept thread can't free the connection mid-write.
        auto s = getSession();
        if (s && s->isOpen()) { s->conn->write(bb_pkt_hdr->size, bbuf); }
    }

    void setInput(dsp::stream<dsp::complex_t>* stream) {
        comp.setInput(stream);
    }

    static void commandHandler(ClientSession* s, Command cmd, uint8_t* data, int len) {
        if (cmd == COMMAND_GET_UI) {
            sendUI(s, COMMAND_GET_UI, "", dummyElem);
        }
        else if (cmd == COMMAND_UI_ACTION && len >= 3) {
            // Check if sending back data is needed
            int i = 0;
            bool sendback = data[i++];
            len--;

            // Load id
            SmGui::DrawListElem diffId;
            int count = SmGui::DrawList::loadItem(diffId, &data[i], len);
            if (count < 0 || diffId.type != SmGui::DRAW_LIST_ELEM_TYPE_STRING) {
                std::lock_guard lck(controlMtx);
                s->sendError(ERROR_INVALID_ARGUMENT);
                return;
            }
            i += count;
            len -= count;

            // Load value
            SmGui::DrawListElem diffValue;
            count = SmGui::DrawList::loadItem(diffValue, &data[i], len);
            if (count < 0) {
                std::lock_guard lck(controlMtx);
                s->sendError(ERROR_INVALID_ARGUMENT);
                return;
            }
            i += count;
            len -= count;

            // Render and send back
            if (sendback) {
                sendUI(s, COMMAND_UI_ACTION, diffId.str, diffValue);
            }
            else {
                std::lock_guard lck(controlMtx);
                renderUI(NULL, diffId.str, diffValue);
            }
        }
        else if (cmd == COMMAND_START) {
            std::lock_guard lck(controlMtx);
            sigpath::sourceManager.start();
            running = true;
        }
        else if (cmd == COMMAND_STOP) {
            std::lock_guard lck(controlMtx);
            sigpath::sourceManager.stop();
            running = false;
        }
        else if (cmd == COMMAND_SET_FREQUENCY && len == 8) {
            // memcpy: data is not 8-byte aligned (header offsets).
            double freq;
            memcpy(&freq, data, sizeof(double));
            std::lock_guard lck(controlMtx);
            sigpath::sourceManager.tune(freq);
            s->sendCommandAck(COMMAND_SET_FREQUENCY, 0);
        }
        else if (cmd == COMMAND_SET_SAMPLE_TYPE && len == 1) {
            dsp::compression::PCMType type = (dsp::compression::PCMType)*(uint8_t*)data;
            comp.setPCMType(type);
        }
        else if (cmd == COMMAND_SET_COMPRESSION && len == 1) {
            compression = *(uint8_t*)data;
        }
        else {
            flog::error("Invalid Command: {0} (len = {1})", (int)cmd, len);
            std::lock_guard lck(controlMtx);
            s->sendError(ERROR_INVALID_COMMAND);
        }
    }

    static void drawMenu() {
        if (running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo("##sdrpp_server_src_sel", &sourceId, sourceList.txt)) {
            sigpath::sourceManager.selectSource(sourceList[sourceId]);
            core::configManager.acquire();
            core::configManager.conf["source"] = sourceList.key(sourceId);
            core::configManager.release(true);
        }
        if (running) { SmGui::EndDisabled(); }

        sigpath::sourceManager.showSelectedMenu();
    }

    static void renderUI(SmGui::DrawList* dl, std::string diffId, SmGui::DrawListElem diffValue) {
        // If we're recording and there's an action, render once with the action and record without

        if (dl && !diffId.empty()) {
            SmGui::setDiff(diffId, diffValue);
            drawMenu();

            SmGui::setDiff("", dummyElem);
            SmGui::startRecord(dl);
            drawMenu();
            SmGui::stopRecord();
        }
        else {
            SmGui::setDiff(diffId, diffValue);
            SmGui::startRecord(dl);
            drawMenu();
            SmGui::stopRecord();
        }
    }

    static void sendUI(ClientSession* s, Command originCmd, std::string diffId, SmGui::DrawListElem diffValue) {
        // renderUI touches global SmGui state and can re-enter the send path
        // (a source menu's selection calls setInputSampleRate), so the whole
        // render-and-send runs under controlMtx.
        std::lock_guard lck(controlMtx);

        // Render UI
        SmGui::DrawList dl;
        renderUI(&dl, diffId, diffValue);

        // Create response and send to network
        int size = dl.getSize();
        dl.store(s->s_cmd_data, size);
        s->sendCommandAck(originCmd, size);
    }

    void setTuningLimits(bool enabled, double minFreq, double maxFreq) {
        std::lock_guard lck(controlMtx);
        tuningLimitEnabled = enabled;
        tuningLimitMin = minFreq;
        tuningLimitMax = maxFreq;
        auto s = getSession();
        if (s && s->isOpen()) { s->sendTuningLimits(enabled, minFreq, maxFreq); }
    }

    void setInputSampleRate(double samplerate) {
        std::lock_guard lck(controlMtx);
        sampleRate = samplerate;
        auto s = getSession();
        if (s && s->isOpen()) { s->sendSampleRate(samplerate); }
    }
}
