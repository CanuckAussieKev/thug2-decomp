#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor_changes.h"
#include "game/front/level_editor_nodes.h"

struct ImFont;

namespace Front::LevelEditor::Ui {
struct Model;
}

// NOT ORIGINAL - the level editor's history, changes, search and organising (ours; see level_editor.h): the panels and
// tools of level_editor_actions.h (the action registry / keymap - the command search, the Keys section),
// level_editor_changes.h (the Changes panel), level_editor_organize.h (the Outliner's toggles, groups, labels and type
// filters; box select; select similar; layout snapshots) and level_editor_clip.h (copy / paste between levels, the
// Inspector's transform / fields), and the undo history panel (level_editor_modules.h's UndoHistory). The editor calls
// Update once a frame (after the keymap's BeginFrame); the UI (level_editor_ui.cpp) calls the drawing hooks below.
// What the panels ask for is queued and done in the next Update (never while the frame is drawn).
//
// Keys (rebindable - the Keys section; level_editor_actions.h has them all):
//   Ctrl+Space or F3  the command search          Ctrl+H        the undo history     Ctrl+Shift+H  the changes
//   Ctrl+C / Ctrl+V   copy / paste between levels Ctrl+Shift+C  copy transform and fields
//   Ctrl+Shift+V      paste the transform         Ctrl+B        box select (the next drag)
//   Shift+G           select similar...           Ctrl+I        invert the selection   Ctrl+A  select all
//   Ctrl+G            group the selection         Ctrl+Shift+Z  redo (as Ctrl+Y)
// A drag from empty space in the view is a box select too (right: inside, left: crossing; Shift adds, Ctrl removes).
namespace Front::LevelEditor::Ux3 {

// --- the game side (level_editor_ux3.cpp) ----------------------------------------------------------------------------

// Once (the editor's Init): the Outliner's layout records, the actions' callbacks.
void Init();
// Once a frame while the editor runs: this module's actions, the queued tasks, the Changes list kept up to date.
void Update();
// A task for the next Update (the panels' clicks).
void Queue(std::function<void()> task);

// The windows (each opened / closed by its action or its toolbar button).
enum Window : int { kPaletteWindow, kHistoryWindow, kChangesWindow, kKeysWindow, kSnapshotsWindow, kWindowCount };
bool& Open(Window window);
// Asked for by the keys (Shift+G, Ctrl+G): the Select similar menu, the group name.
bool TakeSimilarMenu();
bool TakeGroupName();

// The Changes list (level_editor_changes.h) as of the last refresh, and its records' text (Revert finds the record by
// it). Refreshed when the undo history moves, the panel opens, or a second has passed.
struct ChangeRow {
    Changes::Entry entry;
    std::string record;
};
const std::vector<ChangeRow>& ChangeList();
void RefreshChanges();
// Go to / Revert (queued).
void GoTo(const ChangeRow& row);
void Revert(const ChangeRow& row);

// The model's few settings the search's toggles show (the UI notes them each frame).
struct Settings {
    bool snapAlways = false, carry = true, collision = false;
    uint32_t pickFilter = 0;
    bool canUndo = false, canRedo = false;
};
void NoteSettings(const Settings& settings);
const Settings& SettingsNow();

// --- the UI side (level_editor_ux3_ui.cpp) ---------------------------------------------------------------------------

struct Fonts {
    ImFont *ui = nullptr, *small = nullptr, *mono = nullptr, *heading = nullptr;
    float scale = 1.0f;
};
void SetFonts(const Fonts& fonts);
// The toolbar's buttons (after its view options): Search, History, Changes (a count badge), Snapshots.
void Toolbar(const Ui::Model& model);
// The windows over the view: the search, the history, the changes, the keys, the snapshots, the menus.
void Windows(const Ui::Model& model);
// The help window's list of every action from the registry (by category, with its keys as bound now).
void HelpFromRegistry();
// The Keys section: every rebindable action, its keys (click one to press a new key, + adds one, x clears, Reset) and
// clashes - in its own window, or hosted by a preferences window.
void KeysSection();
// The Outliner: the search's type filters (Organize::ParseFilter) for a row; the user's label shown beside a row's
// name; the groups at the top of the tree; a row's eye / lock / selectable toggles (drawn at `x`, the row's right
// end less their width - KeysWidth); the row's right-click menu; a search asked for (Select similar on a node).
bool RowPasses(const Nodes::Node& node, const std::string& name, const char* search);
std::string RowLabel(uint32_t name);
void Groups();
float TogglesWidth();
void RowToggles(const Nodes::Node& node, float x);
void RowMenu(const Nodes::Node& node);
std::string TakeOutlinerSearch();
// The Inspector's extra sections: the clipboard (copy / paste transform and fields) and, with several selected, their
// shared fields ("mixed" where they differ - a value typed goes to all of them, one undo step).
void InspectorSection(const Ui::Model& model);
// The Inspector's sections folded / unfolded as last time ([editor_sections] in the ini) - after it's drawn.
void RememberSections();

} // namespace Front::LevelEditor::Ux3
