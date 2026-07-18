#pragma once
#include <imgui.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include <gui/gui.h>
#include <gui/widgets/popup_dialog.h>

#define GENERIC_DIALOG_BUTTONS_OK           "Ok\0"
#define GENERIC_DIALOG_BUTTONS_YES_NO       "Yes\0No\0"
#define GENERIC_DIALOG_BUTTONS_APPLY_CANCEL "Apply\0Cancel\0"
#define GENERIC_DIALOG_BUTTONS_OK_CANCEL    "Ok\0Cancel\0"

#define GENERIC_DIALOG_BUTTON_OK    0
#define GENERIC_DIALOG_BUTTON_YES   0
#define GENERIC_DIALOG_BUTTON_NO    1
#define GENERIC_DIALOG_BUTTON_APPLY 0
#define GENERIC_DIALOG_BUTTON_CANCE 1

namespace ImGui {
    // Enter activates the first button, Escape the last one (for a single
    // button, either key activates it). Closing the popup at the ImGui level
    // (Android back gesture, click outside) clears `open` and returns -1.
    template <typename Func>
    int GenericDialog(const char* id, bool& open, const char* buttons, Func draw) {
        // If not open, return
        if (!open) { return -1; }

        // One dialog state per popup id; per-call-site via template instantiation.
        static std::unordered_map<std::string, PopupDialog> dialogs;
        PopupDialog& dlg = dialogs[id];
        if (!dlg.isOpen()) { dlg.request(); }

        // Draw popup
        gui::mainWindow.lockWaterfallControls = true;
        std::string idstr = std::string("##") + std::string(id);
        int result = -1;
        if (dlg.begin(id, ImGuiWindowFlags_NoResize)) {
            // Draw widgets
            draw();

            // Count buttons so Escape can map to the last one
            int count = 0;
            for (const char* b = buttons; b[0]; b += strlen(b) + 1) { count++; }

            bool applyKey = dlg.applyRequested();
            bool cancelKey = dlg.cancelRequested();

            // Draw buttons
            int bid = 0;
            for (const char* b = buttons; b[0]; b += strlen(b) + 1, bid++) {
                if (bid) { ImGui::SameLine(); }
                bool pressed = ImGui::Button((b + idstr).c_str());
                if (bid == 0 && applyKey) { pressed = true; }
                if (bid == count - 1 && cancelKey) { pressed = true; }
                if (pressed && result < 0) { result = bid; }
            }

            if (result >= 0) { dlg.close(); }
            dlg.end();
        }
        if (!dlg.isOpen()) { open = false; }

        return result;
    }
}
