// NOT ORIGINAL - the level editor's menu bar (ours; see level_editor_ui_strip.h): File, Edit, View, Tools, Window, Play,
// Help. Each item is an action of the registry (level_editor_actions.h) with its key as bound now, right-aligned;
// clicking it runs the action as its key would. Only ImGui's own API and the editor's modules' menu parts here (the
// Window menu's panels and layout, and Maximise on Play: level_editor_ui_app.h).

#include "editor/level_editor_ui.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_gizmo_lines.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_shell.h"
#include "editor/level_editor_split.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui_app.h"
#include "editor/level_editor_ui_strip.h"
#include "editor/level_editor_ux8.h"
#include "base/core/checked_index.h"
#include "imgui.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::ui {

namespace {

constexpr int kBookmarkSlots = 9; // camera bookmarks 1-9

// A menu item's id and its label.
struct MenuItem {
    const char* id{};
    const char* label = nullptr;
};

// A registry action as a menu item: its name (or `label`), its first key, its check and whether it can run now.
void get_item(MenuItem item)
{
    const auto* const id = item.id;
    const auto* const label = item.label;
    const int index = actions::get_find(id);
    const actions::ActStep* action = index >= 0 ? actions::get_pull(index) : nullptr;
    if (!action) {
        return;
    }
    const std::string keys = hints::leading_key_name(actions::get_keys_text(index));
    const int checked = actions::get_checked(index);
    if (ImGui::MenuItem(label ? label : action->name.c_str(), keys.empty() ? nullptr : keys.c_str(), checked == 1,
                        actions::is_switched_on(index))) {
        actions::get_run(index);
    }
    if (!action->tip.empty() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("%s", action->tip.c_str());
    }
}

void get_items(std::initializer_list<const char*> ids)
{
    for (const char* id : ids) {
        get_item({.id = id});
    }
}

// A menu's title, noted for the self-test (level_editor_ui_strip.h).
bool get_menu(const char* title)
{
    const bool open = ImGui::BeginMenu(title);
    ui_strip::get_note(title);
    return open;
}

void file_menu()
{
    if (!get_menu("File")) {
        return;
    }
    shell::file_menu_top(); // (the editor mode's Open Level, Recent - level_editor_shell.h)
    get_items({"save", "snapshots"});
    ImGui::Separator();
    get_items({"restore_autosave", "discard_autosave"});
    ImGui::Separator();
    get_item({.id = "reset_level"});
    shell::file_menu_bottom(); // (and Exit)
    ImGui::EndMenu();
}

void edit_menu(const ux8::Fonts& fonts)
{
    if (!get_menu("Edit")) {
        return;
    }
    get_items({"undo", "redo", "history", "changes"});
    ImGui::Separator();
    get_items({"copy", "paste", "copy_props", "paste_transform", "paste_fields"});
    ImGui::Separator();
    get_items({"duplicate", "linked_duplicate", "repeat_duplicate"});
    ImGui::Separator();
    get_items({"delete", "home", "lock"});
    ImGui::Separator();
    if (ImGui::BeginMenu("Select")) {
        get_items({"deselect", "select_all", "invert_selection", "select_similar", "select_family", "box_select"});
        ImGui::Separator();
        get_items({"select_touching", "select_cluster", "pick_units"});
        ImGui::Separator();
        get_items({"make_group", "ungroup", "create_prefab"});
        ux8::sets_menu(fonts);
        ImGui::EndMenu();
    }
    get_item({.id = "note_here"});
    ImGui::Separator();
    get_item({.id = "preferences"});
    ImGui::EndMenu();
}

void view_menu(const ShapeMesh& model, std::vector<Request>* requests)
{
    if (!get_menu("View")) {
        return;
    }
    overlays::view_mode_items();
    ImGui::Separator();
    if (ImGui::BeginMenu("Overlays")) {
        overlays::overlay_items(model, requests);
        ImGui::EndMenu();
    }
    get_items({"collision", "faces", "occlusion"});
    ImGui::Separator();
    get_items({"content_browser", "browser"});
    gizmo_lines::behind_menu(); // (level_editor_gizmo_lines.h)
    ImGui::Separator();
    get_items({"hide", "isolate", "show_all"});
    ImGui::Separator();
    if (ImGui::BeginMenu("Camera")) {
        get_items({"frame_selection", "follow_selection", "walk_mode", "teleport"});
        ImGui::Separator();
        get_items({"view_top", "view_bottom", "view_front", "view_back", "view_right", "view_left", "view_ortho"});
        ImGui::Separator();
        if (ImGui::BeginMenu("Bookmarks")) {
            for (int i = 1; i <= kBookmarkSlots; ++i) {
                get_item({.id = ("bookmark_" + std::to_string(i)).c_str()});
            }
            ImGui::Separator();
            for (int i = 1; i <= kBookmarkSlots; ++i) {
                get_item({.id = ("bookmark_store_" + std::to_string(i)).c_str()});
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        get_items({"faster", "slower", "camera_node", "log_camera"});
        ImGui::EndMenu();
    }
    split_view::layout_menu(); // (the Scene panel's views - level_editor_split.h)
    ImGui::EndMenu();
}

void tools_menu()
{
    if (!get_menu("Tools")) {
        return;
    }
    get_items({"tool_move", "tool_rotate", "tool_scale", "tool_space", "modal_transform"});
    ImGui::Separator();
    get_item({.id = "snap25"});
    xform::options_menu_items();
    get_items({"edge_snap", "carry_objects"});
    if (ImGui::BeginMenu("Pick")) {
        get_items({"pick_nodes", "pick_objects", "pick_scenery"});
        ImGui::EndMenu();
    }
    ImGui::Separator();
    get_items({"browser", "place", "drop"});
    ImGui::Separator();
    get_items({"ruler", "ruler_clear", "gap_wizard", "ground_tool", "edge_rail"});
    ImGui::Separator();
    if (ImGui::BeginMenu("Arrange")) {
        get_item({.id = "arrange_window"});
        ImGui::Separator();
        static constexpr std::string_view kAxes = "xyz";
        for (int axis = 0; axis < 3; ++axis) {
            for (const char* edge : {"min", "centre", "max"}) {
                get_item({.id = (std::string("align_") + core::at(kAxes, axis) + "_" + edge).c_str()});
            }
        }
        ImGui::Separator();
        get_items({"distribute_x", "distribute_y", "distribute_z"});
        ImGui::Separator();
        get_items({"replace_selection", "unlink"});
        ImGui::Separator();
        get_items({"drag_readout", "placement_ghost", "click_places"});
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Rails and Paths")) {
        get_items({"chain_insert", "chain_extend", "chain_link", "chain_unlink", "chain_finish"});
        ImGui::Separator();
        get_items({"rail_edges", "join_rails"});
        ImGui::EndMenu();
    }
    ImGui::Separator();
    get_item({.id = "controller_mode"});
    ImGui::EndMenu();
}

void window_menu()
{
    if (!get_menu("Window")) {
        return;
    }
    app::window_menu_panels();
    ImGui::Separator();
    get_items({"problems", "trail_window", "arrange_window", "adjust_last", "history", "changes", "snapshots"});
    ImGui::Separator();
    app::window_menu_layout();
    ImGui::EndMenu();
}

void play_menu()
{
    if (!get_menu("Play")) {
        return;
    }
    shell::play_menu_top(); // (the editor mode's Play: from the level's start - level_editor_shell.h)
    get_items({"play", "simulate"});
    ImGui::Separator();
    get_item({.id = "trail_window"});
    app::play_menu();
    ImGui::EndMenu();
}

// Tools still being tried out (level_editor_rail_tools.h's Auto Rails).
void experimental_menu()
{
    if (!get_menu("Experimental")) {
        return;
    }
    get_item({.id = "auto_rails"});
    ImGui::EndMenu();
}

void help_menu()
{
    if (!get_menu("Help")) {
        return;
    }
    get_items({"help", "command_search", "keys"});
    ImGui::Separator();
    if (ImGui::MenuItem("Show Tips")) {
        prefs::show_tips();
    }
    ImGui::EndMenu();
}

} // namespace

void menu_bar(const ShapeMesh& model, const ux8::Fonts& fonts, std::vector<Request>* requests)
{
    file_menu();
    edit_menu(fonts);
    view_menu(model, requests);
    tools_menu();
    window_menu();
    play_menu();
    experimental_menu();
    help_menu();
}

} // namespace frontend::level_editor::ui
