#pragma once
#include <imgui.h>

// Lifecycle and keyboard handling for Apply/Cancel style popup dialogs.
//
// Owns the OpenPopup arming so the popup is opened exactly once per request:
// closing it at the ImGui level (Android back gesture, click outside the
// popup) is detected in begin() and ends the dialog instead of being undone
// by a re-open on the next frame.
//
// Usage:
//     dialog.request();                     // from the triggering button
//     ...
//     if (dialog.begin("My dialog##id")) {
//         ...widgets...
//         if (dialog.applyButton("Apply", applyDisabled)) { ...; dialog.close(); }
//         ImGui::SameLine();
//         if (dialog.cancelButton()) { dialog.close(); }
//         dialog.end();
//     }
//     if (!dialog.isOpen()) { ...dialog finished... }
class PopupDialog {
public:
    void request();
    bool isOpen() const { return open; }

    bool begin(const char* id, ImGuiWindowFlags flags = 0);
    void end();

    // Draws the button; returns true on click or Enter/keypad Enter while the
    // popup is focused. suppressEnter keeps Enter for the active field (e.g.
    // a multiline text edit).
    bool applyButton(const char* label, bool disabled = false, bool suppressEnter = false);

    // Draws the button; returns true on click or Escape while the popup is
    // focused. An Escape that ends an in-progress field edit does not cancel
    // the dialog; only a later press does.
    bool cancelButton(const char* label = "Cancel");

    // Key-only variants of the above, for dialogs whose buttons don't map
    // one-to-one onto apply/cancel. Valid between begin() and end().
    bool applyRequested(bool suppressEnter = false) const;
    bool cancelRequested() const;

    void close();

    static bool confirmKeyPressed();
    static bool cancelKeyPressed();

private:
    bool open = false;
    bool armRequested = false;
    bool focused = false;
    bool fieldWasActive = false;
};
