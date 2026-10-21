// NOT ORIGINAL - the level editor's history, changes, search and organising: the game side (ours). See
// level_editor_ux3.h.

#include "game/front/level_editor_ux3.h"

#include <SDL2/SDL.h>

#include <algorithm>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_clip.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_notes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_organize.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_ui.h"
#include "hook/log.h"

namespace Front::LevelEditor::Ux3 {

namespace {

std::vector<std::function<void()>> g_tasks;
bool g_open[kWindowCount];
bool g_similarMenu, g_groupName;
Settings g_settings;

std::vector<ChangeRow> g_changes;
uint32_t g_changesAt;
size_t g_historyKey = static_cast<size_t>(-1);
bool g_refreshAsked = true;

void Say(const std::string& message) { ShowMessage(message, 5.0); }

// A setting of the toolbar's in the registry (the search shows and switches it).
void AddToggle(const char* id, const char* name, const char* tip, std::function<int()> checked, std::function<void()> run)
{
    Actions::Action a;
    a.id = id;
    a.name = name;
    a.category = "Settings";
    a.tip = tip;
    a.kind = Actions::Kind::Toggle;
    a.checked = std::move(checked);
    a.run = std::move(run);
    Actions::Register(a);
}

void Ask(Ui::Request::Kind kind, int value = 0, uint32_t id = 0)
{
    Ui::Request request;
    request.kind = kind;
    request.value = value;
    request.id = id;
    Ui::PushRequest(request);
}

} // namespace

void Queue(std::function<void()> task) { g_tasks.push_back(std::move(task)); }

bool& Open(Window window) { return g_open[window]; }

bool TakeSimilarMenu()
{
    bool asked = g_similarMenu;
    g_similarMenu = false;
    return asked;
}

bool TakeGroupName()
{
    bool asked = g_groupName;
    g_groupName = false;
    return asked;
}

void NoteSettings(const Settings& settings) { g_settings = settings; }
const Settings& SettingsNow() { return g_settings; }

void Init()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    Organize::Init();
    using namespace Actions;
    SetEnabled(kUndo, [] { return SettingsNow().canUndo; });
    SetEnabled(kRedo, [] { return SettingsNow().canRedo; });
    SetEnabled(kPasteTransform, [] { return Clip::HaveTransform(); });
    SetEnabled(kPasteFields, [] { return Clip::FieldCount() > 0; });
    // (the windows: their own flags, switched at once - nothing of the level changes)
    SetRun(kPalette, [] { g_open[kPaletteWindow] = !g_open[kPaletteWindow]; });
    SetRun(kHistory, [] { g_open[kHistoryWindow] = !g_open[kHistoryWindow]; });
    SetRun(kChanges, [] {
        g_open[kChangesWindow] = !g_open[kChangesWindow];
        g_refreshAsked = true;
    });
    SetRun(kKeys, [] { g_open[kKeysWindow] = !g_open[kKeysWindow]; });
    SetRun(kSnapshots, [] { g_open[kSnapshotsWindow] = !g_open[kSnapshotsWindow]; });
    SetChecked(kPalette, [] { return g_open[kPaletteWindow] ? 1 : 0; });
    SetChecked(kHistory, [] { return g_open[kHistoryWindow] ? 1 : 0; });
    SetChecked(kChanges, [] { return g_open[kChangesWindow] ? 1 : 0; });
    SetChecked(kKeys, [] { return g_open[kKeysWindow] ? 1 : 0; });
    SetChecked(kSnapshots, [] { return g_open[kSnapshotsWindow] ? 1 : 0; });
    SetChecked(kCollision, [] { return SettingsNow().collision ? 1 : 0; });
    AddToggle("snap25", "Snap", "Snap moves to the grid",
              [] { return SettingsNow().snapAlways ? 1 : 0; },
              [] { Ask(Ui::Request::kSnapAlways, SettingsNow().snapAlways ? 0 : 1); });
    AddToggle("carry_objects", "Carry Objects", "Move objects standing on scenery with it",
              [] { return SettingsNow().carry ? 1 : 0; }, [] { Ask(Ui::Request::kCarry, SettingsNow().carry ? 0 : 1); });
    struct PickToggle {
        const char *id, *name;
        uint32_t bit;
    };
    static const PickToggle kPicks[] = {{"pick_nodes", "Pick Nodes", Picking::kNodes},
                                        {"pick_objects", "Pick Objects", Picking::kObjects},
                                        {"pick_scenery", "Pick Scenery", Picking::kScenery}};
    // (UX4: the Overlays menu's switches; UX5: a note where the view looks, the budget meter)
    for (const Overlays::BitInfo& info : Overlays::Bits()) {
        uint32_t bit = info.bit;
        std::string id = "overlay_" + std::to_string(bit);
        AddToggle(id.c_str(), (std::string("Overlay ") + info.label).c_str(), info.tip,
                  [bit] { return Overlays::Shown(bit) ? 1 : 0; }, [bit] { Overlays::Set(bit, !Overlays::Shown(bit)); });
    }
    {
        Actions::Action note;
        note.id = "note_here";
        note.name = "Add Note";
        note.category = "Edit";
        note.tip = "Add a note at the view center (editor only)";
        note.run = [] { Notes::AddHereQueued(); };
        Actions::Register(note);
        Actions::Action budget;
        budget.id = "hint_budget";
        budget.name = "Budget";
        budget.category = "Panels";
        budget.kind = Actions::Kind::Info;
        budget.rebindable = false;
        budget.keysText = "Tool strip meter";
        budget.tip = "Level usage against engine limits";
        Actions::Register(budget);
    }
    for (const PickToggle& p : kPicks) {
        uint32_t bit = p.bit;
        AddToggle(p.id, p.name, "What a click in the view selects",
                  [bit] { return (SettingsNow().pickFilter & bit) ? 1 : 0; },
                  [bit] { Ask(Ui::Request::kPickFilter, 0, SettingsNow().pickFilter ^ bit); });
    }
}

void Update()
{
    using namespace Actions;
    // (the panels' clicks: done now, in the editor's update)
    std::vector<std::function<void()>> tasks;
    tasks.swap(g_tasks);
    for (const std::function<void()>& task : tasks)
        task();
    bool busy = ChangeUnderWay();
    // This module's own keys (the others are the editor's: level_editor.cpp reads them).
    for (int window : {kPalette, kHistory, kChanges, kKeys, kSnapshots}) {
        if (Pressed(window))
            Get(window)->run();
    }
    std::string message;
    if (Pressed(kCopy)) {
        Clip::Copy(&message);
        Say(message);
    }
    if (Pressed(kPaste) && !busy) {
        Clip::Paste(&message);
        Say(message);
    }
    if (Pressed(kCopyProps)) {
        Clip::CopyProps(&message);
        Say(message);
    }
    if (Pressed(kPasteTransform) && !busy) {
        Clip::PasteTransform(&message);
        Say(message);
    }
    if (Pressed(kPasteFields) && !busy) {
        Clip::PasteFields(&message);
        Say(message);
    }
    if (Pressed(kBoxSelect) && !busy)
        Organize::ArmBox();
    if (Pressed(kSelectSimilar) && !busy)
        g_similarMenu = true;
    if (Pressed(kSelectFamily) && !busy)
        Organize::SelectFamily();
    if (Pressed(kInvertSelection) && !busy)
        Organize::InvertSelection();
    if (Pressed(kSelectAll) && !busy)
        Organize::SelectAll();
    if (Pressed(kMakeGroup) && !busy)
        g_groupName = true;
    // The Changes list: refreshed when the history moved (a change made, undone or redone), when asked, or a second on
    // (other changes - a save, an autosave restored - don't move the history).
    size_t key = UndoHistory().size() * 1000003u + RedoHistory().size();
    uint32_t now = SDL_GetTicks();
    if (key != g_historyKey || g_refreshAsked || now - g_changesAt > 1000) {
        g_historyKey = key;
        g_refreshAsked = false;
        g_changesAt = now;
        RefreshChanges();
    }
}

const std::vector<ChangeRow>& ChangeList() { return g_changes; }

void RefreshChanges()
{
    g_changes.clear();
    std::string text;
    if (!Layout::CaptureText(&text))
        return;
    std::vector<std::string> lines;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    auto label = [](uint32_t checksum) {
        const char* name = Objects::NameOf(checksum);
        return std::string(name ? name : "");
    };
    for (const Changes::Entry& e : Changes::List(text, label))
        g_changes.push_back({e, e.line >= 0 && e.line < static_cast<int>(lines.size()) ? lines[e.line] : ""});
}

void GoTo(const ChangeRow& row)
{
    uint32_t id = row.entry.id;
    Queue([id] {
        if (!id || !GoToThing(id))
            Say("Nothing to go to for that change (it isn't in the level now)");
    });
}

void Revert(const ChangeRow& row)
{
    std::string record = row.record, summary = row.entry.summary;
    Queue([record, summary] {
        std::string text, message;
        if (!Layout::CaptureText(&text)) {
            Say("Not in a level");
            return;
        }
        // (the record as it is now: the line the list showed, found again)
        int line = -1, n = 0;
        for (size_t start = 0; start <= text.size() && line < 0; ++n) {
            size_t end = text.find('\n', start);
            std::string one = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (one == record)
                line = n;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        if (line < 0) {
            Say("That change has changed since - the list is refreshed");
            g_refreshAsked = true;
            return;
        }
        std::string label = "Revert: " + summary.substr(0, 48);
        if (ApplyLayoutStep(Changes::Without(text, line), label.c_str(), &message))
            Say("Reverted: " + summary + " (Ctrl+Z puts it back)");
        else
            Say("Couldn't revert it: " + message);
        g_refreshAsked = true;
    });
}

} // namespace Front::LevelEditor::Ux3
