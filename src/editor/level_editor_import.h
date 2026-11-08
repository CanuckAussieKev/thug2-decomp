#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace frontend::level_editor::ux3 {
struct Fonts;
}

// NOT ORIGINAL - the editor mode's Import Level window (ours, at the user's request: levels of the other Tony Hawk games
// the user has installed - THPS2, THPS3, THPS4, THUG, THAW, Project 8, Proving Ground - converted into THUG2's and
// edited and played like THUG2's own).
//   - File > Import Level... (and the Start page's Import Level button) opens it. Browse... picks the other game's
//     folder (the system's folder dialog; the folder last used for each game is remembered); the game is told from its
//     files and its levels listed - title, place, whether it is imported already, whether it fits a retail install.
//   - Import converts the checked levels, then makes the data overlay again (the level list the game reads): the content
//     pipeline's editor_import/import_levels.ts, run by the Node runtime in the editor's tools folder (editor-tools\
//     beside the exe: node.exe, ns.exe, the pipeline and its level converters - tools/editor_tools.ts). Each step is a
//     child process (platform/child_process.h): no console, its output shown in the window as it comes, Cancel ends it
//     and everything it started. A step that fails says why (the tool's last error line) and the rest don't run.
//   - Where the levels go: one setting (output_folder: the game folder; thug2-decomp.ini [editor] import_output=
//     another) - the converters' staging folders <output>\thug2-decomp-<game> and the overlay <output>\thug2-decomp-data,
//     as the converters' --thug2. The other game's folder is only read.
//   - Afterwards the level list scripts are read again (levels.qb, the level select's) so the levels are on the Start
//     page and in Open Level at once, a section per game; while a level is up that waits until the next level opens or
//     the Start page shows. A row of an imported level opens it (Open).
//   - Retail: the Retail column shows what the budget checker says of a level ("ok" / "big" from the list), and
//     Export to Retail Install runs the pipeline's kRetailExportTool when the tools folder has it (not yet: the button
//     is off).
// [editor] tools_folder= in thug2-decomp.ini points at another tools folder (a development build's out\editor-tools).
namespace frontend::level_editor::import {

// The content pipeline's export to a retail THUG2 install (its path under editor-tools\pipeline), run with
// --thug2 <output folder> and the chosen levels' load scripts.
inline constexpr const char* kRetailExportTool = "retail_export/retail_export.ts";

// Opens the window (`folder`: a game folder to list at once; empty: the last one used).
void open_window(const std::string& folder);
// The window, inside the editor's frames (shell::dialogs). Ticks the running step too.
void window(const ux3::Fonts& fonts);
// Whether a step runs (the window's Import, or a listing).
bool busy();

// The level list scripts read again if an import finished while a level was up (before another opens, as the Start
// page shows). True when it read them (the Start page's list is to be made again).
bool reload_if_pending();

// The automation channel's (shell::screen_command: fe editor import <folder> | importrun <level>... | importwait):
// open the window on a folder and list it; check those levels and import them. `result` once done: "ok ..." or
// "error: ...".
void automation_list(const std::string& folder);
void automation_import(const std::vector<std::string>& levels);
bool automation_done(std::string* result);
// "idle", "listing", "converting", "building", "exporting", "failed", "done" (the status line's).
const char* phase_name();

// The folder the converted levels and the overlay go to (the converters' --thug2).
std::string output_folder();
// The editor's tools folder (editor-tools\ beside the exe, or [editor] tools_folder).
std::string tools_folder();

} // namespace frontend::level_editor::import
