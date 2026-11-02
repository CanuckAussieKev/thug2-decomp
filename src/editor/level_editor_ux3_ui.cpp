// NOT ORIGINAL - the level editor's history, changes, search and organising: the panels (ours). See level_editor_ux3.h.
// Only ImGui's own API here (and imgui_internal.h for the Inspector's folding state).

#include "editor/level_editor_ux3.h"

#include <SDL2/SDL.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_changes.h"
#include "editor/level_editor_clip.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_ui.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::ux3 {

namespace {

Fonts g_fonts;
constexpr ImVec4 kAccent(0.82f, 0.34f, 0.11f, 0.85f);
constexpr ImVec4 kKeyColour(1.0f, 0.78f, 0.45f, 1.0f);
constexpr ImVec4 kMuted(0.6f, 0.61f, 0.63f, 1.0f);

float current_s() { return g_fonts.scale > 0.0f ? g_fonts.scale : 1.0f; }

// The right end of the window's content (local x), a scrollbar's room left.
float get_right_edge()
{
    return ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ScrollbarSize;
}

void get_mono(const char* text)
{
    if (g_fonts.mono)
        ImGui::PushFont(g_fonts.mono);
    ImGui::TextUnformatted(text);
    if (g_fonts.mono)
        ImGui::PopFont();
}

void current_heading(const char* text)
{
    if (g_fonts.heading)
        ImGui::PushFont(g_fonts.heading);
    ImGui::TextUnformatted(text);
    if (g_fonts.heading)
        ImGui::PopFont();
}

// A window over the view, placed the first time (then where the user leaves it this session).
bool begin_floating(const char* title, bool* open, ImVec2 pos, ImVec2 size)
{
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    return ImGui::Begin(title, open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
}

// --- the command search ---

std::array<char, 128> g_query;
int g_highlight;
bool g_palette_was_open;

void get_palette()
{
    bool& open = unseal(kPaletteWindow);
    if (!open) {
        g_palette_was_open = false;
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.1f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(640.0f * current_s(), 0.0f));
    ImGui::Begin("Command Search###ux3palette", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_AlwaysAutoResize);
    if (!g_palette_was_open) {
        core::at(g_query, 0) = 0;
        g_highlight = 0;
        ImGui::SetKeyboardFocusHere();
        g_palette_was_open = true;
    }
    ImGui::SetNextItemWidth(-1.0f);
    const bool enter = ImGui::InputTextWithHint("##query", "Search commands\xE2\x80\xA6", g_query.data(), sizeof(g_query),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive() || enter) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            ++g_highlight;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            --g_highlight;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            open = false;
    }
    const std::vector<actions::ActStep>& all = actions::get_all();
    std::vector<std::pair<int, int>> found; // (score, index)
    for (int i = 0; std::cmp_less(i, all.size()); ++i) {
        const actions::ActStep& a = core::at(all, i);
        const int score = std::max({actions::fuzzy_score(g_query.data(), a.name) * 2,
                                    actions::fuzzy_score(g_query.data(), a.category + " " + a.name),
                                    std::strlen(g_query.data()) >= 3 ? actions::fuzzy_score(g_query.data(), a.tip) / 2 : 0,
                                    actions::fuzzy_score(g_query.data(), actions::get_keys_text(i))});
        if (score > 0)
            found.emplace_back(score, i);
    }
    std::ranges::stable_sort(found, [](const auto& a, const auto& b) { return a.first > b.first; });
    g_highlight = found.empty() ? 0 : std::clamp(g_highlight, 0, static_cast<int>(found.size()) - 1);
    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild(
        "##results",
        ImVec2(0.0f, row_height * std::min<float>(14.0f, std::max<float>(1.0f, static_cast<float>(found.size()))) +
                         8.0f * current_s()));
    int run = -1;
    for (int r = 0; std::cmp_less(r, found.size()); ++r) {
        const int i = core::at(found, r).second;
        const actions::ActStep& a = core::at(all, i);
        const bool enabled = actions::is_switched_on(i);
        ImGui::PushID(i);
        const int checked = actions::get_checked(i);
        const std::string text = (checked == 1 ? "[x] " : checked == 0 ? "[ ] " : "") + a.name;
        if (!enabled)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(text.c_str(), r == g_highlight))
            run = i;
        if (!enabled)
            ImGui::PopStyleColor();
        if (r == g_highlight && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            ImGui::SetScrollHereY(0.5f);
        if (ImGui::IsItemHovered() && !a.tip.empty())
            gui::tooltip("%s%s", a.tip.c_str(), a.kind == actions::Kind::kDetailsOf ? "\n(mouse gesture)" : "");
        ImGui::SameLine(0.0f, 10.0f * current_s());
        gui::text_colored(kMuted, "%s", a.category.c_str());
        const std::string keys = actions::get_keys_text(i);
        if (!keys.empty()) {
            if (g_fonts.mono)
                ImGui::PushFont(g_fonts.mono);
            const float width = ImGui::CalcTextSize(keys.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f * current_s(), get_right_edge() - width));
            gui::text_colored(kKeyColour, "%s", keys.c_str());
            if (g_fonts.mono)
                ImGui::PopFont();
        }
        ImGui::PopID();
    }
    if (found.empty())
        gui::text_disabled("Nothing matches");
    ImGui::EndChild();
    if (enter && !found.empty())
        run = core::at(found, g_highlight).second;
    ImGui::End();
    if (run >= 0) {
        open = false;
        if (actions::is_switched_on(run)) {
            get_queue([run] { actions::get_run(run); });
        } else {
            const std::string why = core::at(all, run).kind == actions::Kind::kDetailsOf
                                        ? core::at(all, run).name + ": " + actions::get_keys_text(run)
                                        : core::at(all, run).name + " is unavailable";
            get_queue([why] { show_message(why); });
        }
    }
}

// --- the undo history ---

void history_window()
{
    bool& open = unseal(kHistoryWindow);
    if (!open)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!begin_floating("Undo history###ux3history", &open,
                        ImVec2(io.DisplaySize.x - 700.0f * current_s(), 90.0f * current_s()),
                        ImVec2(340.0f * current_s(), 420.0f * current_s()))) {
        ImGui::End();
        return;
    }
    std::vector<EditorUndoRecord> undo = undo_history(), redo = redo_history();
    gui::text_disabled("%d undo, %d redo", static_cast<int>(undo.size()), static_cast<int>(redo.size()));
    ImGui::Separator();
    ImGui::BeginChild("##steps");
    int jump = 0;
    bool any = false;
    if (ImGui::Selectable("(start)", undo.empty()))
        jump = -static_cast<int>(undo.size()), any = true;
    for (int i = 0; std::cmp_less(i, undo.size()); ++i) {
        ImGui::PushID(i);
        const bool current = i == static_cast<int>(undo.size()) - 1;
        std::array<char, 200> text{};
        text::format_to(text, "%2d  %s", i + 1, core::at(undo, i).label.c_str());
        if (ImGui::Selectable(text.data(), current))
            jump = -(static_cast<int>(undo.size()) - 1 - i), any = true;
        if (ImGui::IsItemHovered() && !core::at(undo, i).detail.empty())
            gui::tooltip("Selection: %s", core::at(undo, i).detail.c_str());
        if (current)
            ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    for (int j = 0; std::cmp_less(j, redo.size()); ++j) {
        ImGui::PushID(1000 + j);
        std::array<char, 200> text{};
        text::format_to(text, "%2d  %s  (undone)", static_cast<int>(undo.size()) + j + 1,
                        core::at(redo, j).label.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(text.data(), false))
            jump = j + 1, any = true;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !core::at(redo, j).detail.empty())
            gui::tooltip("Selection: %s", core::at(redo, j).detail.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    if (any && jump)
        get_queue([jump] { jump_history(jump); });
}

// --- the changes ---

std::array<char, 64> g_changes_filter;

void changes_window()
{
    bool& open = unseal(kChangesWindow);
    if (!open)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!begin_floating("Changes###ux3changes", &open,
                        ImVec2(io.DisplaySize.x * 0.5f - 260.0f * current_s(), 90.0f * current_s()),
                        ImVec2(560.0f * current_s(), 480.0f * current_s()))) {
        ImGui::End();
        return;
    }
    const std::vector<ChangeRow>& rows = change_list();
    gui::text_disabled("%d changes", static_cast<int>(rows.size()));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter\xE2\x80\xA6", g_changes_filter.data(), sizeof(g_changes_filter));
    ImGui::Separator();
    ImGui::BeginChild("##changes");
    if (rows.empty())
        gui::text_disabled("No changes");
    for (const char* group : changes::bunches()) {
        std::vector<const ChangeRow*> in;
        for (const ChangeRow& row : rows) {
            if (row.entry.group == group && actions::fuzzy_score(g_changes_filter.data(), row.entry.summary + " " + group) > 0)
                in.push_back(&row);
        }
        if (in.empty())
            continue;
        std::array<char, 96> header{};
        text::format_to(header, "%s (%d)###chg_%s", group, static_cast<int>(in.size()), group);
        if (!ImGui::CollapsingHeader(header.data(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        for (const ChangeRow* row : in) {
            ImGui::PushID(row->entry.line);
            const float buttons =
                ImGui::CalcTextSize("Go to  Revert").x + 4.0f * ImGui::GetStyle().FramePadding.x + 12.0f * current_s();
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - buttons);
            ImGui::TextUnformatted(row->entry.summary.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered())
                gui::tooltip("%s", row->record.c_str());
            ImGui::SameLine(get_right_edge() - buttons + 6.0f * current_s());
            ImGui::BeginDisabled(row->entry.id == 0);
            if (ImGui::SmallButton("Go to"))
                go_to(*row);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert"))
                revert(*row);
            if (ImGui::IsItemHovered())
                gui::tooltip("Revert this change");
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- the keys ---

std::array<char, 64> g_keys_filter;

void keys_window()
{
    bool& open = unseal(kKeysWindow);
    if (!open) {
        if (actions::get_capturing())
            actions::cancel_capture();
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (!begin_floating("Keys###ux3keys", &open,
                        ImVec2(io.DisplaySize.x * 0.5f - 330.0f * current_s(), 80.0f * current_s()),
                        ImVec2(660.0f * current_s(), io.DisplaySize.y * 0.75f))) {
        ImGui::End();
        return;
    }
    keys_section();
    ImGui::End();
}

// --- the snapshots ---

std::array<char, 40> g_snapshot_name;
std::vector<std::string> g_snapshots;
uint32_t g_snapshots_at;
bool g_snapshots_stale = true;

void snapshots_window()
{
    bool& open = unseal(kSnapshotsWindow);
    if (!open) {
        g_snapshots_stale = true;
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (!begin_floating("Layout snapshots###ux3snapshots", &open,
                        ImVec2(io.DisplaySize.x * 0.5f - 200.0f * current_s(), 110.0f * current_s()),
                        ImVec2(400.0f * current_s(), 360.0f * current_s()))) {
        ImGui::End();
        return;
    }
    if (g_snapshots_stale || SDL_GetTicks() - g_snapshots_at > 2000) {
        g_snapshots = organize::snapshots();
        g_snapshots_at = SDL_GetTicks();
        g_snapshots_stale = false;
    }
    const std::string active = organize::active_snapshot();
    gui::text_disabled("Named versions of the layout");
    gui::text("Active: %s", active.empty() ? "(none)" : active.c_str());
    ImGui::Separator();
    ImGui::SetNextItemWidth(200.0f * current_s());
    const bool enter = ImGui::InputTextWithHint("##name", "Name", g_snapshot_name.data(), sizeof(g_snapshot_name),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Save Snapshot") || enter) && core::at(g_snapshot_name, 0)) {
        const std::string name = g_snapshot_name.data();
        get_queue([name] {
            std::string message;
            organize::save_snapshot(name, &message);
            show_message(message, 5.0);
        });
        core::at(g_snapshot_name, 0) = 0;
        g_snapshots_stale = true;
    }
    ImGui::BeginChild("##snapshots");
    if (g_snapshots.empty())
        gui::text_disabled("No snapshots");
    for (const std::string& name : g_snapshots) {
        ImGui::PushID(name.c_str());
        const bool is_active = name == active;
        if (is_active)
            gui::text_colored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "%s  (active)", name.c_str());
        else
            ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine(get_right_edge() - ImGui::CalcTextSize("Switch to  Delete").x - 30.0f * current_s());
        if (ImGui::SmallButton(is_active ? "Reload" : "Switch to")) {
            get_queue([name] {
                std::string message;
                organize::switch_snapshot(name, &message);
                show_message(message, 6.0);
            });
            g_snapshots_stale = true;
        }
        if (ImGui::IsItemHovered())
            gui::tooltip("Load this snapshot");
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete") && ImGui::GetIO().KeyShift) {
            get_queue([name] {
                std::string message;
                organize::delete_snapshot(name, &message);
                show_message(message);
            });
            g_snapshots_stale = true;
        }
        if (ImGui::IsItemHovered())
            gui::tooltip("Shift+click to delete");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- the menus and name windows ---

std::array<char, 48> g_group_name;
std::array<char, 48> g_label_text;
uint32_t g_label_for;
bool g_label_asked, g_group_asked;
int g_rename_group = -1;
std::array<char, 48> g_rename_text;
bool g_rename_asked;

void get_menus()
{
    if (take_similar_menu())
        ImGui::OpenPopup("Select similar###ux3similar");
    if (ImGui::BeginPopup("Select similar###ux3similar")) {
        current_heading("Select Similar");
        ImGui::Separator();
        struct Item {
            const char* name;
            organize::Similar by;
            const char* tip;
        };
        static const auto kItems = std::to_array<Item>({{.name = "Same mesh", .by = organize::Similar::kMesh, .tip = "Same model or scenery piece"},
                                      {.name = "Same class", .by = organize::Similar::kClass, .tip = "Same node class"},
                                      {.name = "Same family", .by = organize::Similar::kFamily, .tip = "Same Outliner family"},
                                      {.name = "Same type", .by = organize::Similar::kKindTag, .tip = "Same node type"}});
        for (const Item& item : kItems) {
            const organize::Similar by = item.by;
            if (ImGui::MenuItem(item.name))
                get_queue([by] { organize::select_similar(by); });
            if (ImGui::IsItemHovered())
                gui::tooltip("%s", item.tip);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Same Family (All)"))
            get_queue([] { organize::select_family(); });
        if (ImGui::MenuItem("Invert Selection", actions::get_keys_text(actions::kInvertSelection).c_str()))
            get_queue([] { organize::invert_selection(); });
        if (ImGui::MenuItem("Select all", actions::get_keys_text(actions::kSelectAll).c_str()))
            get_queue([] { organize::select_all(); });
        ImGui::EndPopup();
    }
    if (take_group_name() || g_group_asked) {
        g_group_asked = false;
        text::format_to(g_group_name, "Group");
        ImGui::OpenPopup("Group the selection###ux3group");
    }
    if (ImGui::BeginPopupModal("Group the selection###ux3group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Group name:");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool enter =
            ImGui::InputText("##group", g_group_name.data(), sizeof(g_group_name), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Make group") || enter) {
            const std::string name = g_group_name.data();
            get_queue([name] { organize::make_group(name); });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g_label_asked) {
        g_label_asked = false;
        ImGui::OpenPopup("Label###ux3label");
    }
    if (ImGui::BeginPopupModal("Label###ux3label", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Label (empty clears):");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool enter =
            ImGui::InputText("##label", g_label_text.data(), sizeof(g_label_text), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Set") || enter) {
            const std::string text = g_label_text.data();
            const uint32_t id = g_label_for;
            get_queue([id, text] {
                organize::State next = organize::at_hand();
                std::string clean;
                organize::set_label(&next, id, organize::clean_name(text, &clean) ? clean : "");
                organize::get_change(next, "Label");
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g_rename_asked) {
        g_rename_asked = false;
        ImGui::OpenPopup("Rename group###ux3rename");
    }
    if (ImGui::BeginPopupModal("Rename group###ux3rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool enter =
            ImGui::InputText("##rename", g_rename_text.data(), sizeof(g_rename_text), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Rename") || enter) {
            const std::string text = g_rename_text.data();
            const int index = g_rename_group;
            get_queue([index, text] { organize::rename_group(index, text); });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// --- the Inspector's folding, remembered ---

// The Inspector's foldable sections (their ### ids - the windows' root, where they're drawn).
constexpr auto kSections = std::to_array<const char*>({"###fields",    "###levelprops", "###camera",  "###cameras", "###clusters",
                                 "###faceflags", "###locks",      "###ux3clip", "###ux3multi"});
constexpr int kSectionCount = sizeof(kSections) / sizeof(core::at(kSections, 0));
std::array<int, kSectionCount> g_section_state;
bool g_sections_loaded;
ImGuiID g_sections_window;

// --- the Inspector's shared fields ---

struct SharedEdit {
    uint32_t key = 0;
    std::array<char, 160> text = {""};
};
std::vector<SharedEdit> g_shared;
ImGuiID g_shared_active;

} // namespace

// ---------------------------------------------------------------------------------------------------------------------

void set_fonts(const Fonts& fonts) { g_fonts = fonts; }

void get_windows(const ui::ShapeMesh& model)
{
    (void)model;
    get_palette();
    history_window();
    changes_window();
    keys_window();
    snapshots_window();
    get_menus();
}

void help_from_registry()
{
    const std::vector<actions::ActStep>& all = actions::get_all();
    std::vector<std::string> categories;
    for (const actions::ActStep& a : all) {
        if (std::ranges::find(categories, a.category) == categories.end())
            categories.push_back(a.category);
    }
    gui::text_disabled("All commands and their keys");
    if (ImGui::SmallButton("Keys..."))
        unseal(kKeysWindow) = true;
    if (!ImGui::BeginTable("##registry", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
        return;
    ImGui::TableSetupColumn("##category", ImGuiTableColumnFlags_WidthFixed, 90.0f * current_s());
    ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthFixed, 190.0f * current_s());
    ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthStretch);
    for (const std::string& category : categories) {
        bool first = true;
        for (int i = 0; std::cmp_less(i, all.size()); ++i) {
            const actions::ActStep& a = core::at(all, i);
            if (a.category != category)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (first)
                current_heading(category.c_str());
            first = false;
            ImGui::TableNextColumn();
            if (g_fonts.mono)
                ImGui::PushFont(g_fonts.mono);
            ImGui::PushTextWrapPos(0.0f);
            const std::string keys = actions::get_keys_text(i);
            gui::text_colored(kKeyColour, "%s", keys.empty() ? "-" : keys.c_str());
            ImGui::PopTextWrapPos();
            if (g_fonts.mono)
                ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(a.name.c_str());
            if (ImGui::IsItemHovered() && !a.tip.empty())
                gui::tooltip("%s", a.tip.c_str());
        }
    }
    ImGui::EndTable();
}

void keys_section()
{
    const std::vector<actions::ActStep>& all = actions::get_all();
    gui::text_disabled("Click a key to rebind, + to add, right-click to remove");
    ImGui::SetNextItemWidth(220.0f * current_s());
    ImGui::InputTextWithHint("##keysfilter", "Filter\xE2\x80\xA6", g_keys_filter.data(), sizeof(g_keys_filter));
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset all") && ImGui::GetIO().KeyShift) {
        for (int i = 0; std::cmp_less(i, all.size()); ++i)
            actions::reset_keys(i);
    }
    if (ImGui::IsItemHovered())
        gui::tooltip("Shift+click to reset all keys");
    if (actions::get_capturing()) {
        const actions::ActStep* a = actions::get_pull(actions::capturing_index());
        gui::text_colored(kKeyColour, "Press a key for \"%s\" (Esc: cancel)", a ? a->name.c_str() : "?");
    }
    ImGui::Separator();
    ImGui::BeginChild("##keys");
    if (ImGui::BeginTable("##keytable", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed, 250.0f * current_s());
        ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, 60.0f * current_s());
        std::string category;
        for (int i = 0; std::cmp_less(i, all.size()); ++i) {
            const actions::ActStep& a = core::at(all, i);
            if (!a.rebindable ||
                actions::fuzzy_score(g_keys_filter.data(), a.category + " " + a.name + " " + actions::get_keys_text(i)) == 0)
                continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (a.category != category) {
                category = a.category;
                gui::text_colored(kMuted, "%s", category.c_str());
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(a.name.c_str());
            if (ImGui::IsItemHovered() && !a.tip.empty())
                gui::tooltip("%s", a.tip.c_str());
            ImGui::TableNextColumn();
            if (g_fonts.mono)
                ImGui::PushFont(g_fonts.mono);
            for (int k = 0; std::cmp_less(k, a.keys.size()); ++k) {
                ImGui::PushID(k);
                const bool capturing = actions::get_capturing() && actions::capturing_index() == i;
                const std::string text = capturing ? "..." : actions::format_chord(core::at(a.keys, k));
                if (ImGui::SmallButton(text.c_str()))
                    actions::begin_capture(i, k);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    std::vector<actions::Chord> next = a.keys;
                    next.erase(next.begin() + k);
                    actions::set_keys(i, next);
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+"))
                actions::begin_capture(i, static_cast<int>(a.keys.size()));
            if (g_fonts.mono)
                ImGui::PopFont();
            std::vector<int> clashes = actions::conflicts(i);
            if (!clashes.empty()) {
                ImGui::SameLine();
                gui::text_colored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "also: %s%s", core::at(all, clashes.front()).name.c_str(),
                                   clashes.size() > 1 ? " ..." : "");
            }
            ImGui::TableNextColumn();
            const bool as_default = a.keys == a.defaults;
            ImGui::BeginDisabled(as_default);
            if (ImGui::SmallButton("Reset"))
                actions::reset_keys(i);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                gui::tooltip("Default: %s", actions::format_keys(a.defaults).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

bool row_passes(const nodes::Elem& node, const std::string& name, const char* search)
{
    static std::string last;
    static organize::Filter filter;
    if (last != search) {
        last = search;
        filter = organize::parse_filter(last);
    }
    if (!*search)
        return true;
    const std::string label = filter.labeled || !filter.text.empty() ? row_label(node.name) : "";
    const bool in_group = !filter.group.empty() && organize::in_group(filter.group, node);
    const bool locked = filter.locked && organize::latched(node.name);
    const bool hidden = filter.hidden && organize::hidden_in_editor(node);
    return organize::row_passes(filter, node.family, name, label, in_group, locked, hidden);
}

std::string row_label(uint32_t name)
{
    const organize::State& state = organize::at_hand();
    return state.labels.empty() ? "" : organize::get_label_of(state, name);
}

void bunches()
{
    const organize::State& state = organize::at_hand();
    if (state.groups.empty())
        return;
    std::array<char, 48> header{};
    text::format_to(header, "Groups###ux3groups");
    const bool open = ImGui::TreeNodeEx(header.data(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    gui::text_disabled("%d", static_cast<int>(state.groups.size()));
    if (!open)
        return;
    for (int i = 0; std::cmp_less(i, state.groups.size()); ++i) {
        const organize::GroupSet& g = core::at(state.groups, i);
        ImGui::PushID(i);
        std::array<char, 96> text{};
        text::format_to(text, "%s  (%d)", g.name.c_str(), static_cast<int>(g.members.size()));
        if (ImGui::Selectable(text.data()))
            get_queue([i] { organize::select_group(i); });
        if (ImGui::IsItemHovered())
            gui::tooltip("Click to select; right-click for options");
        if (ImGui::BeginPopupContextItem("##groupmenu")) {
            if (ImGui::MenuItem("Add Selection"))
                get_queue([i] { organize::add_to_group(i); });
            if (ImGui::MenuItem("Rename...")) {
                g_rename_group = i;
                text::format_to(g_rename_text, "%s", g.name.c_str());
                g_rename_asked = true;
            }
            if (ImGui::MenuItem("Delete Group"))
                get_queue([i] { organize::delete_group(i); });
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

float toggles_width()
{
    const float glyph = ImGui::CalcTextSize("W").x;
    return 3.0f * (glyph + 6.0f * current_s()) + 6.0f * current_s();
}

void row_toggles(const nodes::Elem& node, float x)
{
    const bool hidden = organize::hidden_in_editor(node);
    const bool locked = organize::latched(node.name);
    const bool selectable = organize::get_selectable(node);
    struct FlipOver {
        const char* on;
        const char* off;
        bool state;
        const char* tip;
    };
    const std::array<FlipOver, 3> toggles = {{
        {.on = "\xE2\x80\xA2", .off = "-", .state = !hidden, .tip = "Visible in editor"},
        {.on = "L", .off = "L", .state = locked, .tip = "Locked against changes"},
        {.on = "S", .off = "S", .state = selectable, .tip = "Selectable in the view"},
    }};
    const float step = ImGui::CalcTextSize("W").x + 6.0f * current_s();
    for (int t = 0; t < 3; ++t) {
        ImGui::SameLine(x + 6.0f * current_s() + step * static_cast<float>(t));
        const FlipOver& toggle = core::at(toggles, t);
        // (lit when on: the lock orange; the eye and selectable light; off: dim)
        ImGui::PushID(t);
        ImGui::PushStyleColor(
            ImGuiCol_Text, toggle.state ? (t == 1 ? ImVec4(1.0f, 0.6f, 0.3f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f))
                                        : ImVec4(0.38f, 0.39f, 0.41f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * current_s(), 0.0f));
        const bool clicked = ImGui::SmallButton(toggle.state ? toggle.on : toggle.off);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            gui::tooltip("%s", toggle.tip);
        if (clicked) {
            const nodes::Elem& copy = node;
            const bool value = !toggle.state;
            if (t == 0)
                get_queue([copy, value] { organize::set_hidden_in_editor(copy, !value); });
            else if (t == 1)
                get_queue([copy, value] { organize::set_locked(copy.name, value); });
            else
                get_queue([copy, value] { organize::set_selectable(copy, value); });
        }
        ImGui::PopID();
    }
}

void row_menu(const nodes::Elem& node)
{
    if (!ImGui::BeginPopupContextItem("##rowmenu"))
        return;
    current_heading(nodes::caption(node.name).c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Label...")) {
        g_label_for = node.name;
        text::format_to(g_label_text, "%s", row_label(node.name).c_str());
        g_label_asked = true;
    }
    const nodes::Elem& copy = node;
    const bool hidden = organize::hidden_in_editor(node);
    const bool locked = organize::latched(node.name);
    const bool selectable = organize::get_selectable(node);
    if (ImGui::MenuItem("Hidden in Editor", nullptr, hidden))
        get_queue([copy, hidden] { organize::set_hidden_in_editor(copy, !hidden); });
    if (ImGui::MenuItem("Locked", nullptr, locked))
        get_queue([copy, locked] { organize::set_locked(copy.name, !locked); });
    if (ImGui::MenuItem("Selectable", nullptr, selectable))
        get_queue([copy, selectable] { organize::set_selectable(copy, !selectable); });
    ImGui::Separator();
    if (ImGui::MenuItem("Group Selection...", actions::get_keys_text(actions::kMakeGroup).c_str()))
        g_group_asked = true;
    const organize::State& state = organize::at_hand();
    if (!state.groups.empty() && ImGui::BeginMenu("Add Selection to")) {
        for (int i = 0; std::cmp_less(i, state.groups.size()); ++i) {
            if (ImGui::MenuItem(core::at(state.groups, i).name.c_str()))
                get_queue([i] { organize::add_to_group(i); });
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Select Similar...", actions::get_keys_text(actions::kSelectSimilar).c_str()))
        get_queue([] { actions::fire_off(actions::kSelectSimilar); });
    ImGui::EndPopup();
}

std::string take_outliner_search() { return organize::take_outliner_search(); }

void inspector_section(const ui::ShapeMesh& model)
{
    (void)model;
    const SelectionIds s = current_selection();
    const bool any = !s.objects.empty() || !s.sectors.empty() || s.node;
    ImGui::Spacing();
    // The clipboard (Ctrl+Shift+C / Ctrl+Shift+V).
    if (ImGui::CollapsingHeader("Copy / paste###ux3clip")) {
        ImGui::BeginDisabled(!any);
        if (ImGui::SmallButton("Copy Transform and Fields"))
            get_queue([] {
                std::string m;
                clip::copy_props(&m);
                show_message(m);
            });
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            gui::tooltip("%s", actions::get_keys_text(actions::kCopyProps).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(!any || !clip::have_transform());
        if (ImGui::SmallButton("Paste Transform"))
            get_queue([] {
                std::string m;
                clip::paste_transform(&m);
                show_message(m);
            });
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            gui::tooltip("%s", actions::get_keys_text(actions::kPasteTransform).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(!any || clip::field_count() == 0);
        if (ImGui::SmallButton("Paste fields"))
            get_queue([] {
                std::string m;
                clip::paste_fields(&m);
                show_message(m);
            });
        ImGui::EndDisabled();
        gui::text_disabled("Copied: %s", clip::describe().c_str());
    }
    // Several selected: their shared fields.
    std::vector<uint32_t> nodes;
    for (const uint32_t id : s.objects.empty() ? s.sectors : s.objects) {
        if (nodes::get_find(id) >= 0 && nodes.size() < 64)
            nodes.push_back(id);
    }
    if (nodes.size() < 2)
        return;
    std::array<char, 64> header{};
    text::format_to(header, "Shared fields (%d nodes)###ux3multi", static_cast<int>(nodes.size()));
    if (!ImGui::CollapsingHeader(header.data()))
        return;
    // (the fields every selected node has and can edit; a value where they agree, "mixed" where not)
    std::vector<std::vector<fields::Row>> rows;
    rows.reserve(nodes.size());
    for (const uint32_t id : nodes)
        rows.push_back(fields::rows_of(core::at(nodes::get_all(), nodes::get_find(id))));
    struct Shared {
        uint32_t key;
        fields::Kind kind;
        std::string text;
        bool mixed;
    };
    std::vector<Shared> shared;
    for (const fields::Row& row : rows.front()) {
        if (!row.editable || row.key == fields::key_pos() || row.key == fields::key_angles() ||
            row.key == fields::key_links())
            continue;
        const std::string text = fields::get_format(row.value, fields::get_name_of);
        bool all = true, mixed = false;
        for (size_t n = 1; n < rows.size() && all; ++n) {
            auto it =
                std::find_if(core::at(rows, n).begin(), core::at(rows, n).end(), [&](const fields::Row& r) { return r.key == row.key; });
            all = it != core::at(rows, n).end() && it->editable && it->value.kind == row.value.kind;
            mixed = mixed || (all && !fields::same_value(it->value, row.value));
        }
        if (all)
            shared.push_back({.key = row.key, .kind = row.value.kind, .text = text, .mixed = mixed});
    }
    if (shared.empty()) {
        gui::text_disabled("No shared editable fields");
        return;
    }
    gui::text_disabled("Applies to all %d", static_cast<int>(nodes.size()));
    if (!ImGui::BeginTable("##shared", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed, 120.0f * current_s());
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
    if (g_shared.size() != shared.size())
        g_shared.assign(shared.size(), SharedEdit{});
    for (size_t i = 0; i < shared.size(); ++i) {
        const Shared& f = core::at(shared, i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        gui::text_disabled("%s", fields::get_name_text(f.key, fields::get_name_of).c_str());
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(f.key));
        SharedEdit& edit = core::at(g_shared, i);
        const ImGuiID id = ImGui::GetID("##value");
        // (the value shown afresh unless it's being typed in)
        if (g_shared_active != id || edit.key != f.key) {
            edit.key = f.key;
            text::format_to(edit.text, "%s", f.mixed ? "" : f.text.c_str());
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (g_fonts.mono)
            ImGui::PushFont(g_fonts.mono);
        const bool enter = ImGui::InputTextWithHint("##value", f.mixed ? "mixed" : "", edit.text.data(), sizeof(edit.text),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (g_fonts.mono)
            ImGui::PopFont();
        if (ImGui::IsItemActive())
            g_shared_active = id;
        else if (g_shared_active == id)
            g_shared_active = 0;
        if (ImGui::IsItemHovered())
            gui::tooltip("%s - %s", fields::kind_name(f.kind), f.mixed ? "mixed values" : "same on all");
        if (enter) {
            FieldSet set;
            set.key = f.key;
            set.kind = static_cast<int>(f.kind);
            set.text = edit.text.data();
            const std::vector<uint32_t>& targets = nodes;
            get_queue(([targets, set] {
                std::string message;
                const int n = set_fields_on_nodes(targets, {set}, "Field (all selected)", &message);
                show_message(n ? "Set on " + std::to_string(n) + " nodes" +
                                     (message.empty() ? "" : " (" + message + ")")
                               : "Not set: " + message);
            }));
            g_shared_active = 0;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void remember_sections()
{
    ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
    if (!window)
        return;
    const std::string ini = game_folder_path("thug2-decomp.ini");
    if (!g_sections_loaded || g_sections_window != window->ID) {
        g_sections_loaded = true;
        g_sections_window = window->ID;
        for (int i = 0; i < kSectionCount; ++i) {
            const int value =
                static_cast<int>(GetPrivateProfileIntA("editor_sections", std::next(core::at(kSections, i), 3), -1, ini.c_str()));
            core::at(g_section_state, i) = value;
            if (value == 0 || value == 1)
                window->StateStorage.SetInt(window->GetID(core::at(kSections, i)), value);
        }
        return;
    }
    for (int i = 0; i < kSectionCount; ++i) {
        const int value = window->StateStorage.GetInt(window->GetID(core::at(kSections, i)), -1);
        if (value < 0 || value == core::at(g_section_state, i))
            continue;
        core::at(g_section_state, i) = value;
        WritePrivateProfileStringA("editor_sections", std::next(core::at(kSections, i), 3), value ? "1" : "0", ini.c_str());
    }
}

} // namespace frontend::level_editor::ux3
