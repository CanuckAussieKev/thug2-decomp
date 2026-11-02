// NOT ORIGINAL - the level editor's history, changes, search and organising: the game side (ours). See
// level_editor_ux3.h.

#include "editor/level_editor_ux3.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <utility>
#include <array>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_clip.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_ui.h"
#include "platform/run_log.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::ux3 {

namespace {

std::vector<std::function<void()>> g_tasks;
std::array<bool, kWindowCount> g_open;
bool g_similar_menu, g_group_name;
Settings g_settings;

std::vector<ChangeRow> g_changes;
uint32_t g_changes_at;
size_t g_history_key = static_cast<size_t>(-1);
bool g_refresh_asked = true;

void get_say(const std::string& message) { show_message(message, 5.0); }

// A setting of the toolbar's in the registry (the search shows and switches it).
void add_toggle(const char* id, const char* name, const char* tip, std::function<int()> checked,
                std::function<void()> run)
{
    actions::ActStep a;
    a.id = id;
    a.name = name;
    a.category = "Settings";
    a.tip = tip;
    a.kind = actions::Kind::kFlipOver;
    a.checked = std::move(checked);
    a.run = std::move(run);
    actions::enrol(a);
}

void get_ask(ui::Request::Kind kind, int value = 0, uint32_t id = 0)
{
    ui::Request request;
    request.kind = kind;
    request.value = value;
    request.id = id;
    ui::push_request(request);
}

} // namespace

void get_queue(std::function<void()> task) { g_tasks.push_back(std::move(task)); }

bool& unseal(Pane window) { return core::at(g_open, window); }

bool take_similar_menu()
{
    const bool asked = g_similar_menu;
    g_similar_menu = false;
    return asked;
}

bool take_group_name()
{
    const bool asked = g_group_name;
    g_group_name = false;
    return asked;
}

void note_settings(const Settings& settings) { g_settings = settings; }
const Settings& settings_now() { return g_settings; }

void prepare_all()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    organize::prepare_all();
    using namespace actions;
    set_enabled(kUndo, [] { return settings_now().can_undo_; });
    set_enabled(kRedo, [] { return settings_now().can_redo_; });
    set_enabled(kPasteTransform, [] { return clip::have_transform(); });
    set_enabled(kPasteFields, [] { return clip::field_count() > 0; });
    // (the windows: their own flags, switched at once - nothing of the level changes)
    set_run(kPalette, [] { core::at(g_open, kPaletteWindow) = !core::at(g_open, kPaletteWindow); });
    set_run(kHistory, [] { core::at(g_open, kHistoryWindow) = !core::at(g_open, kHistoryWindow); });
    set_run(kChanges, [] {
        core::at(g_open, kChangesWindow) = !core::at(g_open, kChangesWindow);
        g_refresh_asked = true;
    });
    set_run(kKeys, [] { core::at(g_open, kKeysWindow) = !core::at(g_open, kKeysWindow); });
    set_run(kSnapshots, [] { core::at(g_open, kSnapshotsWindow) = !core::at(g_open, kSnapshotsWindow); });
    set_checked(kPalette, [] { return core::at(g_open, kPaletteWindow) ? 1 : 0; });
    set_checked(kHistory, [] { return core::at(g_open, kHistoryWindow) ? 1 : 0; });
    set_checked(kChanges, [] { return core::at(g_open, kChangesWindow) ? 1 : 0; });
    set_checked(kKeys, [] { return core::at(g_open, kKeysWindow) ? 1 : 0; });
    set_checked(kSnapshots, [] { return core::at(g_open, kSnapshotsWindow) ? 1 : 0; });
    set_checked(kCollision, [] { return settings_now().collision ? 1 : 0; });
    add_toggle(
        "snap25", "Snap", "Snap moves to the grid", [] { return settings_now().snap_always_ ? 1 : 0; },
        [] { get_ask(ui::Request::Kind::kSnapAlways, settings_now().snap_always_ ? 0 : 1); });
    add_toggle(
        "carry_objects", "Carry Objects", "Move objects standing on scenery with it",
        [] { return settings_now().carry ? 1 : 0; },
        [] { get_ask(ui::Request::Kind::kCarry, settings_now().carry ? 0 : 1); });
    struct PickToggle {
        const char *id, *name;
        uint32_t bit;
    };
    static const auto kPicks = std::to_array<PickToggle>({{.id = "pick_nodes", .name = "Pick Nodes", .bit = picking::kNodes},
                                        {.id = "pick_objects", .name = "Pick Objects", .bit = picking::kObjects},
                                        {.id = "pick_scenery", .name = "Pick Scenery", .bit = picking::kScenery}});
    // (UX4: the Overlays menu's switches; UX5: a note where the view looks, the budget meter)
    for (const overlays::BitInfo& info : overlays::get_bits()) {
        const uint32_t bit = info.bit;
        const std::string id = "overlay_" + std::to_string(bit);
        add_toggle(
            id.c_str(), (std::string("Overlay ") + info.label).c_str(), info.tip,
            [bit] { return overlays::get_shown(bit) ? 1 : 0; },
            [bit] { overlays::assign_value(bit, !overlays::get_shown(bit)); });
    }
    {
        actions::ActStep note;
        note.id = "note_here";
        note.name = "Add Note";
        note.category = "Edit";
        note.tip = "Add a note at the view center (editor only)";
        note.run = [] { notes::add_here_queued(); };
        actions::enrol(note);
        actions::ActStep budget;
        budget.id = "hint_budget";
        budget.name = "Budget";
        budget.category = "Panels";
        budget.kind = actions::Kind::kDetailsOf;
        budget.rebindable = false;
        budget.keys_text_ = "Tool strip meter";
        budget.tip = "Level usage against engine limits";
        actions::enrol(budget);
    }
    for (const PickToggle& p : kPicks) {
        const uint32_t bit = p.bit;
        add_toggle(
            p.id, p.name, "What a click in the view selects",
            [bit] { return (settings_now().pick_filter_ & bit) ? 1 : 0; },
            [bit] { get_ask(ui::Request::Kind::kPickFilter, 0, settings_now().pick_filter_ ^ bit); });
    }
}

void tick_frame()
{
    using namespace actions;
    // (the panels' clicks: done now, in the editor's update)
    std::vector<std::function<void()>> tasks;
    tasks.swap(g_tasks);
    for (const std::function<void()>& task : tasks)
        task();
    const bool busy = change_under_way();
    // This module's own keys (the others are the editor's: level_editor.cpp reads them).
    for (const int window : {kPalette, kHistory, kChanges, kKeys, kSnapshots}) {
        if (squeezed(window))
            get_pull(window)->run();
    }
    std::string message;
    if (squeezed(kCopy)) {
        clip::dup_from(&message);
        get_say(message);
    }
    if (squeezed(kPaste) && !busy) {
        clip::graft(&message);
        get_say(message);
    }
    if (squeezed(kCopyProps)) {
        clip::copy_props(&message);
        get_say(message);
    }
    if (squeezed(kPasteTransform) && !busy) {
        clip::paste_transform(&message);
        get_say(message);
    }
    if (squeezed(kPasteFields) && !busy) {
        clip::paste_fields(&message);
        get_say(message);
    }
    if (squeezed(kBoxSelect) && !busy)
        organize::arm_box();
    if (squeezed(kSelectSimilar) && !busy)
        g_similar_menu = true;
    if (squeezed(kSelectFamily) && !busy)
        organize::select_family();
    if (squeezed(kInvertSelection) && !busy)
        organize::invert_selection();
    if (squeezed(kSelectAll) && !busy)
        organize::select_all();
    if (squeezed(kMakeGroup) && !busy)
        g_group_name = true;
    // The Changes list: refreshed when the history moved (a change made, undone or redone), when asked, or a second on
    // (other changes - a save, an autosave restored - don't move the history).
    const size_t key = undo_history().size() * 1000003u + redo_history().size();
    const uint32_t now = SDL_GetTicks();
    if (key != g_history_key || g_refresh_asked || now - g_changes_at > 1000) {
        g_history_key = key;
        g_refresh_asked = false;
        g_changes_at = now;
        refresh_changes();
    }
}

const std::vector<ChangeRow>& change_list() { return g_changes; }

void refresh_changes()
{
    g_changes.clear();
    std::string text;
    if (!layout::capture_text(&text))
        return;
    std::vector<std::string> lines;
    for (size_t start = 0; start < text.size();) {
        const size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    auto label = [](uint32_t checksum) {
        const char* name = object_edit::get_name_of(checksum);
        return std::string(name ? name : "");
    };
    for (const changes::TableRow& e : changes::link_chain(text, label))
        g_changes.push_back({.entry = e, .record = e.line >= 0 && std::cmp_less(e.line, lines.size()) ? core::at(lines, e.line) : ""});
}

void go_to(const ChangeRow& row)
{
    const uint32_t id = row.entry.id;
    get_queue([id] {
        if (!id || !go_to_thing(id))
            get_say("Item not found in the level");
    });
}

void revert(const ChangeRow& row)
{
    const std::string record = row.record;
    const std::string summary = row.entry.summary;
    get_queue([record, summary] {
        std::string text, message;
        if (!layout::capture_text(&text)) {
            get_say("No level loaded");
            return;
        }
        // (the record as it is now: the line the list showed, found again)
        int line = -1, n = 0;
        for (size_t start = 0; start <= text.size() && line < 0; ++n) {
            const size_t end = text.find('\n', start);
            const std::string one = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (one == record)
                line = n;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        if (line < 0) {
            get_say("List refreshed");
            g_refresh_asked = true;
            return;
        }
        const std::string label = "Revert: " + summary.substr(0, 48);
        if (apply_layout_step(changes::without(text, line), label.c_str(), &message))
            get_say("Reverted: " + summary);
        else
            get_say("Couldn't revert: " + message);
        g_refresh_asked = true;
    });
}

} // namespace frontend::level_editor::ux3
