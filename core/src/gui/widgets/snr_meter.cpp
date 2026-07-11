#include <gui/widgets/snr_meter.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <gui/style.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace {
    // The scale spans -90..0 dBFS: 10 tick labels over 9 intervals.
    constexpr float METER_RANGE = 90.0f;

    // Key the cache on the font pointer AND the scale epoch: a font atlas
    // rebuild can reuse the same pointer address, and style::dp() feeds the
    // cached widths even when the font pointer is unchanged.
    static ImFont*  cachedFont = nullptr;
    static uint64_t cachedEpoch = UINT64_MAX;
    static float    cachedLabelWidth = 0.0f;
    static float    cachedMinWidth = 0.0f;
    static float    cachedTextColWidth = 0.0f;

    void updateWidthCache() {
        ImFont* currentFont = ImGui::GetFont();
        uint64_t currentEpoch = style::scaleEpoch();
        if (currentFont == cachedFont && currentEpoch == cachedEpoch) { return; }
        cachedFont = currentFont;
        cachedEpoch = currentEpoch;
        float maxLabelWidth = 0.0f;
        char buf[32];
        for (int i = 0; i < 10; i++) {
            sprintf(buf, "%d", (i - 9) * 10);
            maxLabelWidth = std::max(maxLabelWidth, ImGui::CalcTextSize(buf).x);
        }
        cachedLabelWidth = maxLabelWidth;
        // Numeric readout column on the right; widest realistic value.
        cachedTextColWidth = ImGui::CalcTextSize("-150.0 dB").x + style::dp(4.0f);
        // Hard minimum width: the bar with sparse (every-other) tick labels, so
        // each labeled interval spans two ticks and stays at least as wide as
        // the widest label. The numeric readout is optional and not required.
        cachedMinWidth = maxLabelWidth * 4.5f + style::dp(8.0f);
    }
};

namespace ImGui {
    float GetLevelMeterMinWidth() {
        updateWidthCache();
        return cachedMinWidth;
    }

    void LevelMeter(float level, float levelMax, float snr, const ImVec2& size_arg) {
        ImGuiWindow* window = GetCurrentWindow();
        ImGuiStyle& style = GImGui->Style;

        ImVec2 min = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), 26);
        ImRect bb(min, min + size);

        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

        ItemSize(size, style.FramePadding.y);
        if (!ItemAdd(bb, 0)) {
            return;
        }

        updateWidthCache();
        // The numeric readout is optional: only reserve its column when the bar
        // clears its sparse-label minimum plus some slack (so the readout hides
        // a bit sooner, before the bar gets cramped) with the column removed.
        float minBarWidth = cachedLabelWidth * 5.5f;
        bool showReadout = (size.x - cachedTextColWidth) >= minBarWidth;
        float barWidth = std::max(showReadout ? size.x - cachedTextColWidth : size.x, 1.0f);
        // Mandatory gap between adjacent labels.
        float labelGap = style::dp(3.0f);
        // Label every tick when there is room for all ten with that gap;
        // otherwise label every other tick (all tick marks are still drawn).
        bool fullLabels = barWidth >= (cachedLabelWidth + labelGap) * 9.0f;
        float ratio = barWidth / METER_RANGE;
        float it = barWidth / 9;
        char buf[32];

        float barHeight = style::dp(10.0f);

        // Level bar and peak hold marker
        if (std::isfinite(level)) {
            float gval = std::clamp<float>(level, -METER_RANGE, 0) + METER_RANGE;
            window->DrawList->AddRectFilled(min + ImVec2(0, 1), min + ImVec2(roundf(gval * ratio), barHeight), IM_COL32(0, 192, 0, 255));
        }
        if (std::isfinite(levelMax)) {
            float fval = std::clamp<float>(levelMax, -METER_RANGE, 0) + METER_RANGE;
            float px = roundf(fval * ratio);
            window->DrawList->AddRectFilled(min + ImVec2(px, 1), min + ImVec2(px + std::max(2.0f, style::dp(2.0f)), barHeight), IM_COL32(255, 255, 0, 255));
        }

        window->DrawList->AddLine(min, min + ImVec2(0, barHeight - 1), text, style::uiScale);
        window->DrawList->AddLine(min + ImVec2(0, barHeight - 1), min + ImVec2(barWidth + 1, barHeight - 1), text, style::uiScale);

        // The leftmost label is left-aligned to its tick (it cannot center
        // without spilling off the left edge). Drop it if that pushes it into
        // the next label.
        int firstLabelIdx = fullLabels ? 0 : 1;
        int secondLabelIdx = fullLabels ? 1 : 3;
        bool skipFirstLabel = false;
        {
            char b0[32], b1[32];
            sprintf(b0, "%d", (firstLabelIdx - 9) * 10);
            sprintf(b1, "%d", (secondLabelIdx - 9) * 10);
            float w0 = ImGui::CalcTextSize(b0).x;
            float w1 = ImGui::CalcTextSize(b1).x;
            float x0 = std::max(roundf((float)firstLabelIdx * it - w0 / 2.0f) + 1, 0.0f);
            float x1 = roundf((float)secondLabelIdx * it - w1 / 2.0f) + 1;
            skipFirstLabel = (x0 + w0 + labelGap) > x1;
        }

        for (int i = 0; i < 10; i++) {
            // In sparse mode label only odd ticks (0, -20, -40, -60, -80 dB);
            // the unlabeled 10 dB ticks in between are drawn shorter.
            bool labeled = fullLabels || (i % 2) == 1;
            float tickBottom = labeled ? style::dp(15.0f) : style::dp(12.5f);
            window->DrawList->AddLine(min + ImVec2(roundf((float)i * it), barHeight - 1), min + ImVec2(roundf((float)i * it), tickBottom - 1), text, style::uiScale);
            if (!labeled) { continue; }
            if (i == firstLabelIdx && skipFirstLabel) { continue; }
            sprintf(buf, "%d", (i - 9) * 10);
            ImVec2 sz = ImGui::CalcTextSize(buf);
            // Center the label on the tick, but keep the first one inside the widget
            float labelX = std::max(roundf(((float)i * it) - (sz.x / 2.0f)) + 1, 0.0f);
            window->DrawList->AddText(min + ImVec2(labelX, style::dp(16.0f)), text, buf);
        }

        // Numeric readout: peak level on top, SNR below. Hidden when the widget
        // is too narrow to reserve the column without starving the bar.
        if (showReadout) {
            if (std::isfinite(levelMax)) {
                sprintf(buf, "%+.1f dB", levelMax);
            }
            else {
                strcpy(buf, "--.- dB");
            }
            ImVec2 sz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(min + ImVec2(size.x - sz.x, 0), text, buf);

            if (std::isfinite(snr)) {
                sprintf(buf, "%.1f dB", snr);
            }
            else {
                strcpy(buf, "--.- dB");
            }
            sz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(min + ImVec2(size.x - sz.x, sz.y), text, buf);
        }
    }
}
