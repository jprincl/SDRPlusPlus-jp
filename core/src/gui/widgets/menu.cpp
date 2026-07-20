#include <gui/widgets/menu.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <gui/style.h>

Menu::Menu() {
}

void Menu::registerEntry(std::string name, void (*drawHandler)(void* ctx), void* ctx, ModuleManager::Instance* inst) {
    MenuItem_t item;
    item.drawHandler = drawHandler;
    item.ctx = ctx;
    item.inst = inst;
    items[name] = item;
    if (!isInOrderList(name)) {
        MenuOption_t opt;
        opt.name = name;
        opt.open = true;
        order.push_back(opt);
    }
}

void Menu::removeEntry(std::string name) {
    items.erase(name);
}

bool Menu::draw(bool updateStates) {
    bool changed = false;
    float menuWidth = ImGui::GetContentRegionAvail().x;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& imStyle = ImGui::GetStyle();
    // The enable checkbox is a GetFrameHeight() square — the exact height of a
    // CollapsingHeader — placed flush with the header's right edge and
    // top-aligned with the header (the cursor sits one ItemSpacing.y below the
    // header when the checkbox is drawn, hence the negative Y). Derived from
    // live style metrics so it stays aligned under the touch style overlay.
    float checkboxSize = ImGui::GetFrameHeight();
    ImVec2 checkboxOffset = ImVec2(menuWidth - checkboxSize, -(checkboxSize + imStyle.ItemSpacing.y));
    // Width reserved at the header's right so the label ellipsis stops short
    // of the checkbox.
    float headerLabelReserve = checkboxSize + imStyle.ItemInnerSpacing.x;

    int displayedCount = 0;
    int rawId = 0;

    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    // Index-based iteration: a menu's draw handler may create a module
    // instance (Module Manager "+"), whose constructor registers a menu entry
    // and thus appends to `order` mid-loop. Re-evaluating size()/order[i]
    // every pass keeps the loop valid across the vector reallocation, where
    // a range-for's cached iterators would dangle (issue #816).
    for (size_t i = 0; i < order.size(); i++) {
        MenuOption_t& opt = order[i];
        rawId++;
        if (items.find(opt.name) == items.end()) {
            continue;
        }
        if (opt.name == draggedMenuName) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", draggedMenuName.c_str());
            ImGui::EndTooltip();
            continue;
        }

        // Draw dragged menu item
        if (displayedCount == insertBefore && !draggedMenuName.empty()) {
            if (updateStates) { ImGui::SetNextItemOpen(false); }
            ImVec2 posMin = ImGui::GetCursorScreenPos();
            ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
            style::beginDisabled();
            ImRect orignalRect = window->WorkRect;
            ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
            if (items[draggedOpt.name].inst != NULL) {
                window->WorkRect = orignalRect;
                ImVec2 pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = items[draggedOpt.name].inst->isEnabled();
                ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
                ImGui::SetCursorPos(pos);
            }
            style::endDisabled();
            window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::lineWidth());
        }
        displayedCount++;

        MenuItem_t& item = items[opt.name];


        ImRect orginalRect = window->WorkRect;
        if (item.inst != NULL) {
            window->WorkRect = ImRect(orginalRect.Min, ImVec2(orginalRect.Max.x - headerLabelReserve, orginalRect.Max.y));
        }

        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());

        headerTops[displayedCount - 1] = posMin.y;
        optionIDs[displayedCount - 1] = rawId - 1;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(posMin, posMax)) {
            menuClicked = true;
            clickedMenuName = opt.name;
        }

        bool menuDragged = (menuClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && draggedMenuName.empty() && clickedMenuName == opt.name);
        if (menuDragged && !locked) {
            draggedMenuName = opt.name;
            draggedId = rawId - 1;
            draggedOpt = opt;
            continue;
        }
        else if (menuDragged) {
            ImGui::SetTooltip("Menu Order Locked!");
        }

        // Draw menu header and menu content. There is a lot of boilerplate because the checkbox has to be drawn before the menu, TODO: fix
        if (updateStates) { ImGui::SetNextItemOpen(opt.open); }
        if (ImGui::CollapsingHeader((opt.name + "##sdrpp_main_menu").c_str())) {
            if (item.inst != NULL) {
                window->WorkRect = orginalRect;
                ImVec2 pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = item.inst->isEnabled();
                if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                    enabled ? item.inst->enable() : item.inst->disable();
                    changed = true;
                }
                ImGui::SetCursorPos(pos);
            }

            // Check if the state changed
            if (!opt.open && !updateStates) {
                opt.open = true;
                changed = true;
            }

            item.drawHandler(item.ctx);
            ImGui::Spacing();
        }
        else if (item.inst != NULL) {
            window->WorkRect = orginalRect;
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
            ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
            bool enabled = item.inst->isEnabled();
            if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                enabled ? item.inst->enable() : item.inst->disable();
                changed = true;
            }
            ImGui::SetCursorPos(pos);

            if (opt.open && !updateStates) {
                opt.open = false;
                changed = true;
            }
        }
        else if (opt.open && !updateStates) {
            opt.open = false;
            changed = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && menuClicked) {

        if (!draggedMenuName.empty()) {
            // Move menu
            order.erase(order.begin() + draggedId);

            if (insertBefore == displayedCount) {
                order.push_back(draggedOpt);
            }
            else if (insertBeforeName != "") {
                int beforeId = 0;
                for (int i = 0; i < order.size(); i++) {
                    if (order[i].name == insertBeforeName) {
                        beforeId = i;
                        break;
                    }
                }
                order.insert(order.begin() + beforeId, draggedOpt);
            }
            changed = true;
        }

        menuClicked = false;
        draggedMenuName = "";
        insertBeforeName = "";
        insertBefore = -1;
    }

    // TODO: Figure out why the hell this is needed
    if (insertBefore == displayedCount && !draggedMenuName.empty()) {
        if (updateStates) { ImGui::SetNextItemOpen(false); }
        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
        style::beginDisabled();
        ImRect orignalRect = window->WorkRect;
        ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
        if (items[draggedOpt.name].inst != NULL) {
            window->WorkRect = orignalRect;
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
            ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
            bool enabled = items[draggedOpt.name].inst->isEnabled();
            ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
            ImGui::SetCursorPos(pos);
        }
        style::endDisabled();
        window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::lineWidth());
    }

    if (!draggedMenuName.empty()) {
        insertBefore = displayedCount;
        ImVec2 mPos = ImGui::GetMousePos();
        for (int i = 0; i < displayedCount; i++) {
            if (headerTops[i] > mPos.y) {
                insertBefore = i;
                insertBeforeName = order[optionIDs[i]].name;
                break;
            }
        }
    }

    return changed;
}

bool Menu::isInOrderList(std::string name) {
    for (MenuOption_t opt : order) {
        if (opt.name == name) {
            return true;
        }
    }
    return false;
}
