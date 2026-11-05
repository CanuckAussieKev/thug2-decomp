#pragma once

#include <string>
#include <vector>

// NOT ORIGINAL - the level editor's HUD (ours; see level_editor.h). The screen elements are made by the overlay's
// scripts (tools/pipeline/menu_level/make_menu_level.ts, gamemenu_pause.qb): level_editor_hud_create - a status panel top left
// (lines 1-4: the mode, the selection, what's under the cursor, the camera and counts), a bar at the bottom (lines
// 5-6: the controls for what you're doing) and a message line (7, top centre) - and level_editor_panel_show /
// level_editor_panel_hide: one panel of lines (the help, the piece browser). Text may hold the fonts' colour codes:
// \c1 (the highlight the cutscene subtitles use for names) ... \c0 (back to the element's colour).
namespace frontend::level_editor::hud {

constexpr double kDefaultMessageSeconds = 2.5;
// The editor's message durations, by how long the message is (the panels' own kinds keep their own).
constexpr double kShortMessageSeconds = 4.0;
constexpr double kMessageSeconds = 5.0;
constexpr double kLongMessageSeconds = 6.0;

// The editor starting: nothing shown is assumed (the elements are new). `screen_elements` false: the UI
// (level_editor_ui.h) shows the editor instead - the HUD's elements are taken down (level_editor_hud_destroy) and the
// lines and panels aren't set; the message is still kept (current_message).
void get_reset(bool screen_elements = true);
// The message being shown (empty: none).
const std::string& current_message();
// A line of the HUD (1-7), set when it changes (at most 90 characters: a much longer text corrupted the heap).
void change_strand(int line, const std::string& text);
// A message in the message line for `seconds`.
void get_message(const std::string& text, double seconds = kDefaultMessageSeconds);
// Once a frame: the message cleared when its time is up.
void tick();
// The panel: shown at (x, y) in the 640 x 480 screen, `width` wide, with these lines (the first in the title colour);
// made again only when its place or number of lines changes.
void show_panel(const std::vector<std::string>& lines, float x, float y, float width);
void hide_panel();
bool panel_shown();

// The keys in the highlight colour: "\c1" + keys + "\c0".
std::string get_key(const char* keys);

} // namespace frontend::level_editor::hud
