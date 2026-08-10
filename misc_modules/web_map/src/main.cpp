/*
 * web_map — SDR++ module
 *
 * Own embedded HTTP/SSE server living directly in the SDR++ process (no
 * subprocess, unlike F4JTV's sdr_map_launcher, which spawns Django) + a
 * TCP collector on a second, independently configurable port, protocol
 * compatible with F4JTV decoders (see tcp_collector.h for the exact shape
 * of the JSON-lines envelope). The only entry point for data from decoders
 * -- deliberately no in-process API (considered and rejected: it would
 * lock a producer module to this specific web_map; TCP keeps decoders what
 * they should be -- a universal data source, usable outside this process
 * and outside SDR++ entirely).
 *
 * Start/Stop in the panel controls both the HTTP and TCP layers
 * atomically -- if the TCP port fails to bind, HTTP is torn back down too,
 * so it never sits half-started.
 *
 * The frontend at "/" is a real Leaflet map (library vendored locally,
 * tiles from OSM online). A newly opened browser first downloads
 * "/api/snapshot" (current state of every received object), then
 * connects to "/events" (SSE) for live updates -- without the snapshot it
 * would only ever see things received AFTER the page was opened.
 *
 * Deliberately still missing (a next step, not a bug): expiring old
 * objects and broadcasting a "remove" event when a source disconnects --
 * F4JTV handles this in listen_sdr.py, we currently keep objects in
 * memory forever.
 */

#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <module.h>
#include <utils/flog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// cpp-httplib is a vendored single-header library (MIT), see httplib.h
// next to this file. Quoted include (not <>): quoted lookup always
// searches the including file's own directory first, independent of how a
// given CMake/Ninja generator decides to pass include paths to the
// compiler -- on the Android build the angle-bracket form turned out to
// rely on that and didn't work. No OpenSSL flag = plain HTTP, no TLS --
// fine for a local/LAN dev server.
//
// cpp-httplib's "USE_IF2IP" branch (binding by network interface name) is
// gated on "!defined ANDROID" -- but the NDK toolchain defines
// __ANDROID__ (with underscores), not bare ANDROID. Without this fix that
// branch mistakenly compiles on Android and falls over on
// getifaddrs/freeifaddrs, which the bionic sysroot doesn't declare below a
// high enough API level. We don't need that feature (binding by interface
// name), so this just reliably turns it off without touching the
// vendored file itself.
#if defined(__ANDROID__) && !defined(ANDROID)
    #define ANDROID
#endif

#include "httplib.h"
#include "leaflet_assets.h"
#include "tcp_collector.h"

#include <cctype>
#include <cstdlib>
#include <map>

SDRPP_MOD_INFO{
    /* Name:            */ "web_map",
    /* Description:     */ "Embedded HTTP/SSE server + TCP collector for a live Leaflet position map (F4JTV-compatible JSON-lines).",
    /* Author:          */ "jprincl",
    /* Version:         */ 0, 1, 0,
    /* Max instances:   */ 1
};

ConfigManager config;

// ---------------------------------------------------------------- SSE ----
// One open /events connection = one client with its own message queue.
// broadcast() adds a message to every active client's queue and wakes
// their content provider thread (see setupRoutes).
struct SseClient {
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::string> queue;
    std::atomic<bool> active{true};
};

class WebMapModule : public ModuleManager::Instance {
public:
    WebMapModule(std::string name) {
        this->name = name;

        config.acquire();
        if (config.conf.contains("http_host")) httpHost = config.conf["http_host"].get<std::string>();
        if (config.conf.contains("http_port")) httpPort = config.conf["http_port"].get<int>();
        if (config.conf.contains("tcp_host"))  tcpHost  = config.conf["tcp_host"].get<std::string>();
        if (config.conf.contains("tcp_port"))  tcpPort  = config.conf["tcp_port"].get<int>();
        config.release();

        copyToBuffers();

        gui::menu.registerEntry(name, menuHandler, this, NULL);
    }

    ~WebMapModule() {
        stopServer();
        gui::menu.removeEntry(name);
    }

    void postInit() override {}
    void enable()  override {}
    void disable() override {}
    bool isEnabled() override { return true; }

private:
    // ------------------------------------------------------------- GUI ---
    static void menuHandler(void* ctx) {
        ((WebMapModule*)ctx)->drawMenu();
    }

    void drawMenu() {
        const float width = ImGui::GetContentRegionAvail().x;
        const bool serverRunning = isServerRunning();

        ImGui::BeginDisabled(serverRunning);
        ImGui::LeftLabel("HTTP host:port");
        ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() + 8);
        if (ImGui::InputText(("##wm_http_addr_" + name).c_str(), httpAddrBuf, sizeof(httpAddrBuf))) {
            std::string h; int p;
            if (parseHostPort(httpAddrBuf, h, p)) {
                httpAddrError = false;
                httpHost = h;
                httpPort = p;
                saveString("http_host", httpHost);
                saveInt("http_port", httpPort);
            }
            else {
                httpAddrError = true;
            }
        }
        if (httpAddrError) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "invalid format, expected host:port");
        }
        ImGui::LeftLabel("TCP host:port");
        ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() + 8);
        if (ImGui::InputText(("##wm_tcp_addr_" + name).c_str(), tcpAddrBuf, sizeof(tcpAddrBuf))) {
            std::string h; int p;
            if (parseHostPort(tcpAddrBuf, h, p)) {
                tcpAddrError = false;
                tcpHost = h;
                tcpPort = p;
                saveString("tcp_host", tcpHost);
                saveInt("tcp_port", tcpPort);
            }
            else {
                tcpAddrError = true;
            }
        }
        if (tcpAddrError) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "invalid format, expected host:port");
        }
        ImGui::EndDisabled();

        ImGui::Spacing();

        const float btnW = width;
        if (!serverRunning) {
            if (ImGui::Button(("Start server##wm_start_" + name).c_str(), ImVec2(btnW, 0))) {
                startServer();
            }
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.20f, 0.20f, 1.0f));
            if (ImGui::Button(("Stop server##wm_stop_" + name).c_str(), ImVec2(btnW, 0))) {
                stopServer();
            }
            ImGui::PopStyleColor();
        }

        if (serverRunning) {
            if (ImGui::Button(("Send test point##wm_test_" + name).c_str(), ImVec2(width, 0))) {
                sendTestPoint();
            }
        }

        ImGui::Spacing();
        if (serverRunning) {
            ImGui::TextColored(ImVec4(0.20f, 0.85f, 0.40f, 1.0f), "[+] Running");
            ImGui::TextDisabled("http://%s:%d/", displayHost(httpHost).c_str(), httpPort);
            ImGui::TextDisabled("Browser clients: %d   Test points sent: %d",
                                clientCount.load(), testSeq.load());
            ImGui::TextDisabled("TCP input %s:%d   Decoders: %d   Objects: %d   Errors: %d",
                                displayHost(tcpHost).c_str(), tcpPort,
                                tcpCollector.clientCount(), objectCount.load(), ingestErrors.load());
        }
        else {
            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "[-] Stopped");
            if (!lastError.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s", lastError.c_str());
            }
        }
    }

    // ------------------------------------------------------- HTTP/SSE ---
    void setupRoutes(httplib::Server& svr) {
        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(kIndexHtml, "text/html; charset=utf-8");
        });

        // Vendored Leaflet (leaflet_assets.cpp) -- served from the same
        // origin as the map, no CDN dependency. Tiles (OSM) intentionally
        // come from the network -- see the tile layer comment in kIndexHtml.
        svr.Get("/leaflet.js", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(kLeafletJs, "application/javascript; charset=utf-8");
        });
        svr.Get("/leaflet.css", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(kLeafletCss, "text/css; charset=utf-8");
        });

        svr.Get("/api/ping", [this](const httplib::Request&, httplib::Response& res) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), R"({"status":"ok","clients":%d})", clientCount.load());
            res.set_content(buf, "application/json");
        });

        // Current state of every received object -- the browser downloads
        // this BEFORE connecting to /events, so it also sees points
        // received before the page was opened (SSE by itself only ever
        // moves forward in time).
        svr.Get("/api/snapshot", [this](const httplib::Request&, httplib::Response& res) {
            std::string body = "[";
            {
                std::lock_guard<std::mutex> lg(objectsMutex);
                bool first = true;
                for (auto& kv : objects) {
                    if (!first) body += ",";
                    body += kv.second;
                    first = false;
                }
            }
            body += "]";
            res.set_content(body, "application/json");
        });

        svr.Get("/events", [this](const httplib::Request&, httplib::Response& res) {
            auto client = std::make_shared<SseClient>();
            {
                std::lock_guard<std::mutex> lg(clientsMutex);
                clients.push_back(client);
            }
            clientCount++;

            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider(
                "text/event-stream",
                [client](size_t, httplib::DataSink& sink) -> bool {
                    std::unique_lock<std::mutex> lock(client->mtx);
                    // Shorter interval = lower worst-case delay on Stop if
                    // a client connects just outside the notify loop in
                    // stopServer() -- see the comment there.
                    client->cv.wait_for(lock, std::chrono::seconds(3), [&] {
                        return !client->queue.empty() || !client->active.load();
                    });
                    if (!client->active.load()) return false;
                    if (client->queue.empty()) {
                        lock.unlock();
                        return sink.write(": ping\n\n", 8);
                    }
                    std::string msg = std::move(client->queue.front());
                    client->queue.pop_front();
                    lock.unlock();
                    return sink.write(msg.c_str(), msg.size());
                },
                [this, client](bool) {
                    client->active = false;
                    std::lock_guard<std::mutex> lg(clientsMutex);
                    clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
                    clientCount--;
                });
        });
    }

    void broadcast(const std::string& eventName, const std::string& jsonPayload) {
        std::string frame = "event: " + eventName + "\ndata: " + jsonPayload + "\n\n";
        std::lock_guard<std::mutex> lg(clientsMutex);
        for (auto& c : clients) {
            {
                std::lock_guard<std::mutex> lg2(c->mtx);
                c->queue.push_back(frame);
            }
            c->cv.notify_one();
        }
    }

    void sendTestPoint() {
        int seq = ++testSeq;
        // deterministic drift so the log visibly shows the point "living"
        double lat = 49.7384 + (seq % 7) * 0.01;
        double lon = 13.3736 + (seq % 5) * 0.01;
        char buf[192];
        std::snprintf(buf, sizeof(buf),
            R"({"seq":%d,"name":"TEST-%d","lat":%.4f,"lon":%.4f,"type":"test"})",
            seq, seq, lat, lon);
        upsertAndBroadcast(buf);
    }

    // Shared path for "Send test point" and real data from the TCP
    // collector alike -- validation, storing into objects (for
    // /api/snapshot for newly connecting browsers), and broadcasting to
    // live SSE clients.
    void upsertAndBroadcast(const std::string& rawJsonLine) {
        json obj;
        try {
            obj = json::parse(rawJsonLine);
        }
        catch (...) {
            ingestErrors++;
            return;
        }
        if (!obj.contains("lat") || !obj.contains("lon") || !obj.contains("type")) {
            ingestErrors++;
            return;
        }

        std::string type = obj.value("type", "");
        std::string ident = obj.value("name", "");
        if (ident.empty()) ident = "#" + std::to_string(objectCount.load());
        std::string key = type + "|" + ident;

        {
            std::lock_guard<std::mutex> lg(objectsMutex);
            bool isNew = objects.find(key) == objects.end();
            objects[key] = rawJsonLine;
            if (isNew) objectCount++;
        }
        broadcast("object", rawJsonLine);
    }

    // ------------------------------------------------------- lifecycle ---
    bool isServerRunning() { return running.load(); }

    void startServer() {
        lastError.clear();
        if (running.load()) return;

        svr = std::make_unique<httplib::Server>();
        setupRoutes(*svr);

        // bind_to_port checks the port RIGHT NOW, on this (GUI) thread --
        // so a busy port shows up in the panel immediately, not as
        // silent nothing in the background. This is exactly that
        // desktop "collides with something" case.
        if (!svr->bind_to_port(httpHost.c_str(), httpPort)) {
            lastError = "Failed to bind HTTP " + httpHost + ":" +
                        std::to_string(httpPort) + " (port busy, or invalid address).";
            flog::error("web_map: {}", lastError);
            svr.reset();
            return;
        }

        // Start/Stop is atomic for both layers -- if the TCP collector
        // fails to bind, we also discard the already-successfully-bound
        // HTTP socket (svr.reset() closes it), so it never sits half-started.
        std::string tcpErr = tcpCollector.start(tcpHost, tcpPort,
            [this](const std::string& line) { upsertAndBroadcast(line); });
        if (!tcpErr.empty()) {
            lastError = "Failed to start the TCP collector on " + tcpHost + ":" +
                        std::to_string(tcpPort) + " (" + tcpErr + ").";
            flog::error("web_map: {}", lastError);
            svr.reset();
            return;
        }

        running = true;
        serverThread = std::thread([this]() {
            svr->listen_after_bind();  // blocks until stop()
            running = false;
        });

        flog::info("web_map: HTTP {}:{}  TCP {}:{}", httpHost, httpPort, tcpHost, tcpPort);
    }

    void stopServer() {
        if (!svr && !running.load() && !tcpCollector.isRunning()) return;

        // MUST wake any SSE clients parked in the wait_for BEFORE joining
        // serverThread below. cpp-httplib's listen_after_bind() only
        // returns once its internal thread pool has joined every in-flight
        // request handler (ThreadPool::shutdown() in httplib.h) -- and our
        // /events handler can sit blocked in that wait for up to
        // kSseHeartbeatSec. Doing this notify AFTER the join (the original
        // bug here) means the whole app stalls for however much of that
        // wait was left when Stop got pressed -- that's the multi-second
        // freeze. Notifying first lets those handler threads exit almost
        // immediately, so the join below returns fast.
        {
            std::lock_guard<std::mutex> lg(clientsMutex);
            for (auto& c : clients) {
                c->active = false;
                c->cv.notify_all();
            }
            clients.clear();
        }
        clientCount = 0;

        if (svr) svr->stop();
        if (serverThread.joinable()) serverThread.join();
        tcpCollector.stop();  // also joins every client thread, see tcp_collector.cpp

        svr.reset();
        running = false;
        flog::info("web_map: server stopped");
    }

    static std::string displayHost(const std::string& h) {
        if (h == "0.0.0.0" || h.empty()) return "127.0.0.1";
        return h;
    }

    // Parses "host:port" (the merged address field in the panel). Splits on
    // the LAST ':' rather than the first, so it doesn't break if a hostname
    // itself ever contained one -- harmless for plain IPv4 today, cheap
    // correctness margin for later.
    static bool parseHostPort(const std::string& s, std::string& outHost, int& outPort) {
        size_t colon = s.rfind(':');
        if (colon == std::string::npos || colon == 0 || colon == s.size() - 1) return false;
        std::string host = s.substr(0, colon);
        std::string portStr = s.substr(colon + 1);
        for (char c : portStr) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        int port = std::atoi(portStr.c_str());
        if (port < 1 || port > 65535) return false;
        outHost = host;
        outPort = port;
        return true;
    }

    // ----------------------------------------------- config helpers ---
    void saveString(const char* key, const std::string& v) {
        config.acquire();
        config.conf[key] = v;
        config.release(true);
    }
    void saveInt(const char* key, int v) {
        config.acquire();
        config.conf[key] = v;
        config.release(true);
    }
    void copyToBuffers() {
        std::snprintf(httpAddrBuf, sizeof(httpAddrBuf), "%s:%d", httpHost.c_str(), httpPort);
        std::snprintf(tcpAddrBuf, sizeof(tcpAddrBuf), "%s:%d", tcpHost.c_str(), tcpPort);
    }

    // ------------------------------------------------------- fields ---
    std::string name;

    std::string httpHost = "0.0.0.0";
    int         httpPort = 8073;
    std::string tcpHost  = "0.0.0.0";
    int         tcpPort  = 8093;

    char httpAddrBuf[64]{};
    char tcpAddrBuf[64]{};
    bool httpAddrError = false;
    bool tcpAddrError  = false;

    std::unique_ptr<httplib::Server> svr;
    std::thread       serverThread;
    std::atomic<bool> running{false};
    std::string       lastError;

    std::vector<std::shared_ptr<SseClient>> clients;
    std::mutex        clientsMutex;
    std::atomic<int>  clientCount{0};
    std::atomic<int>  testSeq{0};

    // TCP collector + last known state of every object (type|identity ->
    // raw JSON line), for /api/snapshot for newly connecting browsers.
    TcpCollector                       tcpCollector;
    std::map<std::string, std::string> objects;
    std::mutex                         objectsMutex;
    std::atomic<int>                   objectCount{0};
    std::atomic<int>                   ingestErrors{0};

    static constexpr const char* kIndexHtml =
        R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>web_map</title>
<link rel="stylesheet" href="/leaflet.css">
<style>
html, body, #map { height: 100%; margin: 0; padding: 0; background: #111; }
#status {
  position: fixed; top: 8px; left: 8px; z-index: 1000;
  background: rgba(17,17,17,.85); color: #ddd; font-family: sans-serif;
  font-size: 13px; padding: 6px 10px; border-radius: 6px;
}
.wm-marker { background: transparent; border: none; }
.wm-dot {
  width: 14px; height: 14px; border-radius: 50%; background: #e0433c;
  border: 2px solid #fff; box-shadow: 0 0 3px rgba(0,0,0,.7);
}
</style>
</head>
<body>
<div id="status">connecting&hellip;</div>
<div id="map"></div>
<script src="/leaflet.js"></script>
<script>
const statusEl = document.getElementById('status');

// Default view over the test data (Plzen); once the TCP collector is
// wired up, the first received point could re-center the map itself.
const map = L.map('map').setView([49.7384, 13.3736], 12);

// Tiles intentionally from OSM online (agreed) -- only the Leaflet
// library itself is vendored and served locally from this module.
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom: 19,
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
}).addTo(map);

const dotIcon = L.divIcon({
  className: 'wm-marker',
  html: '<div class="wm-dot"></div>',
  iconSize: [14, 14],
  iconAnchor: [7, 7]
});

const markers = new Map();

// Client-side trail: bounded position history per object key, drawn as a
// polyline behind the marker. Universal mechanism, not radiosonde-specific
// (matches how F4JTV's map.js does it -- global mechanism, not per-type
// hardcoding) -- any object that moves gets a trail, a static one-shot
// object just ends up with an invisible single-point "trail". Session-only:
// starts empty on page load, no server-side history (deliberately deferred,
// see the file header comment).
const trails = new Map();
const trailLines = new Map();
const MAX_TRAIL_POINTS = 2000;

const markerColor = '#e0433c';

function setStatus(connected) {
  const n = markers.size;
  statusEl.textContent = connected
    ? 'connected \u2014 ' + n + ' points on map'
    : 'reconnecting...';
}

// same key composition as the backend (type|identity) -- see
// upsertAndBroadcast in main.cpp -- so the same object from the snapshot
// and from live SSE both point at the same marker.
function keyOf(obj) {
  return (obj.type || '') + '|' + (obj.name || obj.seq || '?');
}

function upsert(obj) {
  const key = keyOf(obj);
  const pos = [obj.lat, obj.lon];

  let hist = trails.get(key);
  if (!hist) { hist = []; trails.set(key, hist); }
  hist.push(pos);
  if (hist.length > MAX_TRAIL_POINTS) hist.shift();

  let line = trailLines.get(key);
  if (!line) {
    line = L.polyline(hist, { color: markerColor, weight: 2, opacity: 0.55 }).addTo(map);
    trailLines.set(key, line);
  } else {
    line.setLatLngs(hist);
  }

  let m = markers.get(key);
  if (!m) {
    m = L.marker(pos, { icon: dotIcon }).addTo(map);
    markers.set(key, m);
  } else {
    m.setLatLng(pos);
  }
  m.bindPopup('<pre style="margin:0">' + JSON.stringify(obj, null, 1) + '</pre>');
  setStatus(true);
}

function removeById(id) {
  const m = markers.get(id);
  if (m) { map.removeLayer(m); markers.delete(id); }
  const line = trailLines.get(id);
  if (line) { map.removeLayer(line); trailLines.delete(id); }
  trails.delete(id);
  setStatus(true);
}

// Snapshot before connecting to SSE -- without this a new browser would
// only see points received AFTER the page was opened.
fetch('/api/snapshot')
  .then((r) => r.json())
  .then((arr) => arr.forEach(upsert))
  .catch(() => {});

const es = new EventSource('/events');
es.onopen = () => setStatus(true);
es.onerror = () => setStatus(false);
es.addEventListener('object', (e) => upsert(JSON.parse(e.data)));
es.addEventListener('remove', (e) => removeById(JSON.parse(e.data).id));
</script>
</body></html>)HTML";
};

// ----------------------------------------- module manager wiring ----------
MOD_EXPORT void _INIT_() {
    json def = json({});
    def["http_host"] = "0.0.0.0";
    def["http_port"] = 8073;
    def["tcp_host"]  = "0.0.0.0";
    def["tcp_port"]  = 8093;
    config.setPath(core::args["root"].s() + "/web_map_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new WebMapModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (WebMapModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
