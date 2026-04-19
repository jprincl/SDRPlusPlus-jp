#include <gui/widgets/volume_meter.h>
#include <algorithm>
#include <gui/style.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace {
    // Key the cache on the font pointer AND the scale epoch: a font atlas
    // rebuild can reuse the same pointer address, and style::dp() feeds the
    // cached width even when the font pointer is unchanged.
    static ImFont*  cachedFont = nullptr;
    static uint64_t cachedEpoch = UINT64_MAX;
    static float    cachedWidth = 0.0f;
};

namespace ImGui {
    float GetSNRMeterMinWidth() {
        ImFont* currentFont = ImGui::GetFont();
        uint64_t currentEpoch = style::scaleEpoch();
        if (currentFont != cachedFont || currentEpoch != cachedEpoch) {
            cachedFont = currentFont;
            cachedEpoch = currentEpoch;
            float maxLabelWidth = 0.0f;
            char buf[32];
            // 10 tick labels (0, 10, 20, … 90) over 9 intervals; minimum width
            // so that each interval is at least as wide as the widest label.
            for (int i = 0; i < 10; i++) {
                sprintf(buf, "%d", i * 10);
                maxLabelWidth = std::max(maxLabelWidth, ImGui::CalcTextSize(buf).x);
            }
            cachedWidth = maxLabelWidth * 9.0f + style::dp(8.0f);
        }
        return cachedWidth;
    }

    void SNRMeter(float val, const ImVec2& size_arg = ImVec2(0, 0)) {
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

        val = std::clamp<float>(val, 0, 100);
        float ratio = size.x / 90;
        float it = size.x / 9;
        char buf[32];

        float barHeight = style::dp(10.0f);
        window->DrawList->AddRectFilled(min + ImVec2(0, 1), min + ImVec2(roundf((float)val * ratio), barHeight), IM_COL32(0, 136, 255, 255));
        window->DrawList->AddLine(min, min + ImVec2(0, barHeight - 1), text, style::uiScale);
        window->DrawList->AddLine(min + ImVec2(0, barHeight - 1), min + ImVec2(size.x + 1, barHeight - 1), text, style::uiScale);

        for (int i = 0; i < 10; i++) {
            window->DrawList->AddLine(min + ImVec2(roundf((float)i * it), barHeight - 1), min + ImVec2(roundf((float)i * it), style::dp(15.0f) - 1), text, style::uiScale);
            sprintf(buf, "%d", i * 10);
            ImVec2 sz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(min + ImVec2(roundf(((float)i * it) - (sz.x / 2.0)) + 1, style::dp(16.0f)), text, buf);
        }
    }
}
