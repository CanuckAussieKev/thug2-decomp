#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <windows.h>

#include <cstdint>

struct SDL_Window;

// The game's window on PC: an SDL window ([Graphics] in thug2-decomp.ini: ResolutionX / ResolutionY - 0 = the
// desktop's -, Windowed, Borderless) in place of the original's Win32 one (create_game_window 0x005F5860, 640x480
// full screen). WinMain keeps its handle where the game keeps its window (0x007D6A28); render::init_direct_3d makes the
// device at its size, windowed or not.
namespace sys {

// The graphics API the window is made for: Vulkan's needs SDL's Vulkan window flag (NOT ORIGINAL; chosen by the app's
// renderer selection, app/renderer_choice.h).
enum class WindowApi : uint8_t { kDirect3D9, kVulkan };

// Read the settings, make the window (WinMain) and show it; the cursor hidden; the game's focus flag (0x007CCBE4) set.
// `api` adds the Vulkan flag for a Vulkan window. Returns the window's Win32 handle. (WinMain registers the handler of its focus events first: they set the focus
// flag and allow / stop device recreation, 0x00786AB4.)
HWND create_sdl_window(WindowApi api = WindowApi::kDirect3D9);
// The window's Win32 handle where WinMain keeps it (the original's 0x007D6A28; null before the window is made): the
// device is made for it, DirectSound and DirectInput cooperate with it.
HWND& game_window();

// The Windowed setting, and the window's size (at least 640x480; 0 before the window is made unless set).
bool& get_windowed();
int& get_window_width();
int& get_window_height();
// Not PartyMod's - the SDL window itself (null before create_sdl_window), for the level editor's UI layer
// (editor/level_editor_ui.h).
SDL_Window* sdl_window();

} // namespace sys
