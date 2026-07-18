#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <imgui.h>
#include <gui/style.h>

// Compact single-choice bank of toggle buttons (PowerSDR/Thetis-style mode
// grid). Fills the available width, wrapping into as many balanced rows as
// needed for the widest label to fit; the selected button gets a derived
// high-contrast fill/text pair plus an inner stroke, so the active state does
// not depend on theme ButtonActive contrast. Buttons within the bank use
// tighter-than-normal spacing so the group reads as one control; the gap is
// kept at >= 2x TouchExtraPadding so adjacent hit boxes never overlap in the
// touch style.
// Returns true when selected changed.
inline bool doToggleButtonGrid(const std::string& id, int& selected, const std::vector<std::string>& labels) {
    const int count = (int)labels.size();
    const ImGuiStyle& s = ImGui::GetStyle();
    auto mixColor = [](const ImVec4& a, const ImVec4& b, float t) {
        return ImVec4(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        );
    };

    const ImVec4 windowBg = s.Colors[ImGuiCol_WindowBg];
    const ImVec4 textCol = s.Colors[ImGuiCol_Text];
    const bool disabled = s.Colors[ImGuiCol_Button].w <= 0.25f && textCol.w <= 0.75f;
    const float selectedAlpha = disabled ? std::max(0.25f, s.Colors[ImGuiCol_Button].w) : 0.95f;

    ImVec4 selectedBg = mixColor(windowBg, textCol, 0.60f);
    selectedBg.w = selectedAlpha;
    ImVec4 selectedHovered = mixColor(selectedBg, textCol, 0.10f);
    selectedHovered.w = selectedAlpha;
    ImVec4 selectedActive = mixColor(selectedBg, textCol, 0.18f);
    selectedActive.w = selectedAlpha;
    ImVec4 selectedText = windowBg;
    selectedText.w = disabled ? textCol.w : 1.0f;

    const float gapX = std::max(style::dp(3.0f), 2.0f * s.TouchExtraPadding.x);
    const float gapY = std::max(style::dp(3.0f), 2.0f * s.TouchExtraPadding.y);
    const float avail = ImGui::GetContentRegionAvail().x;

    float maxLabel = 0.0f;
    for (const std::string& label : labels) {
        maxLabel = std::max(maxLabel, ImGui::CalcTextSize(label.c_str()).x);
    }
    const float minButton = maxLabel + 2.0f * s.FramePadding.x;

    const int perRow = std::clamp((int)((avail + gapX) / (minButton + gapX)), 1, count);
    const int rows = (count + perRow - 1) / perRow;
    const int base = count / rows;
    const int extra = count % rows;

    bool changed = false;
    ImGui::PushID(id.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gapX, gapY));
    ImGui::BeginGroup();
    int idx = 0;
    for (int r = 0; r < rows; r++) {
        const int n = base + ((r < extra) ? 1 : 0);
        // Distribute the row width by rounding cumulative slot edges so the
        // row spans exactly avail regardless of fractional button widths.
        const float slot = (avail + gapX) / (float)n;
        for (int c = 0; c < n; c++, idx++) {
            if (c) { ImGui::SameLine(); }
            const float w = std::round(slot * (float)(c + 1)) - std::round(slot * (float)c) - gapX;
            const bool sel = (idx == selected);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button, selectedBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selectedHovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, selectedActive);
                ImGui::PushStyleColor(ImGuiCol_Text, selectedText);
            }
            if (ImGui::Button(labels[idx].c_str(), ImVec2(w, 0)) && !sel) {
                selected = idx;
                changed = true;
            }
            if (sel) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                const float inset = style::dp(1.0f);
                const float rounding = std::max(0.0f, s.FrameRounding - inset);
                ImVec4 stroke = selectedText;
                stroke.w *= disabled ? 0.35f : 0.55f;
                ImGui::GetWindowDrawList()->AddRect(
                    ImVec2(min.x + inset, min.y + inset),
                    ImVec2(max.x - inset, max.y - inset),
                    ImGui::GetColorU32(stroke),
                    rounding,
                    0,
                    std::max(1.0f, style::dp(1.0f))
                );
                ImGui::PopStyleColor(4);
            }
        }
    }
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}
