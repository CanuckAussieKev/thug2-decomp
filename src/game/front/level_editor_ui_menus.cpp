// NOT ORIGINAL - the level editor's menu bar (ours; see level_editor_ui_strip.h): File, Edit, View, Tools, Help. Each
// item is an action of the registry (level_editor_actions.h) with its key as bound now, right-aligned; clicking it runs
// the action as its key would. Only ImGui's own API and the editor's modules' menu parts here.

#include "game/front/level_editor_ui.h"

#include <string>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_hints.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_prefs.h"
#include "game/front/level_editor_transform.h"
#include "game/front/level_editor_ui_strip.h"
#include "game/front/level_editor_ux8.h"
#include "imgui.h"

namespace Front::LevelEditor::Ui {

namespace {

// A registry action as a menu item: its name (or `label`), its first key, its check and whether it can run now.
void Item(const char* id, const char* label = nullptr)
{
    int index = Actions::Find(id);
    const Actions::Action* action = index >= 0 ? Actions::Get(index) : nullptr;
    if (!action)
        return;
    std::string keys = Hints::FirstKey(Actions::KeysText(index));
    int checked = Actions::Checked(index);
    if (ImGui::MenuItem(label ? label : action->name.c_str(), keys.empty() ? nullptr : keys.c_str(), checked == 1,
                        Actions::Enabled(index)))
        Actions::Run(index);
    if (!action->tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", action->tip.c_str());
}

void Items(std::initializer_list<const char*> ids)
{
    for (const char* id : ids)
        Item(id);
}

// A menu's title, noted for the self-test (level_editor_ui_strip.h).
bool Menu(const char* title)
{
    bool open = ImGui::BeginMenu(title);
    UiStrip::Note(title);
    return open;
}

void FileMenu()
{
    if (!Menu("File"))
        return;
    Items({"save", "snapshots"});
    ImGui::Separator();
    Items({"restore_autosave", "discard_autosave"});
    ImGui::Separator();
    Items({"play", "simulate"});
    ImGui::Separator();
    Item("reset_level");
    ImGui::EndMenu();
}

void EditMenu(const Ux8::Fonts& fonts)
{
    if (!Menu("Edit"))
        return;
    Items({"undo", "redo", "history", "changes"});
    ImGui::Separator();
    Items({"copy", "paste", "copy_props", "paste_transform", "paste_fields"});
    ImGui::Separator();
    Items({"duplicate", "linked_duplicate", "repeat_duplicate"});
    ImGui::Separator();
    Items({"delete", "home", "lock"});
    ImGui::Separator();
    if (ImGui::BeginMenu("Select")) {
        Items({"deselect", "select_all", "invert_selection", "select_similar", "select_family", "box_select"});
        ImGui::Separator();
        Item("make_group");
        Ux8::SetsMenu(fonts);
        ImGui::EndMenu();
    }
    Item("note_here");
    ImGui::Separator();
    Item("preferences");
    ImGui::EndMenu();
}

void ViewMenu(const Model& model, std::vector<Request>* requests)
{
    if (!Menu("View"))
        return;
    Overlays::ViewModeItems();
    ImGui::Separator();
    if (ImGui::BeginMenu("Overlays")) {
        Overlays::OverlayItems(model, requests);
        ImGui::EndMenu();
    }
    Items({"faces", "occlusion"});
    ImGui::Separator();
    Items({"hide", "isolate", "show_all"});
    ImGui::Separator();
    if (ImGui::BeginMenu("Camera")) {
        Items({"frame_selection", "follow_selection", "walk_mode", "teleport"});
        ImGui::Separator();
        Items({"view_top", "view_bottom", "view_front", "view_back", "view_right", "view_left", "view_ortho"});
        ImGui::Separator();
        if (ImGui::BeginMenu("Bookmarks")) {
            for (int i = 1; i <= 9; ++i)
                Item(("bookmark_" + std::to_string(i)).c_str());
            ImGui::Separator();
            for (int i = 1; i <= 9; ++i)
                Item(("bookmark_store_" + std::to_string(i)).c_str());
            ImGui::EndMenu();
        }
        ImGui::Separator();
        Items({"faster", "slower", "camera_node", "log_camera"});
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Windows")) {
        Items({"problems", "trail_window", "arrange_window", "adjust_last", "history", "changes", "snapshots"});
        ImGui::EndMenu();
    }
    ImGui::EndMenu();
}

void ToolsMenu()
{
    if (!Menu("Tools"))
        return;
    Items({"tool_move", "tool_rotate", "tool_scale", "tool_space", "modal_transform"});
    ImGui::Separator();
    Item("snap25");
    Transform::OptionsMenuItems();
    Items({"edge_snap", "carry_objects"});
    if (ImGui::BeginMenu("Pick")) {
        Items({"pick_nodes", "pick_objects", "pick_scenery"});
        ImGui::EndMenu();
    }
    ImGui::Separator();
    Items({"browser", "place", "drop"});
    ImGui::Separator();
    Items({"ruler", "ruler_clear", "gap_wizard", "ground_tool", "edge_rail"});
    ImGui::Separator();
    if (ImGui::BeginMenu("Arrange")) {
        Item("arrange_window");
        ImGui::Separator();
        static const char* const kAxes = "xyz";
        for (int axis = 0; axis < 3; ++axis) {
            for (const char* edge : {"min", "centre", "max"})
                Item((std::string("align_") + kAxes[axis] + "_" + edge).c_str());
        }
        ImGui::Separator();
        Items({"distribute_x", "distribute_y", "distribute_z"});
        ImGui::Separator();
        Items({"replace_selection", "unlink"});
        ImGui::Separator();
        Items({"drag_readout", "placement_ghost", "click_places"});
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Rails and Paths")) {
        Items({"chain_insert", "chain_extend", "chain_link", "chain_unlink", "chain_finish"});
        ImGui::EndMenu();
    }
    ImGui::Separator();
    Item("controller_mode");
    ImGui::EndMenu();
}

void HelpMenu()
{
    if (!Menu("Help"))
        return;
    Items({"help", "command_search", "keys"});
    ImGui::Separator();
    if (ImGui::MenuItem("Show Tips"))
        Prefs::ShowTips();
    ImGui::EndMenu();
}

} // namespace

void MenuBar(const Model& model, const Ux8::Fonts& fonts, std::vector<Request>* requests)
{
    FileMenu();
    EditMenu(fonts);
    ViewMenu(model, requests);
    ToolsMenu();
    HelpMenu();
}

} // namespace Front::LevelEditor::Ui
