#include <gui/widgets/volume_meter.h>
#include <algorithm>
#include <gui/style.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace {
    // Recompute only when the font pointer changes (e.g. after a style reload).
    static ImFont* cachedFont = nullptr;
    static float   cachedWidth = 0.0f;
};

namespace ImGui {    
    float GetSNRMeterMinWidth() {
        ImFont* currentFont = ImGui::GetFont();
        if (currentFont != cachedFont) {
            cachedFont = currentFont;
            float maxLabelWidth = 0.0f;
            char buf[32];
            // 10 tick labels (0, 10, 20, … 90) over 9 intervals; minimum width
            // so that each interval is at least as wide as the widest label.
            for (int i = 0; i < 10; i++) {
                sprintf(buf, "%d", i * 10);
                maxLabelWidth = std::max(maxLabelWidth, ImGui::CalcTextSize(buf).x);
            }
            cachedWidth = maxLabelWidth * 9.0f + 8.0f * style::uiScale;
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

        window->DrawList->AddRectFilled(min + ImVec2(0, 1), min + ImVec2(roundf((float)val * ratio), 10 * style::uiScale), IM_COL32(0, 136, 255, 255));
        window->DrawList->AddLine(min, min + ImVec2(0, (10.0f * style::uiScale) - 1), text, style::uiScale);
        window->DrawList->AddLine(min + ImVec2(0, (10.0f * style::uiScale) - 1), min + ImVec2(size.x + 1, (10.0f * style::uiScale) - 1), text, style::uiScale);

        for (int i = 0; i < 10; i++) {
            window->DrawList->AddLine(min + ImVec2(roundf((float)i * it), (10.0f * style::uiScale) - 1), min + ImVec2(roundf((float)i * it), (15.0f * style::uiScale) - 1), text, style::uiScale);
            sprintf(buf, "%d", i * 10);
            ImVec2 sz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(min + ImVec2(roundf(((float)i * it) - (sz.x / 2.0)) + 1, 16.0f * style::uiScale), text, buf);
        }
    }
}
