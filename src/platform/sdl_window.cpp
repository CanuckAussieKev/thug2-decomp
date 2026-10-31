// The game's SDL window.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/sdl_window.h"

#include <windows.h>

#include <cstdint>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include "frontend/automation.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include "platform/sdl_events.h"
#include "platform/winmain.h"
#include "engine/gfx/device.h"
#include "engine/gfx/movie.h"

namespace sys {

namespace {

bool s_windowed = false;
bool s_borderless = false;
int s_client_width = 0;
int s_client_height = 0;
SDL_Window* s_window = nullptr;

HWND& game_window() { return render::g_game_window; }
bool& get_focused() { return g_app_active; }
// Whether the device may be recreated (off while the window is in the background).
bool& recreate_device() { return render::g_device_lost; }

// A size no display mode reaches in both directions (width and height checked separately) becomes 0x0 (the
// desktop's).
void enforce_max_resolution()
{
    DEVMODEA mode;
    bool width_fits = false;
    bool height_fits = false;
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode); ++i) {
        if (mode.dmPelsWidth >= static_cast<DWORD>(s_client_width))
            width_fits = true;
        if (mode.dmPelsHeight >= static_cast<DWORD>(s_client_height))
            height_fits = true;
    }
    if (!width_fits || !height_fits) {
        s_client_width = 0;
        s_client_height = 0;
    }
}

void load_window_settings()
{
    s_client_width = build_info::get_int(build_info::kGraphics, "ResolutionX", 0);
    s_client_height = build_info::get_int(build_info::kGraphics, "ResolutionY", 0);
    if (!s_windowed)
        s_windowed = build_info::fetch_flag(build_info::kGraphics, "Windowed", false);
    s_borderless = build_info::fetch_flag(build_info::kGraphics, "Borderless", false);
}

void handle_window_event(SDL_Event* event)
{
    if (event->type != SDL_WINDOWEVENT)
        return;
    if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        recreate_device() = false;
        // Not PartyMod's - with the test-automation channel on (frontend/automation.h) the game keeps drawing in
        // the background, so a test's picture of its window (PrintWindow) shows the frame now, not the last one
        // drawn before the window lost the focus (the main loop draws nothing while the flag is off).
        // Original behaviour (PartyMod v1.0.4): Focused() = false always.
        if (!frontend::automation::is_switched_on())
            get_focused() = false;
    } else if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        recreate_device() = true;
        get_focused() = true;
    }
}

} // namespace

bool& get_windowed() { return s_windowed; }
int& get_window_width() { return s_client_width; }
int& get_window_height() { return s_client_height; }
SDL_Window* sdl_window() { return s_window; }

void create_sdl_window()
{
    load_window_settings();
    os_layer::register_event_handler(handle_window_event);
    SDL_Init(SDL_INIT_VIDEO);

    uint32_t flags = s_windowed ? SDL_WINDOW_SHOWN : SDL_WINDOW_FULLSCREEN;
    if (s_borderless && s_windowed)
        flags |= SDL_WINDOW_BORDERLESS;

    enforce_max_resolution();
    if (s_client_width == 0 || s_client_height == 0) {
        SDL_DisplayMode desktop;
        SDL_GetDesktopDisplayMode(0, &desktop);
        s_client_width = desktop.w;
        s_client_height = desktop.h;
    }
    if (s_client_width < 640)
        s_client_width = 640;
    if (s_client_height < 480)
        s_client_height = 480;

    s_window = SDL_CreateWindow("THUG2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, s_client_width,
                                s_client_height, flags);
    if (!s_window)
        debug_log::printf(debug_log::StageMap::kError, "Failed to create window! Error: %s\n", SDL_GetError());

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(s_window, &info);
    game_window() = info.info.win.window;
    get_focused() = true;

    SDL_ShowCursor(0);
}

} // namespace sys
