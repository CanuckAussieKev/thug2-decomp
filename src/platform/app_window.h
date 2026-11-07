#pragma once

#include <windows.h>

#include <string>

union SDL_Event;
struct SDL_Window;

// NOT ORIGINAL - the game's window as an application's (ours: the editor mode, engine/sys/editor_mode.h - app/ asks for
// it before the window is made). The window (platform/sdl_window.h) is then
//   - windowed and resizable whatever [Graphics] says (its Windowed, Borderless, ResolutionX / ResolutionY aren't read:
//     never full screen, no display mode switched), at the size, place and maximised state it had when it was last
//     closed - kept in `placement_file` (an ini file: [window] X, Y, Width, Height, Maximised; the first time a size
//     from the display it opens on - [Graphics] WindowDisplay -, centred there);
//   - titled `title`, in a taskbar group of its own (`app_id`: the process's AppUserModelID, and the window's - with
//     `display_name` and `launcher` as what pinning it starts again);
//   - given `launcher`'s icon (the "EDITOR" icon resource of the launcher exe beside the game; none if it isn't there).
// Without it nothing here runs: the window is the game's as it always was.
namespace sys::app_window {

struct AppWindow {
    std::string title;
    std::string placement_file;
    std::wstring app_id;
    std::wstring display_name;
    std::wstring launcher; // the launcher's file name beside the exe (its full path once request has found it)
};

// app/'s start-up (before the window is made). The launcher is looked for beside the exe (none: no icon, no relaunch).
void request(const AppWindow& window);
// Whether the window is made as an application's.
bool requested();
const AppWindow& settings();

// The kept placement (the window's client area in pixels; `have_place`: X / Y were kept and are on a display now).
struct Placement {
    int x{};
    int y{};
    int width{};
    int height{};
    bool have_place{};
    bool maximised{};
};
Placement read_placement();

// The process's taskbar group (before the window is made), then the window's: its group, relaunch command and icon.
void prepare_process();
void prepare_window(HWND window);

// The window made: its size in pixels and its normal place noted (what the window events are measured against).
void note_made(SDL_Window* window);
// A window event (the main loop's pump): the window's normal place and size followed, and whether it is maximised.
// True when the client area's size in pixels changed (`size` the new one): the back buffer follows it.
struct PixelSize {
    int width{};
    int height{};
};
bool note_event(SDL_Window* window, const SDL_Event& event, PixelSize* size);
// The placement written (the game ending; also as each move or resize ends, and as the window is maximised or restored,
// so a crash keeps it).
void save_placement();

// An application window of that title (or with it as its title's start: "<title> - <level>") already open - another
// copy running -, or null.
HWND find_running(const std::string& title);

} // namespace sys::app_window
