// NOT ORIGINAL - the level editor's history, changes, search and organising: the game side (ours). See
// level_editor_ux3.h.

#include "editor/level_editor_ux3.h"

#include <SDL2/SDL.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_changes.h"
#include "editor/level_editor_clip.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_ui.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::ux3 {

namespace {

constexpr double kSayDefaultSeconds = 5.0; // how long a message stays
constexpr uint32_t kHistoryRefreshDelay = 1000; // milliseconds after the last change before the history refreshes

// NOT ORIGINAL grouping - level_editor_ux3.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_ux3_state()).
struct LevelEditorUx3State {
    std::vector<std::function<void()>> tasks_;
    std::array<bool, kWindowCount> open_{};
    bool similar_menu_{};
    bool group_name_{};
    Settings settings_{};
    std::vector<ChangeRow> changes_;
    uint32_t changes_at_{};
    size_t history_key_ = static_cast<size_t>(-1);
    bool refresh_asked_ = true;
};

LevelEditorUx3State& level_editor_ux3_state()
{
    static core::NoDestroy<LevelEditorUx3State> state;
    return state.get();
}

void get_say(const std::string& message) { show_message(message, kSayDefaultSeconds); }

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

// A request's value and the id it is for.
struct AskValue {
    int value = 0;
    uint32_t id = 0;
};

void get_ask(ui::Request::Kind kind, AskValue ask_value)
{
    const auto value = ask_value.value;
    const auto id = ask_value.id;
    ui::Request request;
    request.kind = kind;
    request.value = value;
    request.id = id;
    ui::push_request(request);
}

} // namespace

void get_queue(std::function<void()> task) { level_editor_ux3_state().tasks_.push_back(std::move(task)); }

bool& unseal(Pane window) { return core::at(level_editor_ux3_state().open_, window); }

bool take_similar_menu()
{
    const bool asked = level_editor_ux3_state().similar_menu_;
    level_editor_ux3_state().similar_menu_ = false;
    return asked;
}

bool take_group_name()
{
    const bool asked = level_editor_ux3_state().group_name_;
    level_editor_ux3_state().group_name_ = false;
    return asked;
}

void note_settings(const Settings& settings) { level_editor_ux3_state().settings_ = settings; }
const Settings& settings_now() { return level_editor_ux3_state().settings_; }

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
    set_run(kPalette, [] { core::at(level_editor_ux3_state().open_, kPaletteWindow) = !core::at(level_editor_ux3_state().open_, kPaletteWindow); });
    set_run(kHistory, [] { core::at(level_editor_ux3_state().open_, kHistoryWindow) = !core::at(level_editor_ux3_state().open_, kHistoryWindow); });
    set_run(kChanges, [] {
        core::at(level_editor_ux3_state().open_, kChangesWindow) = !core::at(level_editor_ux3_state().open_, kChangesWindow);
        level_editor_ux3_state().refresh_asked_ = true;
    });
    set_run(kKeys, [] { core::at(level_editor_ux3_state().open_, kKeysWindow) = !core::at(level_editor_ux3_state().open_, kKeysWindow); });
    set_run(kSnapshots, [] { core::at(level_editor_ux3_state().open_, kSnapshotsWindow) = !core::at(level_editor_ux3_state().open_, kSnapshotsWindow); });
    set_checked(kPalette, [] { return core::at(level_editor_ux3_state().open_, kPaletteWindow) ? 1 : 0; });
    set_checked(kHistory, [] { return core::at(level_editor_ux3_state().open_, kHistoryWindow) ? 1 : 0; });
    set_checked(kChanges, [] { return core::at(level_editor_ux3_state().open_, kChangesWindow) ? 1 : 0; });
    set_checked(kKeys, [] { return core::at(level_editor_ux3_state().open_, kKeysWindow) ? 1 : 0; });
    set_checked(kSnapshots, [] { return core::at(level_editor_ux3_state().open_, kSnapshotsWindow) ? 1 : 0; });
    set_checked(kCollision, [] { return settings_now().collision ? 1 : 0; });
    add_toggle(
        "snap25", "Snap", "Snap moves to the grid", [] { return settings_now().snap_always_ ? 1 : 0; },
        [] { get_ask(ui::Request::Kind::kSnapAlways, {.value = settings_now().snap_always_ ? 0 : 1}); });
    add_toggle(
        "carry_objects", "Carry Objects", "Move objects standing on scenery with it",
        [] { return settings_now().carry ? 1 : 0; },
        [] { get_ask(ui::Request::Kind::kCarry, {.value = settings_now().carry ? 0 : 1}); });
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
            [bit] { get_ask(ui::Request::Kind::kPickFilter, {.value = 0, .id = settings_now().pick_filter_ ^ bit}); });
    }
}

void tick_frame()
{
    using namespace actions;
    // (the panels' clicks: done now, in the editor's update)
    std::vector<std::function<void()>> tasks;
    tasks.swap(level_editor_ux3_state().tasks_);
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
        level_editor_ux3_state().similar_menu_ = true;
    if (squeezed(kSelectFamily) && !busy)
        organize::select_family();
    if (squeezed(kInvertSelection) && !busy)
        organize::invert_selection();
    if (squeezed(kSelectAll) && !busy)
        organize::select_all();
    if (squeezed(kMakeGroup) && !busy)
        level_editor_ux3_state().group_name_ = true;
    // The Changes list: refreshed when the history moved (a change made, undone or redone), when asked, or a second on
    // (other changes - a save, an autosave restored - don't move the history).
    const size_t key = undo_history().size() * 1000003u + redo_history().size();
    const uint32_t now = SDL_GetTicks();
    if (key != level_editor_ux3_state().history_key_ || level_editor_ux3_state().refresh_asked_ || now - level_editor_ux3_state().changes_at_ > kHistoryRefreshDelay) {
        level_editor_ux3_state().history_key_ = key;
        level_editor_ux3_state().refresh_asked_ = false;
        level_editor_ux3_state().changes_at_ = now;
        refresh_changes();
    }
}

const std::vector<ChangeRow>& change_list() { return level_editor_ux3_state().changes_; }

void refresh_changes()
{
    level_editor_ux3_state().changes_.clear();
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
        level_editor_ux3_state().changes_.push_back({.entry = e, .record = e.line >= 0 && std::cmp_less(e.line, lines.size()) ? core::at(lines, e.line) : ""});
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
        std::string text;
        std::string message;
        if (!layout::capture_text(&text)) {
            get_say("No level loaded");
            return;
        }
        // (the record as it is now: the line the list showed, found again)
        int line = -1;
        int n = 0;
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
            level_editor_ux3_state().refresh_asked_ = true;
            return;
        }
        const std::string label = "Revert: " + summary.substr(0, 48);
        if (apply_layout_step(changes::without(text, line), label.c_str(), &message))
            get_say("Reverted: " + summary);
        else
            get_say("Couldn't revert: " + message);
        level_editor_ux3_state().refresh_asked_ = true;
    });
}

} // namespace frontend::level_editor::ux3
