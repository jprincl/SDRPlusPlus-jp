#pragma once

#include <atomic>
#include <string>
#include <imgui.h>

namespace backend::common {
    struct ScaleState {
        float detectedContentScale = 1.0f;
        float userScaleFactor = 1.0f;
        std::atomic<float> pendingContentScale{0.0f};
        float pendingUserScaleFactor = 0.0f;
        std::string resDir;
    };

    float configUserScaleFactor();
    void initScaleState(ScaleState& state, const std::string& resDir, float detectedContentScale, float userScaleFactor);
    float setScaleFromConfig(ScaleState& state, float detectedContentScale);
    void queueContentScale(ScaleState& state, float detectedContentScale);
    void setUserScaleFactor(ScaleState& state, float userScaleFactor);
    bool applyScale(ScaleState& state, float detectedContentScale, float userScaleFactor, bool rebuildFonts, bool rescaleMainWindow);
    bool applyPendingScaleChanges(ScaleState& state, float currentDetectedContentScale, bool checkCurrentDetected);

    void drawMainWindow(const ImVec2& size);
    void renderOpenGLFrame(int width, int height);
}
