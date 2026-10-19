#pragma once

#include <cstdint>

namespace Mdl {
struct FrontEnd;
}
namespace Script {
class CStruct;
class CScript;
} // namespace Script

// NOT ORIGINAL - debug mode (ours, added at the user's request; not the game's and not PartyMod's): Neversoft's
// in-house build again, as far as the scripts go.
//
// The script command CD ("is this the disc build") is true in the retail exe - its CFunction table entry is the
// shared "return true" stub 0x005B9760 (68 commands use it) - and NotCD false (the "return false" stub 0x005B9E50).
// The scripts keep their developers' parts behind them: the pause menu's DEBUG OPTIONS hub (create_debug_options_menu:
// Cheats: Debug, Goto Restart, Debug Menu, Test Menu, the cutscene viewer), BACK TO CAS, the park editor's PARK-ED
// DEBUG MENU / SAVE PRE-MADE PARK, the viewer camera's buttons (viewer_buttons_enabled), the level select's
// DEVKIT_ONLY levels (Default), the Select combos' wireframe / render modes and screenshot (NotCD). Many debug
// commands those menus call are compiled out in this exe (the same stub: GetCurrentGeomStats, StartProfiler...); the
// script-only menus work.
//
// thug2-decomp.ini [features] debug_mode=1 makes CD false and NotCD true (default 0: as the game) - except for the
// start-up scripts (default_system_startup, autolaunch, startup_loading_screen): with CD false they'd take the
// developers' start-up (their loose files instead of the retail data's archives, the viewer instead of the main
// menu). Once the front end runs, debug mode does what that start-up did that still works: select_shift on (the
// Select combos) and LaunchViewer (the viewer module, Mdl::Viewer - game/sys/viewer.h - and its free camera
// viewer_cam). The data overlay's pause menu (tools/menu_level/make_menu_level.py) gets a DEBUG OPTIONS item while CD
// is false. What else CD false changes: Game Options gets "Cheats: DO NOT TEST"; leaving the pause menu enables the
// viewer camera's buttons; the cutscene viewer shows its debug text; the credits movie isn't played.
//
// The viewer on the keyboard (in a level, not paused; the controller's Select combos work too):
//   F9          free camera on / off (Select+X: UserSelectX - ToggleViewMode)
//   F10         render mode: wireframe, wireframe skins, normal (Select+Triangle) - drawn by game/gfx/wireframe.h
//   F11         in a wireframe mode: the next wireframe style; otherwise: the skater put where the camera is and the
//               free camera off (Select+Circle)
//   F12         a screenshot (Select+Square)
//   Home        rendering back to normal (Select+Start)
//   in the free camera: W / S forward / back, A / D left / right, E or Page Up / Q or Page Down up / down, the arrow
//   keys turn (left / right) and look (up / down), Tab the speed (medium, fast, super slow - UserViewerCircle), T the
//   next time of day (UserViewerTriangle).
namespace Sys::DebugMode {

bool& Enabled();
// CD: false in debug mode (but for the start-up scripts), else true (the original's stub).
bool __cdecl CFunc_CD(Script::CStruct* params, Script::CScript* script);
// NotCD: CD's opposite (the original: false).
bool __cdecl CFunc_NotCD(Script::CStruct* params, Script::CScript* script);
// Point the CFunction table's CD and NotCD entries (0x0067FF38, 0x0067FF40) at ours when debug mode is on. Until that
// table is the game's own source, this stands in for its entries (done by Sio::InitSdlInput, before the script
// functions are registered).
void Install();
// Once a frame from the front end's input logic (MouseMenus::Update): the viewer launched once, the F keys.
void Update(Mdl::FrontEnd* frontEnd);
// For the viewer's input handler (Mdl::HandleViewerInput, in the free camera): the movement keys as the sticks (raw,
// -127..127) and the up / down commands (0x2 / 0x4). False when no key is held (the controller's values stand).
bool KeyboardViewer(float* leftX, float* leftY, float* rightX, float* rightY, uint32_t* commands);

} // namespace Sys::DebugMode
