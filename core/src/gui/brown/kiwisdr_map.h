#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <core.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <gui/style.h>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include "gui/widgets/simple_widgets.h"
#include "utils/proto/kiwisdr.h"
#include "utils/proto/http.h"
#include <gui/brown/geomap.h>



struct KiwiSDRMapSelector {

    geomap::GeoMap geoMap;
    std::shared_ptr<json> serversList;
    std::string serverListError;
    bool loadingList = false;
    std::mutex serversListMutex;
    std::string root;

    struct ServerTestState {
        std::mutex mutex;
        std::string status;
        std::string error;
        std::string lastTestedServer;
        std::string lastTestedServerLoc;
        bool inProgress = false;
    };

    struct ServerEntry {
        ImVec2 gps; // -1 .. 1 etc
        std::string name;
        std::string loc;
        std::string url;
        std::string antenna;
        float maxSnr;
        float secondSnr;
        int users, usersmax;
        bool selected = false;
    };

    std::vector<ServerEntry> servers;
    std::shared_ptr<ServerTestState> serverTestState = std::make_shared<ServerTestState>();
    std::thread serversListThread;
    const std::string configPrefix;

    ConfigManager* config;

    KiwiSDRMapSelector(const std::string& root, ConfigManager *config, const std::string& configPrefix) : configPrefix(configPrefix) {
        this->root = root;
        this->config = config;
        json def = json({});
        config->load(def);
        geoMap.loadFrom(*config, configPrefix.c_str()); // configPrefix is like "mapselector1_"
    }

    ~KiwiSDRMapSelector() {
        if (serversListThread.joinable()) {
            serversListThread.join();
        }
    }

    void openPopup() {
        ImGui::OpenPopup((configPrefix+": The KiwiSDR Map").c_str());
    }

    void drawPopup(std::function<void(const std::string&, const std::string&)> onSelected) {
        ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.125f);
        if (ImGui::BeginPopupModal((configPrefix+": The KiwiSDR Map").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            // Set the modal dialog's width and height
            const ImVec2 ws = ImGui::GetIO().DisplaySize * 0.75f;
            ImGui::SetWindowSize(ws);
            ImGui::BeginChild("##geomap-kiwisdr", ws - ImVec2(0, 50), true, 0);
            geoMap.draw();
            if (geoMap.scaleTranslateDirty) {
                geoMap.saveTo(*config, configPrefix.c_str());
                geoMap.scaleTranslateDirty = false;
            }
            std::shared_ptr<json> currentServersList;
            std::string currentServerListError;
            bool currentLoadingList = false;
            {
                std::lock_guard<std::mutex> lock(serversListMutex);
                currentServersList = serversList;
                currentServerListError = serverListError;
                currentLoadingList = loadingList;
            }
            if (!currentServersList) {
                if (!currentServerListError.empty()) {
                    ImGui::Text("%s", currentServerListError.c_str());
                }
                else {
                    ImGui::Text("Loading KiwiSDR servers list..");
                    if (!currentLoadingList) {
                        {
                            std::lock_guard<std::mutex> lock(serversListMutex);
                            loadingList = true;
                        }
                        if (serversListThread.joinable()) {
                            serversListThread.join();
                        }
                        serversListThread = std::thread([&]() {
                            auto loadedServersList = loadServersList();
                            std::vector<ServerEntry> loadedServers;

                            if (loadedServersList) {
                                int totallyParsed = 0;
                                for (const auto& entry : *loadedServersList) {
                                    ServerEntry serverEntry;

                                    // Check if all required fields are present
                                    if (entry.contains("gps") && entry.contains("name") && entry.contains("url") &&
                                        entry.contains("snr") && entry.contains("users") && entry.contains("users_max") && entry.contains("offline")) {

                                        if (entry["offline"].get<std::string>() == "no") {
                                            std::string gps_str = entry["gps"].get<std::string>();
                                            geomap::GeoCoordinates geo = {0.0, 0.0};

                                            std::stringstream ss(gps_str);
                                            ss.imbue(std::locale::classic());          // force '.' as decimal separator

                                            char discard;
                                            ss >> discard          // '('
                                               >> geo.latitude
                                               >> discard          // ','
                                               >> geo.longitude
                                               >> discard;         // ')'

                                            if (!ss) {
                                                flog::warn("Parsing geo coordinates failed: \"{}\"", gps_str);
                                            } else {
                                                serverEntry.gps = geomap::geoToCartesian(geo).toImVec2();
                                                serverEntry.name = entry["name"].get<std::string>();
                                                serverEntry.loc = entry["loc"].get<std::string>();
                                                serverEntry.url = entry["url"].get<std::string>();
                                                if (entry.contains("antenna")) {
                                                    serverEntry.antenna = entry["antenna"].get<std::string>();
                                                }
                                                sscanf(entry["snr"].get<std::string>().c_str(), "%f,%f", &serverEntry.maxSnr, &serverEntry.secondSnr);
                                                serverEntry.users = atoi(entry["users"].get<std::string>().c_str());
                                                serverEntry.usersmax = atoi(entry["users_max"].get<std::string>().c_str());
                                                loadedServers.push_back(serverEntry);
                                                totallyParsed++;
                                            }
                                        }
                                    }
                                }
                                flog::info("Parsed {} servers",totallyParsed);
                            }

                            std::sort(loadedServers.begin(), loadedServers.end(), [](const ServerEntry& a, const ServerEntry& b) {
                                return a.maxSnr < b.maxSnr;
                            });

                            std::lock_guard<std::mutex> lock(serversListMutex);
                            servers = std::move(loadedServers);
                            serversList = loadedServersList;
                            loadingList = false;
                        });
                    }
                }
            }
            else {
                ImGui::Text("Loaded servers list");
                auto sz = style::baseFont->FontSize;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                for (auto& s : servers) {
                    if (s.users < s.usersmax) {
                        auto dest = geoMap.recentMapToScreen(s.gps);
                        auto color = ImColor(0.3f, 0.3f, 0.3f);
                        if (s.maxSnr > 22) {
                            color = ImColor(0.0f, 1.0f, 0.0f);
                        }
                        else if (s.maxSnr > 12) {
                            color = ImColor(0.6f, 0.6f, 0.6f);
                        }
                        drawList->AddRectFilled(geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2), geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2), color, sz / 4.0f);
                        if (s.selected) {
                            drawList->AddRect(geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2), geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2), ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
                            drawList->AddRect(geoMap.recentCanvasPos + dest - ImVec2(sz / 2 - 1, sz / 2 - 1), geoMap.recentCanvasPos + dest + ImVec2(sz / 2 - 1, sz / 2 - 1), ImColor(1.0f, 1.0f, 0.0f), sz / 4.0f);
                        }
                        else {
                            drawList->AddRect(geoMap.recentCanvasPos + dest - ImVec2(sz / 2, sz / 2), geoMap.recentCanvasPos + dest + ImVec2(sz / 2, sz / 2), ImColor(0.0f, 0.0f, 0.0f), sz / 4.0f);
                        }
                    }
                }
                if (ImGui::IsMouseClicked(0) && ImGui::GetMouseClickedCount(0) == 1) {
                    auto clickPos = ImGui::GetMousePos() - geoMap.recentCanvasPos;
                    auto radius = sz / 2;
                    for (int q = 0; q < 2; q++) {
                        auto found = false;
                        for (int i = servers.size() - 1; i >= 0; i--) {
                            auto& it = servers[i];
                            auto dest = geoMap.recentMapToScreen(it.gps);
                            auto dist = sqrt(pow(dest.x - clickPos.x, 2) + pow(dest.y - clickPos.y, 2));
                            if (dist < radius) {
                                found = true;
                                for (auto& s : servers) {
                                    s.selected = false;
                                }
                                auto it0 = it;
                                it0.selected = true;
                                servers.emplace_back(it0);
                                servers.erase(servers.begin() + i);
                                break;
                            }
                        }
                        if (found) {
                            break;
                        }
                        radius *= 5;
                    }
                }
                for (auto& s : servers) {
                    if (s.selected) {
                        ImGui::Text("%s", s.name.c_str());
                        ImGui::Text("%s", s.loc.c_str());
                        if (!s.antenna.empty()) {
                            ImGui::Text("ANT: %s", s.antenna.c_str());
                        }
                        if (s.maxSnr > 0) {
                            ImGui::Text("SNR: %d", (int)s.maxSnr);
                        }
                        if (s.usersmax > 0) {
                            ImGui::Text("USR: %d/%d", s.users, s.usersmax);
                        }
                        ImGui::Text("URL: %s", s.url.c_str());
                        bool currentTestInProgress = false;
                        {
                            std::lock_guard<std::mutex> lock(serverTestState->mutex);
                            currentTestInProgress = serverTestState->inProgress;
                        }
                        ImGui::BeginDisabled(currentTestInProgress);
                        auto doTest = doFingerButton("Test server");
                        ImGui::EndDisabled();
                        if (doTest) {
                            auto testState = serverTestState;
                            bool startTest = false;
                            {
                                std::lock_guard<std::mutex> lock(testState->mutex);
                                if (!testState->inProgress) {
                                    testState->inProgress = true;
                                    testState->status = "Testing server " + s.url + " ...";
                                    testState->error.clear();
                                    testState->lastTestedServer.clear();
                                    testState->lastTestedServerLoc.clear();
                                    startTest = true;
                                }
                            }
                            if (startTest) {
                                ServerEntry server = s;
                                std::thread tester([testState, server]() {
                                    auto setStatus = [testState](const std::string& status) {
                                        std::lock_guard<std::mutex> lock(testState->mutex);
                                        testState->status = status;
                                    };
                                    auto finishTest = [testState]() {
                                        std::lock_guard<std::mutex> lock(testState->mutex);
                                        testState->inProgress = false;
                                    };
                                    KiwiSDRClient testClient;
                                    std::atomic<bool> plannedDisconnect{false};
                                    std::atomic<bool> connected{false};
                                    std::atomic<bool> disconnected{false};
                                    try {
                                        if (server.url.find("http://") == 0) {
                                            auto hostPort = server.url.substr(7);
                                            auto loc = server.loc;
                                            auto lastSlash = hostPort.find("/");
                                            if (lastSlash != std::string::npos) {
                                                hostPort = hostPort.substr(0, lastSlash);
                                            }
                                            setStatus("Testing server " + hostPort + "...");
                                            testClient.init(hostPort);
                                            using namespace std::chrono_literals;
                                            using Clock = std::chrono::steady_clock;
                                            testClient.onConnected = [&]() {
                                                connected = true;
                                                setStatus("Connected to server " + hostPort + " ...");
                                                testClient.tune(14074000, KiwiSDRClient::TUNE_IQ);
                                            };
                                            testClient.onDisconnected = [&]() {
                                                disconnected = true;
                                                std::lock_guard<std::mutex> lock(testState->mutex);
                                                if (plannedDisconnect.load()) {
                                                    testState->status = "Got some data. Server OK: " + server.url;
                                                    testState->lastTestedServer = hostPort;
                                                    testState->lastTestedServerLoc = loc;
                                                }
                                                else {
                                                    testState->status = "Disconnect, no data. Server NOT OK: " + server.url;
                                                }
                                            };
                                            testClient.start();
                                            auto start = Clock::now();
                                            while (true) {
                                                if (disconnected.load()) {
                                                    break;
                                                }
                                                testClient.iqDataLock.lock();
                                                auto bufsize = testClient.iqData.size();
                                                testClient.iqDataLock.unlock();
                                                std::this_thread::sleep_for(100ms);
                                                if (bufsize > 0) {
                                                    plannedDisconnect = true;
                                                    break;
                                                }
                                                if (Clock::now() - start > 5s) {
                                                    break;
                                                }
                                            }
                                            testClient.stop();
                                            if (connected.load()) {
                                                auto disconnectWaitStart = Clock::now();
                                                while (!disconnected.load() && Clock::now() - disconnectWaitStart < 5s) {
                                                    std::this_thread::sleep_for(100ms);
                                                }
                                                flog::info("Disconnected ok");
                                            }
                                            else {
                                                setStatus("Could not connect to server: " + server.url);
                                                std::this_thread::sleep_for(1s);
                                            }
                                        }
                                        else {
                                            setStatus("Non-http url " + server.url);
                                        }
                                    }
                                    catch (const std::exception& e) {
                                        std::lock_guard<std::mutex> lock(testState->mutex);
                                        testState->status = "Server test error";
                                        testState->error = e.what();
                                    }
                                    finishTest();
                                });
                                tester.detach();
                            }
                        }
                    }
                }
                std::string currentServerTestStatus;
                std::string currentServerTestError;
                {
                    std::lock_guard<std::mutex> lock(serverTestState->mutex);
                    currentServerTestStatus = serverTestState->status;
                    currentServerTestError = serverTestState->error;
                }
                if (!currentServerTestStatus.empty()) {
                    ImGui::Text("%s", currentServerTestStatus.c_str());
                }
                if (!currentServerTestError.empty()) {
                    ImGui::Text("Server test error: %s", currentServerTestError.c_str());
                }
            }

            ImGui::EndChild();
            // Display some text in the modal dialog
            //            ImGui::Text("This is a modal dialog box with specified width and height.");

            // Close button
            if (doFingerButton("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            std::string currentLastTestedServer;
            std::string currentLastTestedServerLoc;
            {
                std::lock_guard<std::mutex> lock(serverTestState->mutex);
                currentLastTestedServer = serverTestState->lastTestedServer;
                currentLastTestedServerLoc = serverTestState->lastTestedServerLoc;
            }
            if (currentLastTestedServer != "") {
                ImGui::SameLine();
                if (doFingerButton("Use tested server: " + currentLastTestedServer)) {
                    onSelected(currentLastTestedServer,currentLastTestedServerLoc);
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
    }

    std::shared_ptr<json> loadServersList() {
        // http://rx.linkfanel.net/kiwisdr_com.js
        try {

            std::string jsoncache = root + "/kiwisdr_source.receiverlist.json";

            auto status = std::filesystem::status(jsoncache);
            if (exists(status)) {
                const std::filesystem::file_time_type last_write_time = std::filesystem::last_write_time(jsoncache);
                auto last_write_time_sys_clock = std::chrono::time_point_cast<std::chrono::system_clock::duration>(last_write_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());

                if (std::chrono::system_clock::now() - last_write_time_sys_clock < std::chrono::hours(1)) {
                    std::ifstream ifs(jsoncache);
                    std::string content((std::istreambuf_iterator<char>(ifs)),
                                        (std::istreambuf_iterator<char>()));
                    return std::make_shared<json>(json::parse(content));
                }
            }

            std::string host = "rx.linkfanel.net";
            auto controlSock = net::connect(host, 80);
            auto controlHttp = net::http::Client(controlSock);

            // Make request
            net::http::RequestHeader rqhdr(net::http::METHOD_GET, "/kiwisdr_com.js", host);
            controlHttp.sendRequestHeader(rqhdr);
            net::http::ResponseHeader rshdr;
            controlHttp.recvResponseHeader(rshdr, 5000);

            flog::debug("Response from {}: {}", host, rshdr.getStatusString());
            std::vector<uint8_t> data(2000000, 0);
            std::string response;
            while (true) {
                auto len = controlSock->recv(data.data(), data.size());
                if (len < 1) {
                    break;
                }
                response += std::string((char*)data.data(), len);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            controlSock->close();
            auto BEGIN = "var kiwisdr_com =";
            auto END = "},\n]\n;";
            auto beginIx = response.find(BEGIN);
            if (beginIx == std::string::npos) {
                throw std::runtime_error("Invalid response from server");
            }
            auto endIx = response.rfind(END);
            if (endIx == std::string::npos) {
                throw std::runtime_error("Invalid response from server");
            }
            response = response.substr(beginIx + strlen(BEGIN), endIx - (beginIx + strlen(BEGIN)));
            response += "}]"; // fix trailing comma unsupported by parser

            FILE* toSave = fopen(jsoncache.c_str(), "wt");
            if (toSave) {
                fwrite(response.c_str(), 1, response.size(), toSave);
                fclose(toSave);
            }

            return std::make_shared<json>(json::parse(response));
        }
        catch (std::exception& e) {
            std::lock_guard<std::mutex> lock(serversListMutex);
            serverListError = e.what();
            return std::shared_ptr<json>();
        }
    }
};
