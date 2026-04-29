#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <gui/style.h>

inline float getFingerButtonHeight() {
    return style::baseFont->FontSize * 3.0f;
}

inline bool doFingerButton(const std::string &title) {
    const ImVec2& labelWidth = ImGui::CalcTextSize(title.c_str(), nullptr, true, -1);
    if (title[0] == '>') {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
    }
    auto rv = ImGui::Button(title.c_str(), ImVec2(labelWidth.x + style::baseFont->FontSize, style::baseFont->FontSize * 3));
    if (title[0] == '>') {
        ImGui::PopStyleColor();
    }
    return rv;
};

inline void doRightText(const std::string &title) {
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize((title+"W").c_str()).x, 0));
    ImGui::SameLine();
    ImGui::TextUnformatted(title.c_str());
}

// Like ImGui::Text(), but draws a translucent black rectangle behind the
// text so labels stay readable when overlaid on a busy background (e.g.
// drawn over a map). Cursor advances as for normal Text.
//
// The backdrop extends down by ItemSpacing.y so successive overlay lines
// produce abutting rectangles instead of leaving a transparent stripe of
// background between them. The right edge is extended by the same amount
// for visual symmetry — text doesn't hug the rectangle edge.
inline void doOverlayText(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 sz  = ImGui::CalcTextSize(buf);
    const float pad = ImGui::GetStyle().ItemSpacing.y;
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos,
        ImVec2(pos.x + sz.x + pad, pos.y + sz.y + pad),
        IM_COL32(0, 0, 0, 128));
    ImGui::TextUnformatted(buf);
}

// Bump the button color alpha so labels stay legible when buttons sit on
// top of a busy background (e.g. the world map). Pair with
// popOverlayButtonStyle. Doubles alpha clamped to 1.0 — buttons whose
// theme alpha is already 1 are unchanged.
inline void pushOverlayButtonStyle() {
    const auto bump = [](ImGuiCol id) {
        ImVec4 c = ImGui::GetStyle().Colors[id];
        c.w = std::min(1.0f, c.w * 2.0f);
        return c;
    };
    ImGui::PushStyleColor(ImGuiCol_Button, bump(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bump(ImGuiCol_ButtonHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bump(ImGuiCol_ButtonActive));
}
inline void popOverlayButtonStyle() {
    ImGui::PopStyleColor(3);
}
