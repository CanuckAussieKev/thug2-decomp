#pragma once

#include <string>
#include <vector>

// NOT ORIGINAL - the level editor's HUD (ours; see level_editor.h). The screen elements are made by the overlay's
// scripts (tools/menu_level/make_menu_level.ts, gamemenu_pause.qb): level_editor_hud_create - a status panel top left
// (lines 1-4: the mode, the selection, what's under the cursor, the camera and counts), a bar at the bottom (lines
// 5-6: the controls for what you're doing) and a message line (7, top centre) - and level_editor_panel_show /
// level_editor_panel_hide: one panel of lines (the help, the piece browser). Text may hold the fonts' colour codes:
// \c1 (the highlight the cutscene subtitles use for names) ... \c0 (back to the element's colour).
namespace Front::LevelEditor::Hud {

// The editor starting: nothing shown is assumed (the elements are new). `screenElements` false: the UI
// (level_editor_ui.h) shows the editor instead - the HUD's elements are taken down (level_editor_hud_destroy) and the
// lines and panels aren't set; the message is still kept (CurrentMessage).
void Reset(bool screenElements = true);
// The message being shown (empty: none).
const std::string& CurrentMessage();
// A line of the HUD (1-7), set when it changes (at most 90 characters: a much longer text corrupted the heap).
void SetLine(int line, const std::string& text);
// A message in the message line for `seconds`.
void Message(const std::string& text, double seconds = 2.5);
// Once a frame: the message cleared when its time is up.
void Tick();
// The panel: shown at (x, y) in the 640 x 480 screen, `width` wide, with these lines (the first in the title colour);
// made again only when its place or number of lines changes.
void ShowPanel(const std::vector<std::string>& lines, float x, float y, float width);
void HidePanel();
bool PanelShown();

// The keys in the highlight colour: "\c1" + keys + "\c0".
std::string Key(const char* keys);

} // namespace Front::LevelEditor::Hud
