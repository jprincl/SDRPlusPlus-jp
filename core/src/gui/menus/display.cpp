#include <gui/menus/display.h>
#include <imgui.h>
#include <gui/gui.h>
#include <gui/menus/theme.h>
#include <core.h>
#include <backend.h>
#include <gui/colormaps.h>
#include <gui/gui.h>
#include <gui/main_window.h>
#include <signal_path/signal_path.h>
#include <gui/style.h>
#include <utils/optionlist.h>
#include <algorithm>

namespace displaymenu {
    bool showWaterfall;
    bool fullWaterfallUpdate = true;
    int colorMapId = 0;
    std::vector<std::string> colorMapNames;
    std::string colorMapNamesTxt = "";
    std::string colorMapAuthor = "";
    int selectedWindow = 0;
    int fftRate = 20;
    int fftSizeId = 0;
    int uiScaleFactorId = 0;
    bool fftHold = false;
    int fftHoldSpeed = 60;
    bool fftSmoothing = false;
    int fftSmoothingSpeed = 100;

    OptionList<int, int> fftSizes;
    OptionList<float, float> uiScaleFactors;
    OptionList<std::string, IQFrontEnd::FFTWindow> fftWindows;

    void updateFFTSpeeds() {
        gui::waterfall.setFFTHoldSpeed((float)fftHoldSpeed / ((float)fftRate * 10.0f));
        gui::waterfall.setFFTSmoothingSpeed(std::min<float>((float)fftSmoothingSpeed / (float)(fftRate * 10.0f), 1.0f));
    }

    void init() {
        // Define FFT sizes
        // 1M disabled until the waterfall allocation path is hardened: it stores
        // rawFFTSize * waterfallHeight floats (>1 GiB at a few hundred rows) and
        // reallocs without checking for failure.
        // fftSizes.define(1048576, "1048576", 1048576);
        fftSizes.define(524288, "524288", 524288);
        fftSizes.define(262144, "262144", 262144);
        fftSizes.define(131072, "131072", 131072);
        fftSizes.define(65536, "65536", 65536);
        fftSizes.define(32768, "32768", 32768);
        fftSizes.define(16384, "16384", 16384);
        fftSizes.define(8192, "8192", 8192);
        fftSizes.define(4096, "4096", 4096);
        fftSizes.define(2048, "2048", 2048);
        fftSizes.define(1024, "1024", 1024);

        showWaterfall = core::configManager.conf["showWaterfall"];
        showWaterfall ? gui::waterfall.showWaterfall() : gui::waterfall.hideWaterfall();
        std::string colormapName = core::configManager.conf["colorMap"];
        if (colormaps::maps.find(colormapName) != colormaps::maps.end()) {
            colormaps::Map map = colormaps::maps[colormapName];
            gui::waterfall.updatePalletteFromArray(map.map, map.entryCount);
        }

        for (auto const& [name, map] : colormaps::maps) {
            colorMapNames.push_back(name);
            colorMapNamesTxt += name;
            colorMapNamesTxt += '\0';
            if (name == colormapName) {
                colorMapId = (colorMapNames.size() - 1);
                colorMapAuthor = map.author;
            }
        }

        fullWaterfallUpdate = core::configManager.conf["fullWaterfallUpdate"];
        gui::waterfall.setFullWaterfallUpdate(fullWaterfallUpdate);

        fftSizeId = fftSizes.valueId(65536);
        int size = core::configManager.conf["fftSize"];
        if (fftSizes.keyExists(size)) {
            fftSizeId = fftSizes.keyId(size);
        }
        sigpath::iqFrontEnd.setFFTSize(fftSizes.value(fftSizeId));

        fftRate = core::configManager.conf["fftRate"];
        sigpath::iqFrontEnd.setFFTRate(fftRate);

        // Define FFT windows, in order of increasing dynamic range
        fftWindows.define("Rectangular", "Rectangular", IQFrontEnd::FFTWindow::RECTANGULAR);
        fftWindows.define("Hamming", "Hamming", IQFrontEnd::FFTWindow::HAMMING);
        fftWindows.define("Hann", "Hann", IQFrontEnd::FFTWindow::HANN);
        fftWindows.define("Blackman", "Blackman", IQFrontEnd::FFTWindow::BLACKMAN);
        fftWindows.define("Nuttall", "Nuttall", IQFrontEnd::FFTWindow::NUTTALL);
        fftWindows.define("Blackman-Harris 4", "Blackman-Harris 4", IQFrontEnd::FFTWindow::BLACKMAN_HARRIS4);
        fftWindows.define("Blackman-Harris 7", "Blackman-Harris 7", IQFrontEnd::FFTWindow::BLACKMAN_HARRIS7);

        // The window is stored by name; legacy configs stored an index into
        // the {Rectangular, Blackman, Nuttall} list.
        std::string winName = "Nuttall";
        json fftWindowConf = core::configManager.conf["fftWindow"];
        if (fftWindowConf.is_string()) {
            winName = fftWindowConf;
        }
        else if (fftWindowConf.is_number_integer()) {
            const char* legacyWindows[] = { "Rectangular", "Blackman", "Nuttall" };
            winName = legacyWindows[std::clamp<int>(fftWindowConf, 0, 2)];
            core::configManager.acquire();
            core::configManager.conf["fftWindow"] = winName;
            core::configManager.release(true);
        }
        selectedWindow = fftWindows.keyExists(winName) ? fftWindows.keyId(winName) : fftWindows.keyId("Nuttall");
        sigpath::iqFrontEnd.setFFTWindow(fftWindows.value(selectedWindow));

        gui::menu.locked = core::configManager.conf["lockMenuOrder"];

        fftHold = core::configManager.conf["fftHold"];
        fftHoldSpeed = core::configManager.conf["fftHoldSpeed"];
        gui::waterfall.setFFTHold(fftHold);
        fftSmoothing = core::configManager.conf["fftSmoothing"];
        fftSmoothingSpeed = core::configManager.conf["fftSmoothingSpeed"];
        gui::waterfall.setFFTSmoothing(fftSmoothing);
        updateFFTSpeeds();

        // Define and load UI scale factor options
        uiScaleFactors.define(0.50f, "50%",  0.50f);
        uiScaleFactors.define(0.75f, "75%",  0.75f);
        uiScaleFactors.define(1.00f, "100%", 1.00f);
        uiScaleFactors.define(1.50f, "150%", 1.50f);
        uiScaleFactors.define(2.00f, "200%", 2.00f);
        float factor = core::configManager.conf["uiScaleFactor"];
        uiScaleFactorId = uiScaleFactors.valueId(factor);
        if (uiScaleFactorId < 0) { uiScaleFactorId = uiScaleFactors.valueId(1.00f); }
    }

    void setWaterfallShown(bool shown) {
        showWaterfall = shown;
        showWaterfall ? gui::waterfall.showWaterfall() : gui::waterfall.hideWaterfall();
        core::configManager.acquire();
        core::configManager.conf["showWaterfall"] = showWaterfall;
        core::configManager.release(true);
    }

    void checkKeybinds() {
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            setWaterfallShown(!showWaterfall);
        }
    }

    void draw(void* ctx) {
        thememenu::draw(ctx);

        if (ImGui::Checkbox("Show Waterfall##_sdrpp", &showWaterfall)) {
            setWaterfallShown(showWaterfall);
        }

        if (ImGui::Checkbox("Full Waterfall Update##_sdrpp", &fullWaterfallUpdate)) {
            gui::waterfall.setFullWaterfallUpdate(fullWaterfallUpdate);
            core::configManager.acquire();
            core::configManager.conf["fullWaterfallUpdate"] = fullWaterfallUpdate;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("Lock Menu Order##_sdrpp", &gui::menu.locked)) {
            core::configManager.acquire();
            core::configManager.conf["lockMenuOrder"] = gui::menu.locked;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("FFT Hold##_sdrpp", &fftHold)) {
            gui::waterfall.setFFTHold(fftHold);
            core::configManager.acquire();
            core::configManager.conf["fftHold"] = fftHold;
            core::configManager.release(true);
        }
        ImGui::SameLine();
        ImGui::FillWidth();
        if (ImGui::InputInt("##sdrpp_fft_hold_speed", &fftHoldSpeed)) {
            updateFFTSpeeds();
            core::configManager.acquire();
            core::configManager.conf["fftHoldSpeed"] = fftHoldSpeed;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("FFT Smoothing##_sdrpp", &fftSmoothing)) {
            gui::waterfall.setFFTSmoothing(fftSmoothing);
            core::configManager.acquire();
            core::configManager.conf["fftSmoothing"] = fftSmoothing;
            core::configManager.release(true);
        }
        ImGui::SameLine();
        ImGui::FillWidth();
        if (ImGui::InputInt("##sdrpp_fft_smoothing_speed", &fftSmoothingSpeed)) {
            fftSmoothingSpeed = std::max<int>(fftSmoothingSpeed, 1);
            updateFFTSpeeds();
            core::configManager.acquire();
            core::configManager.conf["fftSmoothingSpeed"] = fftSmoothingSpeed;
            core::configManager.release(true);
        }

        if (ImGui::Checkbox("Touch-Friendly UI##_sdrpp", &style::touchStyle)) {
            style::applyScaledStyle(thememenu::applyTheme);
            core::configManager.acquire();
            core::configManager.conf["touchStyle"] = style::touchStyle;
            core::configManager.release(true);
        }

        ImGui::LeftLabel("UI Scale Adjustment");
        ImGui::FillWidth();
        if (ImGui::Combo("##sdrpp_ui_scale", &uiScaleFactorId, uiScaleFactors.txt)) {
            backend::setUserScaleFactor(uiScaleFactors[uiScaleFactorId]);
        }

        ImGui::LeftLabelFill("FFT Framerate");
        if (ImGui::InputInt("##sdrpp_fft_rate", &fftRate, 1, 10)) {
            fftRate = std::max<int>(1, fftRate);
            sigpath::iqFrontEnd.setFFTRate(fftRate);
            updateFFTSpeeds();
            core::configManager.acquire();
            core::configManager.conf["fftRate"] = fftRate;
            core::configManager.release(true);
        }

        ImGui::LeftLabelFill("FFT Size");
        if (ImGui::Combo("##sdrpp_fft_size", &fftSizeId, fftSizes.txt)) {
            sigpath::iqFrontEnd.setFFTSize(fftSizes.value(fftSizeId));
            core::configManager.acquire();
            core::configManager.conf["fftSize"] = fftSizes.key(fftSizeId);
            core::configManager.release(true);
        }

        ImGui::LeftLabelFill("FFT Window");
        if (ImGui::Combo("##sdrpp_fft_window", &selectedWindow, fftWindows.txt)) {
            sigpath::iqFrontEnd.setFFTWindow(fftWindows.value(selectedWindow));
            core::configManager.acquire();
            core::configManager.conf["fftWindow"] = fftWindows.key(selectedWindow);
            core::configManager.release(true);
        }

        if (colorMapNames.size() > 0) {
            ImGui::LeftLabelFill("Color Map");
            if (ImGui::Combo("##_sdrpp_color_map_sel", &colorMapId, colorMapNamesTxt.c_str())) {
                colormaps::Map map = colormaps::maps[colorMapNames[colorMapId]];
                gui::waterfall.updatePalletteFromArray(map.map, map.entryCount);
                core::configManager.acquire();
                core::configManager.conf["colorMap"] = colorMapNames[colorMapId];
                core::configManager.release(true);
                colorMapAuthor = map.author;
            }
            ImGui::Text("Color map Author: %s", colorMapAuthor.c_str());
        }

    }
}
