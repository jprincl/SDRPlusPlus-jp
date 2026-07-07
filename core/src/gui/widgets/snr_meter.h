#pragma once
#include <imgui/imgui.h>

namespace ImGui {
    float GetLevelMeterMinWidth();

    // Signal meter: level bar (dBFS) with peak hold marker, plus a numeric
    // peak-level / SNR readout on the right. Pass non-finite level/levelMax
    // (e.g. -INFINITY) to draw an empty meter, and NAN snr to blank the SNR
    // readout (no VFO selected).
    void LevelMeter(float level, float levelMax, float snr, const ImVec2& size_arg = ImVec2(0, 0));
}
