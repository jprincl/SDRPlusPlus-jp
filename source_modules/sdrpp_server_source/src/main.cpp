#include "sdrpp_server_client.h"
#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/widgets/stepped_slider.h>
#include <utils/optionlist.h>
#include <gui/dialogs/dialog_box.h>
#include <cstring>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "sdrpp_server_source",
    /* Description:     */ "SDR++ Server source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 2, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class SDRPPServerSourceModule : public ModuleManager::Instance {
public:
    SDRPPServerSourceModule(std::string name) {
        this->name = name;

        // Yeah no server-ception, sorry...
        if (core::args["server"].b()) { return; }

        // Initialize lists
        sampleTypeList.define("Int8", dsp::compression::PCM_TYPE_I8);
        sampleTypeList.define("Int16", dsp::compression::PCM_TYPE_I16);
        sampleTypeList.define("Float32", dsp::compression::PCM_TYPE_F32);
        sampleTypeId = sampleTypeList.valueId(dsp::compression::PCM_TYPE_I16);

        prebufferList.define("Disabled", 0);
        prebufferList.define("50 ms", 50);
        prebufferList.define("100 ms", 100);
        prebufferList.define("250 ms", 250);
        prebufferList.define("500 ms", 500);
        prebufferList.define("1000 ms", 1000);
        rxPrebufferId = prebufferList.valueId(100);

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        // Load config
        config.acquire();
        std::string hostStr = config.conf["hostname"];
        strcpy(hostname, hostStr.c_str());
        port = config.conf["port"];
        config.release();
        loadPasswordForHost();

        // Per-frame GUI-thread pump for server-pushed state (samplerate,
        // tuning limits) cached by the client's network worker. Bound in
        // menuSelected, unbound in menuDeselected.
        frameDrawHandler.ctx = this;
        frameDrawHandler.handler = frameDraw;

        sigpath::sourceManager.registerSource("SDR++ Server", &handler);
    }

    ~SDRPPServerSourceModule() {
        // Server mode: the constructor returned before registering.
        if (core::args["server"].b()) { return; }
        // If still selected, unregisterSource fires menuDeselected, which
        // unbinds frameDrawHandler.
        stop(this);
        sigpath::sourceManager.unregisterSource("SDR++ Server");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    std::string getBandwdithScaled(double bw) {
        char buf[1024];
        if (bw >= 1000000.0) {
            sprintf(buf, "%.1lfMHz", bw / 1000000.0);
        }
        else if (bw >= 1000.0) {
            sprintf(buf, "%.1lfKHz", bw / 1000.0);
        }
        else {
            sprintf(buf, "%.1lfHz", bw);
        }
        return std::string(buf);
    }

    static void menuSelected(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        // Select/deselect only ever runs on the GUI thread, outside
        // onFrameDraw.emit() (source combo, startup, module unregister), so
        // mutating the handler list here is safe. SourceManager strictly
        // pairs the two calls, keeping bind/unbind balanced.
        gui::mainWindow.onFrameDraw.bindHandler(&_this->frameDrawHandler);
        if (_this->client) {
            // Force re-apply of samplerate and tuning limits; another local
            // source may have overwritten them while it was selected.
            _this->client->syncRemoteState(true);
        }
        gui::mainWindow.playButtonLocked = !(_this->client && _this->client->isOpen());
        flog::info("SDRPPServerSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        gui::mainWindow.onFrameDraw.unbindHandler(&_this->frameDrawHandler);
        gui::mainWindow.playButtonLocked = false;
        // Release the remote limits so they don't leak into the next source.
        sigpath::sourceManager.clearTuningLimits();
        flog::info("SDRPPServerSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void frameDraw(MainWindow::FrameDrawArgs, void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        // Only bound while this source is selected, so this can't stomp
        // another source's samplerate or tuning limits.
        if (_this->connected()) {
            _this->client->syncRemoteState();
        }
    }

    static void start(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        if (_this->running) { return; }

        // Try to connect if not already connected (Play button is locked anyway so not sure why I put this here)
        if (!_this->connected()) {
            _this->tryConnect();
            if (!_this->connected()) { return; }
        }

        // Set configuration
        _this->client->setFrequency(_this->freq);
        _this->client->start();

        _this->running = true;
        flog::info("SDRPPServerSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        if (!_this->running) { return; }

        if (_this->connected()) { _this->client->stop(); }

        _this->running = false;
        flog::info("SDRPPServerSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;
        if (_this->running && _this->connected()) {
            _this->client->setFrequency(freq);
        }
        _this->freq = freq;
        flog::info("SDRPPServerSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        SDRPPServerSourceModule* _this = (SDRPPServerSourceModule*)ctx;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        bool connected = _this->connected();
        gui::mainWindow.playButtonLocked = !connected;

        ImGui::GenericDialog("##sdrpp_srv_src_err_dialog", _this->serverBusy, GENERIC_DIALOG_BUTTONS_OK, [=](){
            ImGui::TextUnformatted("This server is already in use.");
        });
        ImGui::GenericDialog("##sdrpp_srv_src_auth_dialog", _this->authFailed, GENERIC_DIALOG_BUTTONS_OK, [=](){
            ImGui::TextUnformatted("Authentication failed.");
        });

        if (connected) { style::beginDisabled(); }
        if (ImGui::InputText(CONCAT("##sdrpp_srv_srv_host_", _this->name), _this->hostname, 1023)) {
            config.acquire();
            config.conf["hostname"] = _this->hostname;
            config.release(true);
            _this->loadPasswordForHost();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(CONCAT("##sdrpp_srv_srv_port_", _this->name), &_this->port, 0, 0)) {
            config.acquire();
            config.conf["port"] = _this->port;
            config.release(true);
        }
        ImGui::LeftLabel("Password");
        ImGui::FillWidth();
        if (ImGui::InputText(CONCAT("##sdrpp_srv_srv_password_", _this->name), _this->password, sizeof(_this->password), ImGuiInputTextFlags_Password)) {
            config.acquire();
            config.conf["hosts"][std::string(_this->hostname)]["password"] = std::string(_this->password);
            config.release(true);
        }
        if (connected) { style::endDisabled(); }

        if (_this->running) { style::beginDisabled(); }
        if (!connected && ImGui::Button("Connect##sdrpp_srv_source", ImVec2(menuWidth, 0))) {
            _this->tryConnect();
        }
        else if (connected && ImGui::Button("Disconnect##sdrpp_srv_source", ImVec2(menuWidth, 0))) {
            _this->client->close();
        }
        if (_this->running) { style::endDisabled(); }


        if (connected) {
            ImGui::LeftLabel("Sample type");
            ImGui::FillWidth();
            if (ImGui::Combo("##sdrpp_srv_source_samp_type", &_this->sampleTypeId, _this->sampleTypeList.txt)) {
                _this->client->setSampleType(_this->sampleTypeList[_this->sampleTypeId]);

                // Save config
                config.acquire();
                config.conf["servers"][_this->devConfName]["sampleType"] = _this->sampleTypeList.key(_this->sampleTypeId);
                config.release(true);
            }
            
            if (ImGui::Checkbox("Compression", &_this->compression)) {
                _this->client->setCompression(_this->compression);

                // Save config
                config.acquire();
                config.conf["servers"][_this->devConfName]["compression"] = _this->compression;
                config.release(true);
            }

            ImGui::LeftLabel("RX prebuffer");
            ImGui::FillWidth();
            if (ImGui::Combo("##sdrpp_srv_source_rx_prebuf", &_this->rxPrebufferId, _this->prebufferList.txt)) {
                int prebufferMsec = _this->prebufferList[_this->rxPrebufferId];
                _this->client->setRxPrebufferMsec(prebufferMsec);
                config.acquire();
                config.conf["servers"][_this->devConfName]["rxPrebuffer"] = prebufferMsec;
                config.release(true);
            }

            bool dummy = true;
            style::beginDisabled();
            ImGui::Checkbox("Full IQ", &dummy);
            style::endDisabled();

            // Calculate datarate
            _this->frametimeCounter += ImGui::GetIO().DeltaTime;
            if (_this->frametimeCounter >= 0.2f) {
                _this->datarate = ((float)_this->client->bytes.exchange(0) / (_this->frametimeCounter * 1024.0f * 1024.0f)) * 8;
                _this->frametimeCounter = 0;
            }

            ImGui::TextUnformatted("Status:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected (%.3f Mbit/s, %d%% buffer)", _this->datarate, _this->client->getRxPrebufferPercent());

            ImGui::CollapsingHeader("Source [REMOTE]", ImGuiTreeNodeFlags_DefaultOpen);

            _this->client->showMenu();
        }
        else {
            ImGui::TextUnformatted("Status:");
            ImGui::SameLine();
            ImGui::TextUnformatted("Not connected (--.--- Mbit/s)");
        }
    }

    bool connected() {
        return client && client->isOpen();
    }

    void tryConnect() {
        try {
            serverBusy = false;
            authFailed = false;
            if (client) { client.reset(); }
            client = server::connect(hostname, port, &stream, password);
            deviceInit();
        }
        catch (const std::exception& e) {
            flog::error("Could not connect to SDR: {}", e.what());
            if (!strcmp(e.what(), "Server busy")) { serverBusy = true; }
            if (!strcmp(e.what(), "Authentication failed")) { authFailed = true; }
        }
    }

    void deviceInit() {
        // Generate the config name
        char buf[4096];
        sprintf(buf, "%s:%05d", hostname, port);
        devConfName = buf;

        // Load settings
        sampleTypeId = sampleTypeList.valueId(dsp::compression::PCM_TYPE_I16);
        if (config.conf["servers"][devConfName].contains("sampleType")) {
            std::string key = config.conf["servers"][devConfName]["sampleType"];
            if (sampleTypeList.keyExists(key)) { sampleTypeId = sampleTypeList.keyId(key); }
        }
        if (config.conf["servers"][devConfName].contains("compression")) {
            compression = config.conf["servers"][devConfName]["compression"];
        }
        rxPrebufferId = prebufferList.valueId(100);
        if (config.conf["servers"][devConfName].contains("rxPrebuffer")) {
            int prebufferMsec = config.conf["servers"][devConfName]["rxPrebuffer"];
            if (prebufferList.valueExists(prebufferMsec)) { rxPrebufferId = prebufferList.valueId(prebufferMsec); }
        }

        // Set settings
        client->setSampleType(sampleTypeList[sampleTypeId]);
        client->setCompression(compression);
        client->setRxPrebufferMsec(prebufferList[rxPrebufferId]);
    }

    void loadPasswordForHost() {
        config.acquire();
        password[0] = 0;
        std::string host = hostname;
        if (config.conf["hosts"].contains(host) && config.conf["hosts"][host].contains("password")) {
            std::string savedPassword = config.conf["hosts"][host]["password"];
            strncpy(password, savedPassword.c_str(), sizeof(password) - 1);
            password[sizeof(password) - 1] = 0;
        }
        config.release();
    }

    std::string name;
    bool enabled = true;
    bool running = false;

    double freq;
    bool serverBusy = false;
    bool authFailed = false;

    float datarate = 0;
    float frametimeCounter = 0;

    char hostname[1024];
    char password[256] = {};
    int port = 50000;
    std::string devConfName = "";

    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    EventHandler<MainWindow::FrameDrawArgs> frameDrawHandler;

    OptionList<std::string, dsp::compression::PCMType> sampleTypeList;
    OptionList<std::string, int> prebufferList;
    int sampleTypeId;
    int rxPrebufferId;
    bool compression = false;

    std::shared_ptr<server::Client> client;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["hostname"] = "localhost";
    def["port"] = 5259;
    def["servers"] = json::object();
    def["hosts"] = json::object();
    config.setPath(core::args["root"].s() + "/sdrpp_server_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SDRPPServerSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (SDRPPServerSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
