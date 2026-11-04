#pragma once

#include <string>

union SDL_Event;
namespace modules {
struct FrontEnd;
}

// NOT ORIGINAL - the new front end (ours, at the user's request: the "B+C" redesign, docs/design/frontend/README.md -
// HMain.dc.html, HLevels.dc.html; Career / Create / Online / Options / Pause's content restyled). Every screen is the
// same layout: the art full-bleed behind the whole screen, a list column of about a third on a darkened, softened scrim
// over it (the user's change, 2026-10-25), numbered items in big type with the kinetic selection, the highlighted
// item's name big on the right with a mono kicker and hints.
//
// thug2-decomp.ini [features] new_frontend=1 (default 0: nothing of this runs - no context, no input taken, no file
// read). "The scripts say WHAT to show, not HOW to show it" (the user, 2026-10-26): every menu is the game's own -
// what it holds comes from the game's scripts and the overlay's (make_menu_level.ts addNewFrontendMain / Pause /
// Skateshop: the main menu's Career, Free Skate, Online, Create, Options, Quit; our Career menu; CREATE-A-MODES' LEVEL
// EDITOR; the pause menu's RESTART, CHANGE LEVEL, OPTIONS; the skate shop's Play Level - each only while
// NewFrontend_Item says so) - and is drawn in this look (new_frontend_mirror.h: Kind::Menu, Pause, Dialog, Skaters,
// List); the keys and buttons go to the game's menu, whose scripts do what is chosen (one press, one sound), the mouse
// steps it with its own pad events. Nothing presses the game's items for the player. The main menu's Continue (X / R:
// the last level played) is ours. Ours draws a screen of its own only where no menu of the game's is behind it - the
// level select and the settings (Options: new_frontend_options.h, the ini's settings in five groups, written at once;
// its Game options row runs the game's GAME OPTIONS) -, when a menu's item asks for it (NewFrontend_Open), on a root
// (Main / Pause) it leaves at once on Back. The level select's Skate starts the level in free skate
// (MenuSystem::begin_level::ReadIn) or changes level from the pause menu; Free Skate shows every level ([frontend]
// respect_locks=1: the game's own level select rules - seen flags outside a career, all_levels_unlocked); Career keeps
// the game's own rules. A network game's level select is ours too: when the game makes its own for one (the host
// options' Level in the front end, a network lobby's CHANGE LEVEL), ours comes up over it from its first frame - the
// same levels, the game's locks always - and a choice does what that select's item does (the network game's level
// preference set and the host options made again, or the lobby's change of level; CREATED PARK: the game's own park
// menu); Back is that select's Back. The automation channel's `fe` commands (devtools/automation.h) drive ours.
// The levels are data: <overlay>\frontend\levels.json (tools/pipeline/menu_level/frontend_levels.ts, written by
// make_menu_level.ts from the overlay's sectioned level select - the same sections, lists and load scripts). Without
// it Free Skate goes on to the original's level select. The last level played: thug2-decomp-frontend.txt next to the ini (the level the game last
// loaded that is in the data - whichever menu started it).
//
// Drawn with Dear ImGui's draw lists (its own context, its own font atlas: never ImGui's widgets or look), through the
// level editor's backend (level_editor_ui_backend.h: renderer-independent). The layout is the design's 1280 x 720,
// kept 16:9 and scaled to the window (frontend/new_frontend_model.h Frame); fonts are rasterised at the window's
// real pixel sizes, made again when its size changes. Fonts: the overlay's frontend\fonts (Anton-Regular.ttf,
// IBMPlexMono-Regular.ttf / -SemiBold.ttf, IBMPlexSans-Regular.ttf - SIL OFL, put there by make_menu_level.ts
// --frontend-fonts), else Windows' own at run time (Bahnschrift drawn condensed and heavier for the display face,
// Consolas, Segoe UI); [frontend] display_font= / mono_font= / mono_bold_font= / body_font= name other files (a
// bare name: the overlay's fonts folder, then C:\Windows\Fonts), display_condense= (percent) / display_bold= (0/1).
//
// Input: the keyboard, controllers and the mouse (hover selects, click confirms, wheel moves, right click backs) are
// read from SDL's events; while the new front end is up the game's own pad and keyboard polling gives the game
// nothing (sio::process_sdl_device), the mouse doesn't drive the original menus (mouse_menus.h) and Enter isn't
// passed on as the game's special key - and that lasts until the keys that chose are let go.
//
// The game's screens restyled (new_frontend_mirror.h), read from the screen tree each frame: the front end's roundbar
// menus and the pause menu in our design, every dialog box (create_snazzy_dialog_box: QUIT?, confirmations, warnings,
// errors - in the front end drawn over the screen they came up over, never over its 3D scene), the skater select (the 3D skater left in view; the skaters as a strip, the stats as a table), the on-screen
// keyboard (its keys, the text typed; a click picks a key), the stats screen (EDIT STATS / the story's STATS: each stat
// as the skater select's table, read from the game's stars, the points left; Left / Right are the game's, a click on
// a stat's blocks steps it) and every list - values as text (Music Level 5, Songs
// Random), checkboxes On / Off, button tags as the game spells them, a column beside the list paired with its rows (the
// grab tricks' names), long lists scrolled, horizontal menus stepped with Left / Right; the create modes' editors'
// lists drawn lighter over their view (the skater, the park). Left as the game draws them: the screens whose meaning
// is a picture (GAMMA, SCREEN SETUP), the park editor's piece browser (a tool's own UI, not a menu), and online when
// [frontend] restyle_online=0 (on by default - online is tested by the user by hand, never by a tool).
// [frontend] restyle_dialogs= / restyle_skater_select= / restyle_menus= (1 each) switch them off one by one;
// debug_restyle=1 logs the element tree of each screen left as it is. A menu being made (shown, not yet taking the pad
// while it animates in) is restyled from its first frame - paused, in the front end, or with a menu's anchor up (a
// network game's pause menu, made while the game runs on). The game keeps the keyboard and the controllers
// (its scripts, its selection - ours reads them each frame); the mouse is ours there (owns_mouse). Whenever ours hides
// the game's elements, a script scaling root_window back is undone the same frame: the game's is never drawn under ours.
namespace frontend::new_frontend {

// [features] new_frontend (read once).
bool& is_switched_on();

// sio::init_sdl_input: listens to SDL's events (nothing when off).
void prepare_all();
// Its script commands for the overlay's pause menu items (NewFrontend_PauseItem, NewFrontend_Open), registered after
// the game's own (sys::Main).
void register_script_functions();
// Once a frame, from the front end's input logic (its extension): decides whether it's up, runs the menus'
// input and time, does what they ask.
void tick_frame(modules::FrontEnd* front_end);
// After the game's 2D pass (render::draw_world): the frame, when it's up.
void blit();
// frame_loop::reset_device_if_lost, before the device is reset: its device objects dropped (made again when next drawn).
void device_lost();

// Whether the game's own input is held back now: the new front end is up, or the keys that chose for it are
// still down.
bool blocks_game_input();
// Whether it is drawn now (for the screenshot sidecar and the log).
bool showing();
// Whether an original screen is restyled now (phase 3): the mouse is ours (mouse_menus leaves the hidden menus alone),
// the keys and buttons the original's.
bool owns_mouse();
// The screen element manager's Update, before its layout pass (engine/ui/screen_element_manager.cpp): the original's
// elements hidden (or given back) for this frame - so a menu the scripts made since the last frame is never drawn as
// the game made it when ours draws it restyled (the pause menu, a dialog, the main menu), the restyled screen and the
// cover kept, and the front end's first frames covered until its first menu is up.
void before_screen_layout();

// --- the automation channel's `fe` commands (devtools/automation.h) ---
// `args` after "fe": select <screen>/<item> (waits for the screen - shown, not moving - then selects and chooses the
// item), focus <screen>/<item> (selects only), back, press <up|down|left|right|confirm|back|resume|prevtab|nexttab>.
// The result ("ok", "error: ...", "refused: ...") with *done true; "" and *done false while select / focus waits
// (then automation_result until it isn't ""). The Online screen's Play online is refused (never online).
std::string automation_command(const std::string& line, bool* done);
std::string automation_result();
void automation_cancel();
// Whether the screen `name` (Screen names: main, career, story, ... pause) is shown and not moving.
bool automation_screen_up(const std::string& name);
// For the status file: "<state> <screen>/<selected item> over the <anchor>" ("" when off).
std::string automation_status();

} // namespace frontend::new_frontend
