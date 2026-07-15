#include <gui/widgets/frequency_select.h>
#include <config.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <backend.h>
#include <utils/hrfreq.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

bool isInArea(ImVec2 val, ImVec2 min, ImVec2 max) {
    return val.x >= min.x && val.x < max.x && val.y >= min.y && val.y < max.y;
}

FrequencySelect::FrequencySelect() {
}

void FrequencySelect::init() {
    for (int i = 0; i < 12; i++) {
        digits[i] = 0;
    }
}

void FrequencySelect::onPosChange() {
    ImVec2 digitSz = ImGui::CalcTextSize("0");
    ImVec2 commaSz = ImGui::CalcTextSize(".");
    int digitHeight = digitSz.y;
    int digitWidth = digitSz.x;
    int commaOffset = 0;
    for (int i = firstDigit; i < 12; i++) {
        int pos = i - firstDigit;
        digitTopMins[i] = ImVec2(widgetPos.x + (pos * digitWidth) + commaOffset, widgetPos.y);
        digitBottomMins[i] = ImVec2(widgetPos.x + (pos * digitWidth) + commaOffset, widgetPos.y + (digitHeight / 2));

        digitTopMaxs[i] = ImVec2(widgetPos.x + (pos * digitWidth) + commaOffset + digitWidth, widgetPos.y + (digitHeight / 2));
        digitBottomMaxs[i] = ImVec2(widgetPos.x + (pos * digitWidth) + commaOffset + digitWidth, widgetPos.y + digitHeight);

        if ((i + 1) % 3 == 0 && i < 11) {
            commaOffset += commaSz.x;
        }
    }
    // commaOffset now holds the total accumulated comma width — reuse it for the
    // width cache rather than recomputing with a separate PushFont/CalcTextSize pair.
    cachedWidth_ = (12 - firstDigit) * digitWidth + commaOffset + 17.0f * style::uiScale;
}

void FrequencySelect::incrementDigit(int i) {
    if (i < 0) {
        return;
    }
    if (digits[i] < 9) {
        digits[i]++;
    }
    else {
        digits[i] = 0;
        incrementDigit(i - 1);
    }
    frequencyChanged = true;
}

void FrequencySelect::decrementDigit(int i) {
    if (i < 0) {
        return;
    }
    if (digits[i] > 0) {
        digits[i]--;
    }
    else {
        if (i == 0) { return; }

        // Check if there are non zero digits afterwards
        bool otherNoneZero = false;
        for (int j = i - 1; j >= 0; j--) {
            if (digits[j] > 0) {
                otherNoneZero = true;
                break;
            }
        }
        if (!otherNoneZero) { return; }

        digits[i] = 9;
        decrementDigit(i - 1);
    }
    frequencyChanged = true;
}

void FrequencySelect::moveCursorToDigit(int i) {
    double xpos, ypos;
    backend::getMouseScreenPos(xpos, ypos);
    double nxpos = (digitTopMaxs[i].x + digitTopMins[i].x) / 2.0;
    backend::setMouseScreenPos(nxpos, ypos);
}

float FrequencySelect::getWidth() {
    // getWidth() is called during top-bar layout before draw() runs, so it
    // must refresh the cache itself when the scale changes — otherwise the
    // first scaled frame reserves space using old-scale digit widths.
    uint64_t currentScaleEpoch = style::scaleEpoch();
    if (currentScaleEpoch != lastScaleEpoch || cachedWidth_ == 0.0f) {
        ImGui::PushFont(style::bigFont);
        float digitWidth = ImGui::CalcTextSize("0").x;
        float commaWidth = ImGui::CalcTextSize(".").x;
        ImGui::PopFont();
        int commaCount = 0;
        for (int i = firstDigit; i < 11; i++) {
            if ((i + 1) % 3 == 0) { commaCount++; }
        }
        cachedWidth_ = (12 - firstDigit) * digitWidth + commaCount * commaWidth + 17.0f * style::uiScale;
        // Don't update lastScaleEpoch here — draw() still needs to see the
        // change to recompute the digit position arrays.
    }
    return cachedWidth_;
}

void FrequencySelect::draw() {
    auto window = ImGui::GetCurrentWindow();
    auto io = ImGui::GetIO();
    widgetPos = ImGui::GetWindowContentRegionMin();
    ImVec2 cursorPos = ImGui::GetCursorPos();
    widgetPos.x += window->Pos.x + cursorPos.x;
    ImGui::PushFont(style::bigFont);
    ImVec2 digitSz = ImGui::CalcTextSize("0");
    ImVec2 commaSz = ImGui::CalcTextSize(".");
    widgetPos.y = window->Pos.y + cursorPos.y - ((digitSz.y / 2.0f) - ceilf(15 * style::uiScale) - 5);

    // Recompute the first visible digit only when maxFreq or limitFreq changes
    bool firstDigitChanged = false;
    if (maxFreq != lastMaxFreq || limitFreq != lastLimitFreq) {
        lastMaxFreq = maxFreq;
        lastLimitFreq = limitFreq;
        int newFirstDigit = 0;
        if (limitFreq && maxFreq > 0) {
            uint64_t mf = maxFreq;
            int numDigits = 0;
            while (mf > 0) { mf /= 10; numDigits++; }
            newFirstDigit = 12 - numDigits;
            if (newFirstDigit < 0) { newFirstDigit = 0; }
        }
        firstDigitChanged = (newFirstDigit != firstDigit);
        firstDigit = newFirstDigit;
        // Zero out hidden leading digits
        for (int i = 0; i < firstDigit; i++)
            digits[i] = 0;
    }

    uint64_t currentScaleEpoch = style::scaleEpoch();
    if (widgetPos.x != lastWidgetPos.x || widgetPos.y != lastWidgetPos.y || firstDigitChanged || currentScaleEpoch != lastScaleEpoch) {
        lastWidgetPos = widgetPos;
        lastScaleEpoch = currentScaleEpoch;
        onPosChange();
    }

    ImU32 disabledColor = ImGui::GetColorU32(ImGuiCol_Text, 0.3f);
    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    
    int digitWidth = digitSz.x;
    int commaOffset = 0;
    float textOffset = 11.0f * style::uiScale;
    bool zeros = true;

    ImGui::ItemSize(ImRect(digitTopMins[firstDigit], ImVec2(digitBottomMaxs[11].x + 15, digitBottomMaxs[11].y)));

    for (int i = firstDigit; i < 12; i++) {
        if (digits[i] != 0) {
            zeros = false;
        }
        int pos = i - firstDigit;
        sprintf(buf, "%d", digits[i]);
        window->DrawList->AddText(ImVec2(widgetPos.x + (pos * digitWidth) + commaOffset, widgetPos.y),
                                  zeros ? disabledColor : textColor, buf);
        if ((i + 1) % 3 == 0 && i < 11) {
            commaOffset += commaSz.x;
            window->DrawList->AddText(ImVec2(widgetPos.x + (pos * digitWidth) + commaOffset + textOffset, widgetPos.y),
                                      zeros ? disabledColor : textColor, ".");
        }
    }

    bool hovered = false;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_None) &&
        !gui::mainWindow.lockWaterfallControls)
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        bool leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool rightClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        // Precision touchpads report fractional wheel deltas; accumulate and
        // step digits on whole notches instead of truncating them away.
        wheelAccum += io.MouseWheel;
        int mw = (int)wheelAccum;
        wheelAccum -= (float)mw;
        bool onDigit = false;

        for (int i = firstDigit; i < 12; i++) {
            onDigit = false;
            if (isInArea(mousePos, digitTopMins[i], digitTopMaxs[i])) {
                window->DrawList->AddRectFilled(digitTopMins[i], digitTopMaxs[i], IM_COL32(255, 0, 0, 75));
                if (leftClick) {
                    incrementDigit(i);
                }
                onDigit = true;
            }
            if (isInArea(mousePos, digitBottomMins[i], digitBottomMaxs[i])) {
                window->DrawList->AddRectFilled(digitBottomMins[i], digitBottomMaxs[i], IM_COL32(0, 0, 255, 75));
                if (leftClick) {
                    decrementDigit(i);
                }
                onDigit = true;
            }
            if (onDigit) {
                hovered = true;
                if (rightClick || (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
                    for (int j = i; j < 12; j++) {
                        digits[j] = 0;
                    }

                    frequencyChanged = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                    incrementDigit(i);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                    decrementDigit(i);
                }
                if ((ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && i > firstDigit) {
                    moveCursorToDigit(i - 1);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && i < 11) {
                    moveCursorToDigit(i + 1);
                }

                auto chars = io.InputQueueCharacters;

                // For each keyboard characters, type it
                for (int j = 0; j < chars.Size; j++) {
                    if (chars[j] >= '0' && chars[j] <= '9') {
                        digits[i + j] = chars[j] - '0';
                        if ((i + j) < 11) { moveCursorToDigit(i + j + 1); }
                        frequencyChanged = true;
                    }
                }

                if (mw != 0) {
                    int count = abs(mw);
                    for (int j = 0; j < count; j++) {
                        mw > 0 ? incrementDigit(i) : decrementDigit(i);
                    }
                }
            }
        }

        if (isInArea(mousePos, digitTopMins[firstDigit], digitBottomMaxs[11])) {
            bool shortcutKey = io.ConfigMacOSXBehaviors ? (io.KeyMods == ImGuiKeyModFlags_Super) : (io.KeyMods == ImGuiKeyModFlags_Ctrl);
            bool ctrlOnly = (io.KeyMods == ImGuiKeyModFlags_Ctrl);
            bool shiftOnly = (io.KeyMods == ImGuiKeyModFlags_Shift);
            bool copy  = ((shortcutKey && ImGui::IsKeyPressed(ImGuiKey_C)) || (ctrlOnly  && ImGui::IsKeyPressed(ImGuiKey_Insert)));
            bool paste = ((shortcutKey && ImGui::IsKeyPressed(ImGuiKey_V)) || (shiftOnly && ImGui::IsKeyPressed(ImGuiKey_Insert)));
            if (copy) {
                // Convert the freqency to a string
                std::string freqStr = hrfreq::toString(frequency);

                // Write it to the clipboard
                ImGui::SetClipboardText(freqStr.c_str());
            }
            if (paste) {
                // Attempt to parse the clipboard as a number
                const char* clip = ImGui::GetClipboardText();

                // If the clipboard is not empty, attempt to parse it
                if (clip) {
                    double newFreq;
                    if (hrfreq::fromString(clip, newFreq)) {
                        setFrequency(abs(newFreq));
                        frequencyChanged = true;
                    }
                }
            }
        }
    }
    // Assigned outside the hover-gated block: leaving the window while over a
    // digit used to leave digitHovered stuck true, blocking the arrow-key
    // tuning in main_window. Partial wheel notches are also dropped once the
    // cursor is off the digits so they can't discharge into a step later.
    digitHovered = hovered;
    if (!hovered) { wheelAccum = 0.0f; }

    uint64_t freq = 0;
    for (int i = 0; i < 12; i++) {
        freq += digits[i] * pow(10, 11 - i);
    }

    uint64_t orig = freq;
    freq = std::clamp<uint64_t>(freq, minFreq, maxFreq);
    if (freq != orig && limitFreq) {
        setFrequency(freq);
    }
    else {
        frequency = orig;
    }

    ImGui::PopFont();

    ImGui::SetCursorPosX(digitBottomMaxs[11].x + (17.0f * style::uiScale));
}

void FrequencySelect::setFrequency(int64_t freq) {
    freq = std::max<int64_t>(0, freq);
    int i = 11;
    for (uint64_t f = freq; i >= 0; i--) {
        digits[i] = f % 10;
        f -= digits[i];
        f /= 10;
    }
    frequency = freq;
}
