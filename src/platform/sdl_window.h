#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

struct SDL_Window;

// The game's window on PC: an SDL window ([Graphics] in thug2-decomp.ini: ResolutionX / ResolutionY - 0 = the
// desktop's -, Windowed, Borderless) in place of the original's Win32 one (create_game_window 0x005F5860, 640x480
// full screen). Its handle goes where the game keeps its window (0x007D6A28); render::init_direct_3d makes the device
// at its size, windowed or not.
namespace sys {

// Read the settings, make the window (WinMain) and show it; the cursor hidden. Its focus events set the game's
// focus flag (0x007CCBE4) and allow / stop device recreation (0x00786AB4).
void create_sdl_window();

// The Windowed setting, and the window's size (at least 640x480; 0 before the window is made unless set).
bool& get_windowed();
int& get_window_width();
int& get_window_height();
// Not PartyMod's - the SDL window itself (null before create_sdl_window), for the level editor's UI layer
// (editor/level_editor_ui.h).
SDL_Window* sdl_window();

} // namespace sys
