#pragma once

#include <cstdint>

namespace ext {
class Extension;
}
namespace modules {
struct FrontEnd;
}
namespace script {
class Struct;
class Script;
} // namespace script

// NOT ORIGINAL - debug mode (ours, added at the user's request; not the game's and not PartyMod's): Neversoft's
// in-house build again, as far as the scripts go.
//
// The script command CD ("is this the disc build") is true in the retail exe - its Function table entry is the
// shared "return true" stub 0x005B9760 (68 commands use it) - and NotCD false (the "return false" stub 0x005B9E50).
// The scripts keep their developers' parts behind them: the pause menu's DEBUG OPTIONS hub (create_debug_options_menu:
// Cheats: Debug, Goto Restart, Debug Menu, Test Menu, the cutscene viewer), BACK TO CAS, the park editor's PARK-ED
// DEBUG MENU / SAVE PRE-MADE PARK, the viewer camera's buttons (viewer_buttons_enabled), the level select's
// DEVKIT_ONLY levels (Default), the Select combos' wireframe / render modes and screenshot (NotCD). Many debug
// commands those menus call are compiled out in this exe (the same stub: GetCurrentGeomStats, StartProfiler...); the
// script-only menus work.
//
// thug2-decomp.ini [features] debug_mode=1 makes CD false and NotCD true (default 0: as the game) - except for the
// start-up scripts (default_system_startup, autolaunch, startup_loading_screen) and LoadPermSongs (its playlist is
// filled only with CD true: no music otherwise): with CD false they'd take the developers' start-up (their loose files instead of the retail data's archives, the viewer instead of the main
// menu). Once the front end runs, debug mode does what that start-up did that still works: select_shift on (the
// Select combos) and LaunchViewer (the viewer module, modules::Viewer - engine/sys/model_viewer.h - and its free camera
// viewer_cam). The data overlay's pause menu (tools/pipeline/menu_level/make_menu_level.ts) gets a DEBUG OPTIONS item while CD
// is false. What else CD false changes: Game Options gets "Cheats: DO NOT TEST"; leaving the pause menu enables the
// viewer camera's buttons; the cutscene viewer shows its debug text; the credits movie isn't played.
//
// The viewer on the keyboard (in a level, not paused; the controller's Select combos work too):
//   F9          free camera on / off (Select+X: UserSelectX - ToggleViewMode)
//   F10         render mode: wireframe, wireframe skins, normal (Select+Triangle) - drawn by render_d3d9/wireframe.h
//   F11         in a wireframe mode: the next wireframe style; otherwise: the skater put where the camera is and the
//               free camera off (Select+Circle)
//   F12         a screenshot (Select+Square)
//   Home        rendering back to normal (Select+Start)
//   in the free camera: W / S forward / back, A / D left / right, E or Page Up / Q or Page Down up / down, the arrow
//   keys turn (left / right) and look (up / down), Tab the speed (medium, fast, super slow - UserViewerCircle), T the
//   next time of day (UserViewerTriangle).
namespace sys::debug_mode {

bool& is_switched_on();
// CD: false in debug mode (but for the start-up scripts), else true (the original's stub).
bool cfunc_cd(script::Struct* params, script::Script* script);
// NotCD: CD's opposite (the original: false).
bool cfunc_not_cd(script::Struct* params, script::Script* script);
// Point the Function table's CD and NotCD entries (0x0067FF38, 0x0067FF40) at ours when debug mode is on. Until that
// table is the game's own source, this stands in for its entries (done by sio::init_sdl_input, before the script
// functions are registered).
void install();
// Once a frame from the front end's input logic (through its extension): the viewer launched once, the F keys.
void tick_frame(modules::FrontEnd* front_end);
// Debug mode as an extension (engine/ext/extension.h; app/ registers it): its frame, while no extension owns the game.
ext::Extension& extension();
// Where the viewer's two sticks go (left and right, x and y).
struct ViewerSticks {
    float* left_x_{};
    float* left_y_{};
    float* right_x_{};
    float* right_y_{};
};

// For the viewer's input handler (modules::handle_viewer_input, in the free camera): the movement keys as the sticks (raw,
// -127..127) and the up / down commands (0x2 / 0x4). False when no key is held (the controller's values stand).
bool keyboard_viewer(ViewerSticks sticks, uint32_t* commands);

} // namespace sys::debug_mode
