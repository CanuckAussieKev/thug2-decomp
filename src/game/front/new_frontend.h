#pragma once

#include <string>

union SDL_Event;
namespace Mdl {
struct FrontEnd;
}

// NOT ORIGINAL - the new front end (ours, at the user's request: the "B+C" redesign, docs/design/frontend/README.md -
// HMain.dc.html, HLevels.dc.html; Career / Create / Online / Options / Pause's content restyled). Every screen is the
// same layout: the art full-bleed behind the whole screen, a list column of about a third on a darkened, softened scrim
// over it (the user's change, 2026-10-25), numbered items in big type with the kinetic selection, the highlighted
// item's name big on the right with a mono kicker and hints.
//
// thug2-decomp.ini [features] new_frontend=1 (default 0: nothing of this runs - no context, no input taken, no file
// read). While on, it is drawn over the original screens it stands for ("anchors" - new_frontend_handoff.h) and has
// the input:
//   - over the MAIN MENU: Main (Career, Free Skate, Online, Create, Options; X / R: Continue - the last level played);
//     Career (THUG2 Story, Classic, THUG Story shown as in development), Story / Classic (Continue, New, Load progress),
//     Difficulty, Confirm (the original's overwrite warning, asked first); Create (CAS, CAT, CAG, CAP, CAGR, Level
//     Editor); Online (Play online, Identity); Options (new_frontend_options.h: the ini's settings in five groups,
//     segmented choices, written to thug2-decomp.ini at once; the Game options row opens the original GAME OPTIONS).
//   - over STORY / CLASSIC MODE OPTIONS (back from the original's own screens): the mode's screen; Back is the
//     original's back.
//   - over CREATE-A-MODES (back from CAS and the others): Create; Back is the original's back.
//   - over the SKATE SHOP menu after the original's skater select (Free Skate): the level select - Skate starts the
//     level in free skate (Front::StartLevel::Load); Back goes back to the skater select.
//   - a level's PAUSE MENU (free skate, story, classic; never online) is the game's own, restyled (phase 3, Kind::Pause):
//     its items as the game made them for the mode - with the overlay's (make_menu_level addNewFrontendPause: RESTART,
//     CHANGE LEVEL to our level select, OPTIONS - each only while NewFrontend_PauseItem says so) -, in our words where
//     we have them (Resume, Quit to menu...), the session (best combo, gaps, time) on the right; the keys and buttons
//     reach the game's menu, its scripts do what is chosen (one press, one sound - the user's 2026-10-26 rule: no
//     hand-off to the original menus). Ours comes up only for a screen its item asks for (NewFrontend_Open: the level
//     select, Options) and goes back to the restyled pause menu when that is closed.
// Hand-offs (new_frontend_handoff.h) do what the player would press through: the original's items chosen by their
// pad_choose (found by id, or by their handler's new_menu_script / change_gamemode / dif ...), one a frame, ours drawn
// over until the original's screen changes; then ours steps aside until an anchor is up again. Free Skate shows every
// level ([frontend] respect_locks=1: the game's own level select rules - seen flags outside a career, all_levels_
// unlocked); Career keeps the game's own rules (the original screens). The automation channel's `fe` commands
// (game/front/automation.h) drive it.// The levels are data: <overlay>\frontend\levels.json (tools/pipeline/menu_level/frontend_levels.ts, written by
// make_menu_level.ts from the overlay's sectioned level select - the same sections, lists and load scripts). Without
// it Free Skate goes on to the original's level select. The last level played: thug2-decomp-frontend.txt next to the ini (the level the game last
// loaded that is in the data - whichever menu started it).
//
// Drawn with Dear ImGui's draw lists (its own context, its own font atlas: never ImGui's widgets or look), through the
// level editor's backend (level_editor_ui_backend.h: renderer-independent). The layout is the design's 1280 x 720,
// kept 16:9 and scaled to the window (game/front/new_frontend_model.h Frame); fonts are rasterised at the window's
// real pixel sizes, made again when its size changes. Fonts: the overlay's frontend\fonts (Anton-Regular.ttf,
// IBMPlexMono-Regular.ttf / -SemiBold.ttf, IBMPlexSans-Regular.ttf - SIL OFL, put there by make_menu_level.ts
// --frontend-fonts), else Windows' own at run time (Bahnschrift drawn condensed and heavier for the display face,
// Consolas, Segoe UI); [frontend] display_font= / mono_font= / mono_bold_font= / body_font= name other files (a
// bare name: the overlay's fonts folder, then C:\Windows\Fonts), display_condense= (percent) / display_bold= (0/1).
//
// Input: the keyboard, controllers and the mouse (hover selects, click confirms, wheel moves, right click backs) are
// read from SDL's events; while the new front end is up the game's own pad and keyboard polling gives the game
// nothing (Sio::ProcessSdlDevice), the mouse doesn't drive the original menus (MouseMenus::Update) and Enter isn't
// passed on as the game's special key - and that lasts until the keys that chose are let go.
//
// Phase 3 (new_frontend_mirror.h): the original screens ours doesn't replace are restyled in the same look while ours
// isn't drawn - every dialog box (create_snazzy_dialog_box: QUIT?, confirmations, warnings, errors), the skater select
// (the 3D skater left in view; the skaters as a strip, the stats as a table) and plain lists (game options' sub-menus,
// YOUR OPTIONS, high scores, cutscenes, ...). The original keeps the keyboard and the controllers (its scripts, its
// selection - ours reads them each frame); the mouse is ours there (OwnsMouse). Not the create modes' editors (CAS,
// CAT, CAG, the park editor, graphics) or online. [frontend] restyle_dialogs= / restyle_skater_select= / restyle_menus=
// (1 each) switch them off one by one. Whenever ours hides the original's elements (the pause menu, a restyled
// screen), a script scaling root_window back is undone the same frame: the original is never drawn under ours.
namespace Front::NewFrontend {

// [features] new_frontend (read once; self-tests may set it).
bool& Enabled();

// Sio::InitSdlInput: listens to SDL's events (nothing when off).
void Init();
// Its script commands for the overlay's pause menu items (NewFrontend_PauseItem, NewFrontend_Open), registered after
// the game's own (Sys::Main).
void RegisterScriptFunctions();
// Once a frame, from the front end's input logic (MouseMenus::Update): decides whether it's up, runs the menus'
// input and time, does what they ask.
void Update(Mdl::FrontEnd* frontEnd);
// After the game's 2D pass (Nx::RenderWorld): the frame, when it's up.
void Draw();
// Mlp::ResetDeviceIfLost, before the device is reset: its device objects dropped (made again when next drawn).
void DeviceLost();

// Whether the game's own input is held back now: the new front end is up, or the keys that chose for it are
// still down.
bool BlocksGameInput();
// Whether it is drawn now (for the screenshot sidecar and the log).
bool Showing();
// Whether an original screen is restyled now (phase 3): the mouse is ours (MouseMenus leaves the hidden menus alone),
// the keys and buttons the original's.
bool OwnsMouse();

// --- the automation channel's `fe` commands (game/front/automation.h) ---
// `args` after "fe": select <screen>/<item> (waits for the screen - shown, not moving - then selects and chooses the
// item), focus <screen>/<item> (selects only), back, press <up|down|left|right|confirm|back|resume|prevtab|nexttab>.
// The result ("ok", "error: ...", "refused: ...") with *done true; "" and *done false while select / focus waits
// (then AutomationResult until it isn't ""). The Online screen's Play online is refused (never online).
std::string AutomationCommand(const std::string& args, bool* done);
std::string AutomationResult();
void AutomationCancel();
// Whether the screen `name` (Screen names: main, career, story, ... pause) is shown and not moving.
bool AutomationScreenUp(const std::string& name);
// For the status file: "<state> <screen>/<selected item> over the <anchor>" ("" when off).
std::string AutomationStatus();

} // namespace Front::NewFrontend
