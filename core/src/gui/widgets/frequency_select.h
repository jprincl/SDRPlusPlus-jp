#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <stdint.h>
#include <string>

class FrequencySelect {
public:
    FrequencySelect();
    void init();
    void draw();
    float getWidth();
    void setFrequency(int64_t freq);

    uint64_t frequency;
    bool frequencyChanged = false;
    bool digitHovered = false;

    bool limitFreq;
    uint64_t minFreq;
    uint64_t maxFreq;

private:
    void onPosChange();
    void onResize();
    void incrementDigit(int i);
    void decrementDigit(int i);
    void moveCursorToDigit(int i);
    void drawKeypad();
    void keypadKey(char key);
    void commitEntry();

    ImVec2 widgetPos;
    ImVec2 lastWidgetPos;

    int digits[12];
    ImVec2 digitBottomMins[12];
    ImVec2 digitTopMins[12];
    ImVec2 digitBottomMaxs[12];
    ImVec2 digitTopMaxs[12];

    // First digit visible and manipulated (based on maxFreq)
    int firstDigit = 0;
    // Cached maxFreq and limitFreq to detect a change of layout.
    uint64_t lastMaxFreq = 0;
    bool lastLimitFreq = false;
    uint64_t lastScaleEpoch = 0;

    char buf[100];
    float cachedWidth_ = 0.0f;
    float wheelAccum = 0.0f;

    // Press tracking on digits: a quick release steps the digit, a motionless
    // hold opens the F-INP direct-entry keypad.
    int pressDigit = -1;  // digit index the press started on, -1 = none
    int pressDir = 0;     // +1 = top half (increment), -1 = bottom half (decrement)
    bool longPressDone = false;

    // F-INP keypad state. `entry` holds the typed value in MHz: digits plus at
    // most one decimal point, IC-705 style.
    bool keypadRequestOpen = false;
    std::string entry;
};
