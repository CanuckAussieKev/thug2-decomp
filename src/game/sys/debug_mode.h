#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// NOT ORIGINAL - debug mode (ours, added at the user's request; not the game's and not PartyMod's): Neversoft's
// in-house build again, as far as the scripts go.
//
// The script command CD ("is this the disc build") is true in the retail exe - its CFunction table entry is the
// shared "return true" stub 0x005B9760 (68 commands use it). The scripts keep their developers' parts behind
// `if ! CD`: the pause menu's DEBUG OPTIONS hub (create_debug_options_menu: Cheats: Debug, Goto Restart, Debug Menu,
// Test Menu, the cutscene viewer), BACK TO CAS, the viewer camera's buttons (viewer_buttons_enabled), the level
// select's DEVKIT_ONLY levels (Default) and the restart menu's way back to the debug options. Many of the debug
// commands those menus call are compiled out in this exe (the same stub: GetCurrentGeomStats, StartProfiler...);
// the script-only menus work.
//
// thug2-decomp.ini [features] debug_mode=1 makes CD false (default 0: true, as the game) - except for the start-up
// scripts (default_system_startup, autolaunch, startup_loading_screen): with CD false they'd take the developers'
// start-up (their loose files instead of the retail data's archives, the viewer instead of the main menu). The data
// overlay's pause menu (tools/menu_level/make_menu_level.py) gets a DEBUG OPTIONS item while CD is false.
// What else CD false changes, after start-up: the level select shows the DEVKIT_ONLY levels (Default) and all levels;
// Game Options gets "Cheats: DO NOT TEST"; the pause menu BACK TO CAS (main_menu_jump_to_cas); leaving the pause menu
// enables the viewer camera's buttons; the cutscene viewer shows its debug text; the credits movie isn't played.
namespace Sys::DebugMode {

bool& Enabled();
// CD: false in debug mode (but for the start-up scripts), else true (the original's stub).
bool __cdecl CFunc_CD(Script::CStruct* params, Script::CScript* script);
// Point the CFunction table's CD entry (0x0067FF38) at CFunc_CD when debug mode is on. Until that table is the game's
// own source, this stands in for its entry (done by Sio::InitSdlInput, before the script functions are registered).
void Install();

} // namespace Sys::DebugMode
