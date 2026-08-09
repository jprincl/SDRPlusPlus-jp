/*
 * web_map — SDR++ module
 *
 * MVP krok: vlastní embedded HTTP/SSE server žijící přímo v procesu SDR++
 * (žádný subprocess, na rozdíl od F4JTV sdr_map_launcher, který spouští
 * Django). Tenhle soubor zatím NEOBSAHUJE TCP vrstvu pro příjem dat od
 * dekodérů (radiosonde, ft8/wspr...) — ta přijde v dalším kroku, na svém
 * vlastním portu. Účel téhle verze je ověřit, že:
 *
 *   1) server jde čistě nastartovat/zastavit ze Start/Stop tlačítka,
 *   2) obsazený port se pozná HNED (bind_to_port), ne až tichým pádem
 *      vlákna na desktopu, kde se to snadno srazí s něčím jiným,
 *   3) Server-Sent Events push funguje end-to-end (tlačítko "Send test
 *      point" v panelu → prohlížeč na /events dostane zprávu živě).
 *
 * Až tohle sedí, další krok je: (a) TCP kolektor na vlastním portu se
 * stejnou JSON-lines obálkou jako F4JTV dekodéry, (b) reálná Leaflet
 * stránka místo testovací /.
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

// cpp-httplib je vendorovaná single-header knihovna (MIT), viz httplib.h
// vedle tohoto souboru. Quoted include (ne <>): quoted lookup vždy hledá
// nejdřív adresář obsahujícího souboru, nezávisle na tom, jak si daný
// CMake/Ninja generátor rozhodne předat include cesty compileru -- na
// Android buildu se ukázalo, že úhlová varianta na to spoléhala a
// nefungovalo to. Bez OpenSSL flagu = čisté HTTP, žádné TLS -- pro
// lokální/LAN dev server v pořádku.
//
// cpp-httplib větev "USE_IF2IP" (vazba na síťové rozhraní podle jména) se
// aktivuje podmínkou "!defined ANDROID" -- ale NDK toolchain definuje
// __ANDROID__ (s podtržítky), ne holé ANDROID. Bez téhle opravy se ta
// větev na Androidu omylem zkompiluje a spadne na getifaddrs/freeifaddrs,
// které bionic sysroot bez dost vysokého API levelu nedeklaruje. My tuhle
// funkci (bind podle jména rozhraní) nepotřebujeme, takže ji takhle jen
// spolehlivě vypneme, aniž bychom sahali do samotného vendorovaného souboru.
#if defined(__ANDROID__) && !defined(ANDROID)
    #define ANDROID
#endif

#include "httplib.h"
#include "leaflet_assets.h"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <shellapi.h>
#endif

SDRPP_MOD_INFO{
    /* Name:            */ "web_map",
    /* Description:     */ "Embedded HTTP/SSE server pro živou mapu pozic (Leaflet přijde v dalším kroku).",
    /* Author:          */ "jprincl",
    /* Version:         */ 0, 1, 0,
    /* Max instances:   */ 1
};

ConfigManager config;

// ---------------------------------------------------------------- SSE ----
// Jeden otevřený /events spojení = jeden klient s vlastní frontou zpráv.
// broadcast() zprávu přidá do fronty všech aktivních klientů a probudí
// jejich content provider vlákno (viz setupRoutes).
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

        // NULL = žádný checkbox vedle položky v menu; Start/Stop v panelu
        // je ten skutečný vypínač (stejný důvod jako u sdr_map_launcher).
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
        ImGui::LeftLabel("HTTP host");
        ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() + 8);
        if (ImGui::InputText(("##wm_hh_" + name).c_str(), httpHostBuf, sizeof(httpHostBuf))) {
            httpHost = httpHostBuf;
            saveString("http_host", httpHost);
        }
        ImGui::LeftLabel("HTTP port");
        ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() + 8);
        if (ImGui::InputInt(("##wm_hp_" + name).c_str(), &httpPort, 0)) {
            if (httpPort < 1)     httpPort = 1;
            if (httpPort > 65535) httpPort = 65535;
            saveInt("http_port", httpPort);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::TextDisabled("Vstupní vrstva pro dekodéry (zatím neaktivní):");

        // TCP vstup je vždy disabled -- pole je tu jen jako náhled na
        // finální dvouportový design, dokud nepřidáme kolektor.
        ImGui::BeginDisabled(true);
        ImGui::LeftLabel("TCP host");
        ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() + 8);
        ImGui::InputText(("##wm_th_" + name).c_str(), tcpHostBuf, sizeof(tcpHostBuf));
        ImGui::LeftLabel("TCP port");
        ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() + 8);
        ImGui::InputInt(("##wm_tp_" + name).c_str(), &tcpPort, 0);
        ImGui::EndDisabled();

        ImGui::Spacing();

        const float btnW = (width - 8.0f) / 2.0f;
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
        ImGui::SameLine();
        if (ImGui::Button(("Open in browser##wm_open_" + name).c_str(), ImVec2(btnW, 0))) {
            openBrowser();
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
            ImGui::TextDisabled("Připojení klienti: %d   Odesláno testů: %d",
                                clientCount.load(), testSeq.load());
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

        // Vendorovaný Leaflet (leaflet_assets.cpp) -- servírováno ze stejného
        // originu jako mapa, žádná závislost na CDN. Dlaždice (OSM) naopak
        // úmyslně jedou z netu, viz komentář u tile layeru v kIndexHtml.
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
                    client->cv.wait_for(lock, std::chrono::seconds(15), [&] {
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
        // deterministický posun, aby bylo v logu vidět, že bod "žije"
        double lat = 49.7384 + (seq % 7) * 0.01;
        double lon = 13.3736 + (seq % 5) * 0.01;
        char buf[192];
        std::snprintf(buf, sizeof(buf),
            R"({"seq":%d,"name":"TEST-%d","lat":%.4f,"lon":%.4f,"type":"test"})",
            seq, seq, lat, lon);
        broadcast("object", buf);
    }

    // ------------------------------------------------------- lifecycle ---
    bool isServerRunning() { return running.load(); }

    void startServer() {
        lastError.clear();
        if (running.load()) return;

        svr = std::make_unique<httplib::Server>();
        setupRoutes(*svr);

        // bind_to_port ověří port HNED, v tomhle (GUI) vlákně -- takže
        // obsazený port se ukáže v panelu okamžitě, ne jako tiché nic na
        // pozadí. Tohle je přesně ten desktopový "s něčím se to srazí"
        // případ.
        if (!svr->bind_to_port(httpHost.c_str(), httpPort)) {
            lastError = "Nepodařilo se nabindovat " + httpHost + ":" +
                        std::to_string(httpPort) + " (port obsazený, nebo neplatná adresa).";
            flog::error("web_map: {}", lastError);
            svr.reset();
            return;
        }

        running = true;
        serverThread = std::thread([this]() {
            svr->listen_after_bind();  // blokuje do stop()
            running = false;
        });

        flog::info("web_map: HTTP/SSE server started on {}:{}", httpHost, httpPort);
    }

    void stopServer() {
        if (!svr && !running.load()) return;

        if (svr) svr->stop();
        if (serverThread.joinable()) serverThread.join();

        // klienty zaseknuté v čekání na frontu je potřeba probudit ručně --
        // svr->stop() zavře listening socket, ale nekopíruje se to
        // automaticky do našich vlastních condition_variable čekání.
        {
            std::lock_guard<std::mutex> lg(clientsMutex);
            for (auto& c : clients) {
                c->active = false;
                c->cv.notify_all();
            }
            clients.clear();
        }
        clientCount = 0;
        svr.reset();
        running = false;
        flog::info("web_map: server stopped");
    }

    // -------------------------------------------------- browser launch ---
    void openBrowser() {
        const std::string url = "http://" + displayHost(httpHost) + ":" + std::to_string(httpPort) + "/";
#if defined(_WIN32)
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
        (void)system(("open \"" + url + "\" >/dev/null 2>&1 &").c_str());
#else
        (void)system(("xdg-open \"" + url + "\" >/dev/null 2>&1 &").c_str());
#endif
    }

    static std::string displayHost(const std::string& h) {
        if (h == "0.0.0.0" || h.empty()) return "127.0.0.1";
        return h;
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
        std::strncpy(httpHostBuf, httpHost.c_str(), sizeof(httpHostBuf) - 1);
        std::strncpy(tcpHostBuf,  tcpHost.c_str(),  sizeof(tcpHostBuf) - 1);
    }

    // ------------------------------------------------------- fields ---
    std::string name;

    std::string httpHost = "0.0.0.0";
    int         httpPort = 8073;
    // Zatím jen persistované/zobrazené, TCP kolektor přijde v dalším kroku.
    std::string tcpHost  = "0.0.0.0";
    int         tcpPort  = 8093;

    char httpHostBuf[64]{};
    char tcpHostBuf[64]{};

    std::unique_ptr<httplib::Server> svr;
    std::thread       serverThread;
    std::atomic<bool> running{false};
    std::string       lastError;

    std::vector<std::shared_ptr<SseClient>> clients;
    std::mutex        clientsMutex;
    std::atomic<int>  clientCount{0};
    std::atomic<int>  testSeq{0};

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

// Vychozi pohled na test data (Plzen); az bude TCP kolektor, prvni
// prijaty bod muze mapu sam vycentrovat.
const map = L.map('map').setView([49.7384, 13.3736], 12);

// Dlazdice umyslne z OSM online (dohodnuto) -- jen samotna Leaflet
// knihovna je vendorovana a servirovana lokalne z tohoto modulu.
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

function setStatus(connected) {
  const n = markers.size;
  statusEl.textContent = connected
    ? 'connected \u2014 ' + n + ' bod\u016f na map\u011b'
    : 'reconnecting...';
}

function upsert(obj) {
  const key = obj.name || String(obj.seq);
  let m = markers.get(key);
  if (!m) {
    m = L.marker([obj.lat, obj.lon], { icon: dotIcon }).addTo(map);
    markers.set(key, m);
  } else {
    m.setLatLng([obj.lat, obj.lon]);
  }
  m.bindPopup('<pre style="margin:0">' + JSON.stringify(obj, null, 1) + '</pre>');
  setStatus(true);
}

function removeById(id) {
  const m = markers.get(id);
  if (!m) return;
  map.removeLayer(m);
  markers.delete(id);
  setStatus(true);
}

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
