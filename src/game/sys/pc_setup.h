#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

namespace Sys {

// PartyMod's start-up, at the start of WinMain (before the command line is read): the settings file
// (thug2-decomp.ini) and its log - with [Miscellaneous] Debug=1 a console too -, the patched scripts, the SDL event
// pump, the music shuffle's random numbers seeded from the time (the C runtime's rand, Pcm::ShuffleRandom), the
// online service's host names, the button glyph settings and the graphics settings. The window
// (CreateSdlWindow) and input (Sio::InitSdlInput) follow later in WinMain.
void InitPcSetup();

// The end of the game (Sys::Main, when its main loop returns): the process ends at once - the game's own clean-up
// is skipped. (Not exit(): that would run the C runtime shutdown while the game's other threads still run.)
void FastExit();

} // namespace Sys
