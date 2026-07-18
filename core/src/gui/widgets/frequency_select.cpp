#include <gui/widgets/frequency_select.h>
#include <config.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <backend.h>
#include <utils/hrfreq.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#ifdef __ANDROID__
#include <android_backend.h>
#endif

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
                    pressDigit = i;
                    pressDir = 1;
                    longPressDone = false;
                }
                onDigit = true;
            }
            if (isInArea(mousePos, digitBottomMins[i], digitBottomMaxs[i])) {
                window->DrawList->AddRectFilled(digitBottomMins[i], digitBottomMaxs[i], IM_COL32(0, 0, 255, 75));
                if (leftClick) {
                    pressDigit = i;
                    pressDir = -1;
                    longPressDone = false;
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
                        if ((i + j) > 11) { break; }
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

        // A press armed on a digit half steps it on a quick release; held
        // motionless past the threshold it opens the F-INP keypad instead.
        // Stepping waits for the release so the two can't both fire.
        if (pressDigit >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float slop = 10.0f * style::uiScale;
                float dx = mousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x;
                float dy = mousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y;
                if ((dx * dx) + (dy * dy) > (slop * slop)) {
                    pressDigit = -1; // moved away: neither a tap nor a long press
                }
                else if (!longPressDone && io.MouseDownDuration[ImGuiMouseButton_Left] >= 0.5f) {
                    longPressDone = true;
                    keypadRequestOpen = true;
#ifdef __ANDROID__
                    backend::hapticTick();
#endif
                }
            }
            else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (!longPressDone) {
                    if (pressDir > 0 && isInArea(mousePos, digitTopMins[pressDigit], digitTopMaxs[pressDigit])) {
                        incrementDigit(pressDigit);
                    }
                    else if (pressDir < 0 && isInArea(mousePos, digitBottomMins[pressDigit], digitBottomMaxs[pressDigit])) {
                        decrementDigit(pressDigit);
                    }
                }
                pressDigit = -1;
            }
            else {
                pressDigit = -1; // press was lost without a release event
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

    drawKeypad();
}

// Format a frequency in Hz with '.' group separators, matching the widget.
static std::string groupHz(uint64_t hz) {
    std::string s = std::to_string(hz);
    for (int pos = (int)s.size() - 3; pos > 0; pos -= 3) {
        s.insert(pos, ".");
    }
    return s;
}

static int digitCount(uint64_t value) {
    int count = 1;
    while (value >= 10) {
        value /= 10;
        count++;
    }
    return count;
}

static int keypadIntegerDigitLimit(bool limitFreq, uint64_t maxFreq) {
    if (!limitFreq) { return 6; } // dialog entry is MHz, capped at 999999 MHz
    uint64_t maxMHz = maxFreq / 1000000;
    return std::min(6, digitCount(maxMHz));
}

static uint64_t clampKeypadHz(double hz, bool limitFreq, uint64_t minFreq, uint64_t maxFreq) {
    if (!std::isfinite(hz) || hz < 0.0) { hz = 0.0; }
    hz = std::min(hz, 999999999999.0);

    if (limitFreq) {
        const uint64_t lo = std::min(minFreq, maxFreq);
        const uint64_t hi = std::max(minFreq, maxFreq);
        hz = std::clamp(hz, (double)lo, (double)hi);
    }

    return (uint64_t)hz;
}

void FrequencySelect::keypadKey(char key) {
    if (key >= '0' && key <= '9') {
        size_t dot = entry.find('.');
        if (dot == std::string::npos) {
            if (entry == "0") { entry.clear(); }
            uint64_t activeMax = limitFreq ? std::max(minFreq, maxFreq) : maxFreq;
            if ((int)entry.size() < keypadIntegerDigitLimit(limitFreq, activeMax)) { entry += key; }
        }
        else if (entry.size() - dot - 1 < 6) { // down to 1 Hz
            entry += key;
        }
    }
    else if (key == '.') {
        if (entry.empty()) {
            // IC-705 shorthand: [.] first re-enters the current MHz digits, so
            // retuning within the band is just [.] plus the kHz digits.
            uint64_t currentHz = clampKeypadHz((double)frequency, limitFreq, minFreq, maxFreq);
            entry = std::to_string(currentHz / 1000000) + '.';
        }
        else if (entry.find('.') == std::string::npos) {
            entry += '.';
        }
    }
}

void FrequencySelect::commitEntry() {
    if (entry.empty()) { return; }
    // The entry is a decimal number in MHz; digits left blank below the last
    // entered one become zeros, same as the IC-705's [ENT].
    uint64_t hz = clampKeypadHz(round(atof(entry.c_str()) * 1e6), limitFreq, minFreq, maxFreq);
    setFrequency((int64_t)hz);
    frequencyChanged = true;
}

void FrequencySelect::drawKeypad() {
    if (keypadRequestOpen) {
        keypadRequestOpen = false;
        entry.clear();
        ImGui::OpenPopup("F-INP##sdrpp_freq_keypad");
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("F-INP##sdrpp_freq_keypad", NULL, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

    ImGuiIO& kio = ImGui::GetIO();
    ImVec2 keySz(style::dp(56.0f), style::dp(42.0f));
    const bool hasRange = limitFreq;
    const uint64_t rangeLo = hasRange ? std::min(minFreq, maxFreq) : 0;
    const uint64_t rangeHi = hasRange ? std::max(minFreq, maxFreq) : 0;
    const ImVec4 errorCol(1.0f, 0.20f, 0.12f, 1.0f);
    double rawHz = 0.0;
    uint64_t targetHz = 0;
    bool belowRange = false;
    bool aboveRange = false;
    if (!entry.empty()) {
        rawHz = std::min(round(atof(entry.c_str()) * 1e6), 999999999999.0);
        targetHz = clampKeypadHz(rawHz, limitFreq, minFreq, maxFreq);
        belowRange = hasRange && rawHz < (double)rangeLo;
        aboveRange = hasRange && rawHz > (double)rangeHi;
    }
    const bool outOfRange = belowRange || aboveRange;

    // Entered value in MHz; before any key, the current frequency dimmed.
    ImGui::PushFont(style::bigFont);
    if (entry.empty()) {
        char cur[32];
        snprintf(cur, sizeof(cur), "%.6f", (double)frequency / 1e6);
        char* end = cur + strlen(cur) - 1;
        while (*end == '0') { *end-- = 0; }
        if (*end == '.') { *end = 0; }
        ImGui::TextDisabled("%s", cur);
    }
    else {
        if (outOfRange) { ImGui::PushStyleColor(ImGuiCol_Text, errorCol); }
        ImGui::TextUnformatted(entry.c_str());
        if (outOfRange) { ImGui::PopStyleColor(); }
    }
    ImGui::PopFont();

    if (entry.empty()) {
        ImGui::TextDisabled("Enter frequency in MHz");
    }
    else {
        if (outOfRange) { ImGui::PushStyleColor(ImGuiCol_Text, errorCol); }
        if (limitFreq && targetHz != (uint64_t)rawHz) {
            ImGui::Text("= %s Hz -> %s Hz", groupHz((uint64_t)rawHz).c_str(), groupHz(targetHz).c_str());
        }
        else {
            ImGui::Text("= %s Hz", groupHz(targetHz).c_str());
        }
        if (outOfRange) { ImGui::PopStyleColor(); }
        if (belowRange) {
            ImGui::TextColored(errorCol, "Below source range: minimum is %s Hz", groupHz(rangeLo).c_str());
        }
        else if (aboveRange) {
            ImGui::TextColored(errorCol, "Above source range: maximum is %s Hz", groupHz(rangeHi).c_str());
        }
    }
    if (hasRange) {
        ImGui::TextDisabled("Range: %s - %s Hz", groupHz(rangeLo).c_str(), groupHz(rangeHi).c_str());
    }
    ImGui::Spacing();

    // 4x4 grid, placed explicitly since the double-height ENT would otherwise
    // stretch its row: digit block with the backspace at its bottom-right
    // (Android PIN-pad convention), function column CE / Cancel / tall ENT.
    ImVec2 sp = ImGui::GetStyle().ItemSpacing;
    float fnW = style::dp(84.0f);
    ImVec2 origin = ImGui::GetCursorPos();
    auto cellPos = [&](int r, int c) {
        return ImVec2(origin.x + c * (keySz.x + sp.x), origin.y + r * (keySz.y + sp.y));
    };
    bool close = false;

    const char* dig[4][3] = {
        { "7", "8", "9" },
        { "4", "5", "6" },
        { "1", "2", "3" },
        { ".", "0", NULL } // NULL = backspace, drawn below
    };
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            if (!dig[r][c]) { continue; }
            ImGui::SetCursorPos(cellPos(r, c));
            if (ImGui::Button(dig[r][c], keySz)) { keypadKey(dig[r][c][0]); }
        }
    }

    // Backspace key: undoes the last keypress. Roboto-Medium ships no arrow or
    // erase glyph, so the icon is drawn by hand; GetColorU32 picks up the
    // disabled-state alpha automatically.
    ImGui::SetCursorPos(cellPos(3, 2));
    ImGui::BeginDisabled(entry.empty());
    bool bksp = ImGui::Button("##sdrpp_finp_bksp", keySz);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 bmin = ImGui::GetItemRectMin();
        ImVec2 bmax = ImGui::GetItemRectMax();
        ImVec2 ctr((bmin.x + bmax.x) / 2.0f, (bmin.y + bmax.y) / 2.0f);
        float iw = style::dp(11.0f);    // icon half-width
        float ih = style::dp(7.0f);     // icon half-height
        float notch = style::dp(7.0f);  // depth of the pointed tip
        float th = style::dp(1.5f);
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        ImVec2 pts[5] = {
            ImVec2(ctr.x - iw, ctr.y),
            ImVec2(ctr.x - iw + notch, ctr.y - ih),
            ImVec2(ctr.x + iw, ctr.y - ih),
            ImVec2(ctr.x + iw, ctr.y + ih),
            ImVec2(ctr.x - iw + notch, ctr.y + ih)
        };
        dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed, th);
        float bx = ctr.x + notch / 2.0f; // center of the icon body
        float xr = style::dp(3.0f);
        dl->AddLine(ImVec2(bx - xr, ctr.y - xr), ImVec2(bx + xr, ctr.y + xr), col, th);
        dl->AddLine(ImVec2(bx - xr, ctr.y + xr), ImVec2(bx + xr, ctr.y - xr), col, th);
    }
    ImGui::EndDisabled();
    if (bksp) { entry.pop_back(); }

    ImGui::SetCursorPos(cellPos(0, 3));
    if (ImGui::Button("CE", ImVec2(fnW, keySz.y))) { entry.clear(); }
    ImGui::SetCursorPos(cellPos(1, 3));
    if (ImGui::Button("Cancel##sdrpp_finp", ImVec2(fnW, keySz.y))) { close = true; }
    ImGui::SetCursorPos(cellPos(2, 3));
    if (ImGui::Button("ENT##sdrpp_finp", ImVec2(fnW, 2.0f * keySz.y + sp.y))) {
        commitEntry();
        close = true;
    }

    // Hardware keyboard: digits, '.', Backspace, Enter, Escape.
    for (int j = 0; j < kio.InputQueueCharacters.Size; j++) {
        char c = (char)kio.InputQueueCharacters[j];
        if ((c >= '0' && c <= '9') || c == '.') { keypadKey(c); }
        else if (c == ',') { keypadKey('.'); }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !entry.empty()) { entry.pop_back(); }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        commitEntry();
        close = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { close = true; }

    if (close) { ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
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
