#pragma once
#include <stdint.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <map>

// printf-style format checking for the *F formatting functions.
// MSVC has no equivalent attribute for plain variadics; expands to nothing there.
#if defined(__GNUC__) || defined(__clang__)
#define SMGUI_PRINTF(fmtIdx, vaIdx) __attribute__((format(printf, fmtIdx, vaIdx)))
#else
#define SMGUI_PRINTF(fmtIdx, vaIdx)
#endif

namespace SmGui {
    enum DrawStep {
        // Format calls
        DRAW_STEP_FILL_WIDTH = 0x00,
        DRAW_STEP_SAME_LINE,
        DRAW_STEP_BEGIN_DISABLED,
        DRAW_STEP_END_DISABLED,        
        
        // Widget calls
        DRAW_STEP_COMBO = 0x80,
        DRAW_STEP_BUTTON,
        DRAW_STEP_COLUMNS,
        DRAW_STEP_NEXT_COLUMN,
        DRAW_STEP_RADIO_BUTTON,
        DRAW_STEP_BEGIN_GROUP,
        DRAW_STEP_END_GROUP,
        DRAW_STEP_LEFT_LABEL,
        DRAW_STEP_SLIDER_INT,
        DRAW_STEP_SLIDER_FLOAT_WITH_STEPS,
        DRAW_STEP_INPUT_INT,
        DRAW_STEP_CHECKBOX,
        DRAW_STEP_SLIDER_FLOAT,
        DRAW_STEP_INPUT_TEXT,
        DRAW_STEP_TEXT,
        DRAW_STEP_TEXT_COLORED,
        DRAW_STEP_OPEN_POPUP,
        DRAW_STEP_BEGIN_POPUP,
        DRAW_STEP_END_POPUP,
        DRAW_STEP_BEGIN_TABLE,
        DRAW_STEP_END_TABLE,
        DRAW_STEP_TABLE_NEXT_ROW,
        DRAW_STEP_TABLE_SET_COLUMN_INDEX,
        DRAW_STEP_SET_NEXT_ITEM_WIDTH,
        // Appended after the original set to keep prior opcode values stable
        // across server/client versions.
        DRAW_STEP_SEPARATOR,
        DRAW_STEP_INPUT_FLOAT,
        DRAW_STEP_INPUT_DOUBLE
    };

    enum DrawListElemType {
        DRAW_LIST_ELEM_TYPE_DRAW_STEP,
        DRAW_LIST_ELEM_TYPE_BOOL,
        DRAW_LIST_ELEM_TYPE_INT,
        DRAW_LIST_ELEM_TYPE_FLOAT,
        DRAW_LIST_ELEM_TYPE_STRING,
        DRAW_LIST_ELEM_TYPE_DOUBLE,
    };

    struct DrawListElem {
        DrawListElemType type;
        DrawStep step;
        bool forceSync;
        // Exactly one scalar payload is live per element, selected by `type`.
        // They are mutually exclusive, so overlay them in a union to avoid
        // carrying all four in every element. `str` stays a separate member:
        // std::string is non-trivial and can't share the union without manual
        // lifetime management.
        union {
            bool b;
            int i;
            float f;
            double d;
        };
        std::string str;
    };

    enum FormatString {
        FMT_STR_NONE,
        FMT_STR_INT_DEFAULT,
        FMT_STR_INT_DB,
        FMT_STR_FLOAT_DEFAULT,
        FMT_STR_FLOAT_NO_DECIMAL,
        FMT_STR_FLOAT_ONE_DECIMAL,
        FMT_STR_FLOAT_TWO_DECIMAL,
        FMT_STR_FLOAT_THREE_DECIMAL,
        FMT_STR_FLOAT_DB_NO_DECIMAL,
        FMT_STR_FLOAT_DB_ONE_DECIMAL,
        FMT_STR_FLOAT_DB_TWO_DECIMAL,
        FMT_STR_FLOAT_DB_THREE_DECIMAL
    };

    extern std::map<FormatString, const char*> fmtStr;

    std::string ImStrToString(const char* imstr);

    class DrawList {
    public:
        void pushStep(DrawStep step, bool forceSync);
        void pushBool(bool b);
        void pushInt(int i);
        void pushFloat(float f);
        void pushDouble(double d);
        void pushString(std::string str);

        void draw(std::string& diffId, DrawListElem& diffValue, bool& syncRequired);
        
        static int loadItem(DrawListElem& elem, uint8_t* data, int len);
        int load(void* data, int len);
        static int storeItem(DrawListElem& elem, void* data, int len);
        int store(void* data, int len);
        static int getItemSize(DrawListElem& elem);
        int getSize();
        bool checkTypes(int firstId, int n, ...);
        bool validate();

        std::vector<DrawListElem> elements;
    };

    // Rec/Play functions
    // TODO: Maybe move verification to the load function instead of checking in drawFrame
    void init(bool server);
    void setDiff(std::string id, SmGui::DrawListElem value);
    void startRecord(DrawList* dl);
    void stopRecord();

    // Signaling Functions
    void ForceSync();
    
    // Format functions
    void FillWidth();
    void SameLine();
    void BeginDisabled();
    void EndDisabled();
    void Separator();

    // Widget functions
    bool Combo(const char *label, int *current_item, const char *items_separated_by_zeros, int popup_max_height_in_items = -1);
    bool Button(const char *label, ImVec2 size = ImVec2(0, 0));
    void Columns(int count = 1, const char *id = (const char *)0, bool border = true);
    void NextColumn();
    bool RadioButton(const char *label, bool active);
    void BeginGroup();
    void EndGroup();
    void LeftLabel(const char *text);
    bool SliderInt(const char *label, int *v, int v_min, int v_max, FormatString format = FMT_STR_INT_DEFAULT, ImGuiSliderFlags flags = 0);
    bool SliderFloatWithSteps(const char *label, float *v, float v_min, float v_max, float v_step, FormatString display_format = FMT_STR_FLOAT_THREE_DECIMAL);
    bool InputInt(const char *label, int *v, int step = 1, int step_fast = 100, ImGuiInputTextFlags flags = 0);
    bool InputFloat(const char *label, float *v, float step = 0.0f, float step_fast = 0.0f, const char *format = "%.3f", ImGuiInputTextFlags flags = 0);
    bool InputDouble(const char *label, double *v, double step = 0.0, double step_fast = 0.0, const char *format = "%.6f", ImGuiInputTextFlags flags = 0);
    bool Checkbox(const char *label, bool *v);
    bool SliderFloat(const char *label, float *v, float v_min, float v_max, FormatString format = FMT_STR_FLOAT_THREE_DECIMAL, ImGuiSliderFlags flags = 0);
    bool InputText(const char *label, char *buf, size_t buf_size, ImGuiInputTextFlags flags = 0);
    // Plain forms: the string is displayed verbatim. Dynamic / untrusted
    // strings (device labels, network-sourced names, error text) go here,
    // so a '%' in them is never interpreted.
    void Text(const char* str);
    void TextColored(const ImVec4 &col, const char *str);

    // printf-style formatting forms. Distinct names: an overload taking
    // (const char*, ...) would be ambiguous with the verbatim form above
    // for single-string calls.
    void TextF(const char* fmt, ...) SMGUI_PRINTF(1, 2);
    void TextColoredF(const ImVec4 &col, const char *fmt, ...) SMGUI_PRINTF(2, 3);
    void LeftLabelF(const char* fmt, ...) SMGUI_PRINTF(1, 2);
    void OpenPopup(const char *str_id, ImGuiPopupFlags popup_flags = 0);
    bool BeginPopup(const char *str_id, ImGuiWindowFlags flags = 0);
    void EndPopup();

    bool BeginTable(const char *str_id, int column, ImGuiTableFlags flags = 0, const ImVec2 &outer_size = ImVec2((0.0F), (0.0F)), float inner_width = (0.0F));
    void EndTable();
    void TableNextRow(ImGuiTableRowFlags row_flags = 0, float min_row_height = (0.0F));
    void TableSetColumnIndex(int column_n);
    void SetNextItemWidth(float item_width);

    // Config configs
    void ForceSyncForNext();

}
