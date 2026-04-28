#pragma once

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
inline void doOverlayText(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 sz  = ImGui::CalcTextSize(buf);
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos,
        ImVec2(pos.x + sz.x, pos.y + sz.y),
        IM_COL32(0, 0, 0, 128));
    ImGui::TextUnformatted(buf);
}
