#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

// Minimal newline-delimited JSON TCP kolektor, protokolove kompatibilni s
// F4JTV/SDRPlusPlus_SwissKnifeEdition (misc_modules/sdr_map_launcher ->
// sdr_map/map/management/commands/listen_sdr.py): kazdy pripojeny dekoder
// posila JEDEN JSON radek na objekt:
//
//   {"name":"...","date":"YYYY-MM-DD","time":"HH:MM:SS","lat":..,"lon":..,
//    "type":"ADSB|AIS|APRS|APRS Meteo|lrrp|radiosonde|TETRA|SARSAT|satellite",
//    "speed":..,"info":"key=value ..."}
//
// My tenhle radek jen predame dal (viz ObjectHandler) -- zadny typovy
// rozbor "info" pole tady zatim neni (SARSAT/TETRA/ADSB extra enrichment
// atd.), takze porovnany F4JTV dekoder muze mirit na tenhle port beze
// zmeny svyho vystupniho kodu. Vice pripojeni najednou je ocekavane
// (kazdy dekoder modul ma vlastni TCP spojeni).
class TcpCollector {
public:
    using ObjectHandler = std::function<void(const std::string& rawJsonLine)>;

    TcpCollector() = default;
    ~TcpCollector();

    TcpCollector(const TcpCollector&) = delete;
    TcpCollector& operator=(const TcpCollector&) = delete;

    // Prazdny navratovy retezec = uspech. Jinak popis chyby (bind/listen).
    std::string start(const std::string& host, int port, ObjectHandler onObject);
    void stop();
    bool isRunning() const { return running_.load(); }
    int  clientCount() const { return clientCount_.load(); }

private:
    void acceptLoop();
    void clientLoop(
#if defined(_WIN32)
        SOCKET fd
#else
        int fd
#endif
    );

    ObjectHandler onObject_;
    std::atomic<bool> running_{false};

#if defined(_WIN32)
    SOCKET listenSock_ = INVALID_SOCKET;
    std::vector<SOCKET> clientSockets_;
#else
    int listenSock_ = -1;
    std::vector<int> clientSockets_;
#endif

    std::thread acceptThread_;
    std::mutex clientsMutex_;
    std::vector<std::thread> clientThreads_;
    std::atomic<int> clientCount_{0};
};
