#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The game's window on PC: an SDL window ([Graphics] in thug2-decomp.ini: ResolutionX / ResolutionY - 0 = the
// desktop's -, Windowed, Borderless) in place of the original's Win32 one (CreateGameWindow 0x005F5860, 640x480
// full screen). Its handle goes where the game keeps its window (0x007D6A28); Nx::InitDirect3D makes the device
// at its size, windowed or not.
namespace Sys {

// Read the settings, make the window (WinMain) and show it; the cursor hidden. Its focus events set the game's
// focus flag (0x007CCBE4) and allow / stop device recreation (0x00786AB4).
void CreateSdlWindow();

// The Windowed setting, and the window's size (at least 640x480; 0 before the window is made unless set).
bool& Windowed();
int& WindowWidth();
int& WindowHeight();

} // namespace Sys
