// NOT ORIGINAL - the split view's part of the Scene panel (ours): each view's overlay, borders and corner menu. See
// level_editor_split.h.

#include <array>
#include <string>

#include "editor/level_editor_camera.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_split.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_layout.h"
#include "imgui.h"

namespace frontend::level_editor::split_view {

namespace {

using view_cam::View;

constexpr float kCornerPad = 4.0F;
constexpr float kBorder = 1.0F;
constexpr float kActiveBorder = 2.0F;
constexpr ImU32 kBorderColour = IM_COL32(12, 12, 14, 255);
constexpr ImU32 kButtonColour = IM_COL32(20, 21, 24, 170);
constexpr ImU32 kButtonHover = IM_COL32(50, 52, 58, 220);
constexpr std::array<View, 7> kTypes = {
    {View::kNone, View::kTop, View::kBottom, View::kMenuSystem, View::kBack, View::kLeft, View::kRight}};

// A view's corner menu: its type, its mode, Maximise / Restore and the layout.
void view_menu(int slot)
{
    const View now = type_of(slot);
    for (const View type : kTypes) {
        if (ImGui::MenuItem(view_cam::view_name(type), nullptr, type == now)) {
            set_type(slot, type);
        }
    }
    ImGui::Separator();
    const int mode = mode_of(slot);
    for (int i = 0; i < overlays::kViewModes; ++i) {
        if (ImGui::MenuItem(overlays::view_mode_name(i), nullptr, i == mode)) {
            set_mode(slot, i);
        }
    }
    ImGui::Separator();
    if (layout() != Layout::kSingle && ImGui::MenuItem(maximised() ? "Restore Views" : "Maximise View", "F11")) {
        toggle_maximise();
    }
    layout_menu();
}

} // namespace

bool draw_panel(const PanelDraw& panel)
{
    const Layout shown = shown_layout();
    const int count = shown_count(shown);
    const bool split = count > 1;
    bool on_controls = false;
    ImGui::PushStyleColor(ImGuiCol_Button, kButtonColour);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kButtonHover);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kCornerPad * panel.scale, 1.0F * panel.scale));
    for (int place = 0; place < count; ++place) {
        const int slot = slot_at(shown, place);
        const Area area = area_at(shown, place);
        const ImVec2 from(panel.rect.x + (area.x * panel.rect.width), panel.rect.y + (area.y * panel.rect.height));
        const ImVec2 to(from.x + (area.width * panel.rect.width), from.y + (area.height * panel.rect.height));
        // The view's overlay, clipped to it.
        const ui::Overlay* overlay = slot == active() ? panel.active_overlay : overlay_of(slot);
        if (overlay && panel.draw_overlay) {
            panel.list->PushClipRect(from, to, true);
            panel.draw_overlay(*overlay);
            panel.list->PopClipRect();
        }
        if (split) {
            panel.list->AddRect(from, to, kBorderColour, 0.0F, 0, kBorder * panel.scale);
        }
        // The corner menu: "<type> - <mode>".
        ImGui::PushID(place);
        ImGui::SetCursorScreenPos(ImVec2(from.x + (kCornerPad * panel.scale), from.y + (kCornerPad * panel.scale)));
        const std::string label = std::string(view_cam::view_name(type_of(slot))) + "  " +
                                  overlays::view_mode_name(mode_of(slot));
        if (ImGui::SmallButton(label.c_str())) {
            ImGui::OpenPopup("view");
        }
        on_controls = on_controls || ImGui::IsItemHovered();
        if (ImGui::BeginPopup("view")) {
            view_menu(slot);
            ImGui::EndPopup();
        }
        // Maximise / restore, in the top right corner.
        if (layout() != Layout::kSingle) {
            const char* text = maximised() ? "Restore" : "Maximise";
            const float width = ImGui::CalcTextSize(text).x + (2.0F * kCornerPad * panel.scale);
            ImGui::SetCursorScreenPos(ImVec2(to.x - width - (kCornerPad * panel.scale), from.y + (kCornerPad * panel.scale)));
            if (ImGui::SmallButton(text)) {
                make_active(slot); // (the view the button is on)
                toggle_maximise();
            }
            on_controls = on_controls || ImGui::IsItemHovered();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    // The active view's highlight.
    if (split) {
        for (int place = 0; place < count; ++place) {
            if (slot_at(shown, place) != active()) {
                continue;
            }
            const Area area = area_at(shown, place);
            const ImVec2 from(panel.rect.x + (area.x * panel.rect.width), panel.rect.y + (area.y * panel.rect.height));
            const ImVec2 to(from.x + (area.width * panel.rect.width), from.y + (area.height * panel.rect.height));
            panel.list->AddRect(from, to, ImGui::GetColorU32(ImGuiCol_CheckMark), 0.0F, 0, kActiveBorder * panel.scale);
        }
    }
    return on_controls;
}

void layout_menu()
{
    if (!ImGui::BeginMenu("Layout")) {
        return;
    }
    for (int i = 0; i < kLayouts; ++i) {
        const auto option = static_cast<Layout>(i);
        if (ImGui::MenuItem(layout_name(option), nullptr, layout() == option)) {
            set_layout(option);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(maximised() ? "Restore Views" : "Maximise View", "F11", maximised(), layout() != Layout::kSingle)) {
        toggle_maximise();
    }
    ImGui::EndMenu();
}

} // namespace frontend::level_editor::split_view
