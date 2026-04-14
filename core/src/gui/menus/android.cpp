#ifdef __ANDROID__
#include <gui/menus/android.h>
#include <imgui.h>
#include <core.h>
#include <gui/gui.h>
#include <android_backend.h>
#include <algorithm>

namespace androidmenu {

    // Must match SleepTimerManager.Mode ordinals: DISABLED=0, KEEP_ALIVE=1, DIM_SCREEN=2, DIM_AND_BLANK=3
    static const char* SLEEP_MODE_ITEMS = "Disabled\0Keep Alive\0Dim Screen\0Dim and Blank\0";
    static const int   SLEEP_MODE_COUNT = 4;

    int sleepMode     = 3;   // default: Dim and Blank
    int screenDimMin  = 3;   // total minutes from start until screen dims
    int screenDarkMin = 8;   // total minutes from start until screen goes dark (> screenDimMin)

    static void applyConfig() {
        backend::setSleepTimerConfig(sleepMode, screenDimMin * 60, screenDarkMin * 60);
    }

    void init() {
        sleepMode     = core::configManager.conf.value("sleepMode",    3);
        screenDimMin  = core::configManager.conf.value("sleepDimMin",  3);
        screenDarkMin = core::configManager.conf.value("sleepDarkMin", 8);

        // Clamp / enforce invariants
        sleepMode     = std::clamp(sleepMode, 0, SLEEP_MODE_COUNT - 1);
        screenDimMin  = std::max(1, screenDimMin);
        screenDarkMin = std::max(screenDimMin + 1, screenDarkMin);

        applyConfig();
    }

    void draw(void* ctx) {
        float menuWidth = ImGui::GetContentRegionAvail().x;

        // ── Keep-alive mode ───────────────────────────────────────────────────────
        ImGui::LeftLabel("Keep Alive");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo("##android_sleep_mode", &sleepMode, SLEEP_MODE_ITEMS)) {
            sleepMode = std::clamp(sleepMode, 0, SLEEP_MODE_COUNT - 1);
            applyConfig();
            core::configManager.acquire();
            core::configManager.conf["sleepMode"] = sleepMode;
            core::configManager.release(true);
        }

        // ── Dim threshold (DIM_SCREEN=2 and DIM_AND_BLANK=3) ─────────────────────
        if (sleepMode >= 2) {
            bool changed = false;

            ImGui::LeftLabel("Dim screen after (min)");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputInt("##android_dim_min", &screenDimMin, 1, 5)) {
                screenDimMin  = std::max(1, screenDimMin);
                screenDarkMin = std::max(screenDimMin + 1, screenDarkMin);
                changed = true;
            }

            // ── Blank threshold (DIM_AND_BLANK=3 only) ───────────────────────────
            if (sleepMode >= 3) {
                ImGui::LeftLabel("Screen off after (min)");
                ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
                if (ImGui::InputInt("##android_dark_min", &screenDarkMin, 1, 5)) {
                    screenDarkMin = std::max(screenDimMin + 1, screenDarkMin);
                    changed = true;
                }
            }

            if (changed) {
                applyConfig();
                core::configManager.acquire();
                core::configManager.conf["sleepDimMin"]  = screenDimMin;
                core::configManager.conf["sleepDarkMin"] = screenDarkMin;
                core::configManager.release(true);
            }
        }
    }

}

#endif
