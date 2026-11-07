#pragma once

#include <string>

namespace modules {
struct FrontEnd;
}
namespace frontend::level_editor::ux3 {
struct Fonts;
}

// NOT ORIGINAL - the level editor's own application (ours, at the user's request: "the game inside the editor", after
// Unity's and Unreal's editors) - the editor mode (engine/sys/editor_mode.h: thug2-editor.exe, -editor). The game's front
// end never shows: it starts underneath as ever (its scripts, the main menu's level, the profile - what a level needs
// before it loads) while the editor's window stands over it, and every level is played in free skate.
//   - The Start page, from the first frame: the recent levels and the level list - each with its level select picture
//     where the data overlay has one (images\loadscrn_sel_*), the edited copies too (the levels with a saved layout -
//     what LevelEditor_EditedLevels lists) - and a filter. Clicking one opens it in the editor: the level select's
//     SKATE NOW for that level runs underneath (game/front_end/start_level.h: read_in_load), then the editor opens as
//     -edit opens it (level_editor_open.h); "Opening <level>" stands over the game meanwhile.
//   - File > Open Level (the same list), File > Recent, File > Exit. Another level from inside the editor: the
//     unsaved-changes question first, then the editor closes, and from the pause menu that brings back the game's own
//     change of level (the level select's) loads it - and the editor opens there.
//   - Leaving: Escape and the editor's other ways out don't leave to the game; closing the window or File > Exit quits
//     (the unsaved-changes question first - Save / Don't Save / Cancel, drawn by ImGui; while playing, back to the
//     editor first).
//   - Play: the Play menu's Play (from the level's start) and Play from Here (F5) play in the Scene panel, Stop (F5)
//     edits again (level_editor_play.h). The pause menu's items that leave the level or reach the front end are left out
//     (CHANGE LEVEL, QUIT, BACK TO CAS, CHANGE PRO, END CURRENT RUN: the data overlay makes them only while
//     LevelEditor_EditorMode is false - tools/pipeline/menu_level/make_menu_level.ts).
//   - The game's keys and controller rest while the editor's pages are up and while it edits (the editor reads its own).
// Without the editor mode nothing here runs.
namespace frontend::level_editor::shell {

// The editor mode is on (engine/sys/editor_mode.h).
bool on();

// Once a frame from the front end's input logic (the editor's extension, whether or not it owns the game): opening a
// level, leaving one for another, the quit after the question.
void tick_frame(modules::FrontEnd* front_end);
// The frame's UI while the editor isn't running and the game doesn't play from it (level_editor::blit): the Start
// page, or the page that stands over a level opening. False: nothing drawn (not the editor mode).
bool draw_frame();
// Inside the editor's frame and the play frame's (between ImGui's NewFrame and Render): Open Level's window and the
// unsaved-changes question.
void dialogs(const ux3::Fonts& fonts);
// The File menu's items of the editor mode: Open Level, Recent (file_menu_top), Exit (file_menu_bottom); and the play
// frame's File menu (Exit).
void file_menu_top();
void file_menu_bottom();
void play_file_menu();
// The Play menu's Play: from the level's start.
void play_menu_top();

// Whether the game's keys and controller rest now (the editor's pages up, or the editor running).
bool blocks_game_input();
// The window's close: taken - the question first, then the quit (true in the editor mode).
bool holds_quit();
// The editor's input logic, while it runs: true once when the shell asks it to close (another level to open).
bool take_leave_request();

// The automation channel's `fe editor ...` commands (devtools/automation.h, through the editor's extension):
//   fe editor start               waits for the Start page
//   fe editor open <level> [edited]   opens a level (its load script with or without Load_) and waits for the editor
//   fe editor play | playhere     plays from the level's start / from here, waits until it plays
//   fe editor stop                back to the editor, waits for it
//   fe editor exit                File > Exit ("refused: unsaved changes" when it would ask)
// False: not an editor command.
bool screen_command(const std::string& line, bool* done, std::string* result);
bool screen_result(std::string* result);
void screen_cancel();
bool screen_status(std::string* status);

} // namespace frontend::level_editor::shell
