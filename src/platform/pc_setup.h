#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

namespace sys {

// PartyMod's start-up, at the start of WinMain (before the command line is read): the settings file
// (thug2-decomp.ini) and its log - with [Miscellaneous] Debug=1 a console too -, the patched scripts, the SDL event
// pump, the music shuffle's random numbers seeded from the time (the C runtime's rand, audio::shuffle_random), the
// online service's host names, the button glyph settings and the graphics settings. The window
// (create_sdl_window) and input (sio::init_sdl_input) follow later in WinMain.
void init_pc_setup();

// The end of the game (sys::Main, when its main loop returns): the process ends at once - the game's own clean-up
// is skipped. (Not exit(): that would run the C runtime shutdown while the game's other threads still run.)
void fast_exit();

} // namespace Sys
