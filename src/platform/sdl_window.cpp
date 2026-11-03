// The game's SDL window.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/sdl_window.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include "frontend/automation.h"
#include "platform/platform_config.h"
#include "platform/debug_log.h"
#include "platform/sdl_events.h"
#include "platform/winmain.h"
#include "engine/core/ints.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"

namespace sys {

constexpr int kMinClientWidth = 640;
constexpr int kMinClientHeight = 480;

namespace {

// SDL's window-manager information as it is on Windows (SDL_syswm.h): the version, the subsystem, then the platform's
// part of its union - Windows' window first (`info.win.window`), the rest of the union after it. Read whole, so the
// union isn't read through a member.
struct WindowsWmInfo {
    SDL_version version;
    SDL_SYSWM_TYPE subsystem;
    HWND window;
    std::array<std::byte, sizeof(SDL_SysWMinfo) - offsetof(SDL_SysWMinfo, info) - sizeof(HWND)> rest;
};
static_assert(sizeof(WindowsWmInfo) == sizeof(SDL_SysWMinfo) && offsetof(WindowsWmInfo, window) == offsetof(SDL_SysWMinfo, info),
              "SDL_SysWMinfo's layout");

// NOT ORIGINAL grouping - sdl_window.cpp's state (the original's globals). Made on first use, never destroyed
// (sdl_window_state()).
struct SdlWindowState {
    bool windowed_ = false;
    bool borderless_ = false;
    int client_width_ = 0;
    int client_height_ = 0;
    SDL_Window* window_ = nullptr;
};

SdlWindowState& sdl_window_state()
{
    static core::NoDestroy<SdlWindowState> state;
    return state.get();
}

// Whether the device may be recreated (off while the window is in the background).

// A size no display mode reaches in both directions (width and height checked separately) becomes 0x0 (the
// desktop's).
void enforce_max_resolution()
{
    DEVMODEA mode;
    bool width_fits = false;
    bool height_fits = false;
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode); ++i) {
        if (mode.dmPelsWidth >= ints::as_unsigned(sdl_window_state().client_width_)) {
            width_fits = true;
        }
        if (mode.dmPelsHeight >= ints::as_unsigned(sdl_window_state().client_height_)) {
            height_fits = true;
        }
    }
    if (!width_fits || !height_fits) {
        sdl_window_state().client_width_ = 0;
        sdl_window_state().client_height_ = 0;
    }
}

void load_window_settings()
{
    sdl_window_state().client_width_ = build_info::get_int(build_info::kGraphics, "ResolutionX", 0);
    sdl_window_state().client_height_ = build_info::get_int(build_info::kGraphics, "ResolutionY", 0);
    if (!sdl_window_state().windowed_) {
        sdl_window_state().windowed_ = build_info::fetch_flag(build_info::kGraphics, "Windowed", false);
    }
    sdl_window_state().borderless_ = build_info::fetch_flag(build_info::kGraphics, "Borderless", false);
}

// NOT ORIGINAL: [Graphics] WindowDisplay=<n> (1 = the first monitor, 2 = the second...; 0, the default, or a monitor
// that isn't there: centred on the primary one, as before) - the monitor a window is centred on. The workspaces'
// test games use 2 (tools/game/game_exe.ps1 Set-WorkspaceMuted).
int window_position()
{
    const int display = build_info::get_int(build_info::kGraphics, "WindowDisplay", 0);
    if (display < 1 || display > SDL_GetNumVideoDisplays()) {
        return SDL_WINDOWPOS_CENTERED;
    }
    return static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display - 1));
}

void handle_window_event(SDL_Event* event)
{
    if (event->type != SDL_WINDOWEVENT) {
        return;
    }
    if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        render::engine_parts().set_device_lost(false);
        // Not PartyMod's - with the test-automation channel on (frontend/automation.h) the game keeps drawing in
        // the background, so a test's picture of its window (PrintWindow) shows the frame now, not the last one
        // drawn before the window lost the focus (the main loop draws nothing while the flag is off).
        // Original behaviour (PartyMod v1.0.4): Focused() = false always.
        if (!frontend::automation::is_switched_on()) {
            set_app_active(false);
        }
    } else if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        render::engine_parts().set_device_lost(true);
        set_app_active(true);
    }
}

} // namespace

bool& get_windowed() { return sdl_window_state().windowed_; }
int& get_window_width() { return sdl_window_state().client_width_; }
int& get_window_height() { return sdl_window_state().client_height_; }
SDL_Window* sdl_window() { return sdl_window_state().window_; }

void create_sdl_window()
{
    load_window_settings();
    os_layer::register_event_handler(handle_window_event);
    SDL_Init(SDL_INIT_VIDEO);

    uint32_t flags = sdl_window_state().windowed_ ? SDL_WINDOW_SHOWN : SDL_WINDOW_FULLSCREEN;
    if (sdl_window_state().borderless_ && sdl_window_state().windowed_) {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    enforce_max_resolution();
    if (sdl_window_state().client_width_ == 0 || sdl_window_state().client_height_ == 0) {
        SDL_DisplayMode desktop;
        SDL_GetDesktopDisplayMode(0, &desktop);
        sdl_window_state().client_width_ = desktop.w;
        sdl_window_state().client_height_ = desktop.h;
    }
    sdl_window_state().client_width_ = std::max(sdl_window_state().client_width_, kMinClientWidth);
    sdl_window_state().client_height_ = std::max(sdl_window_state().client_height_, kMinClientHeight);

    const int position = window_position();
    sdl_window_state().window_ = SDL_CreateWindow("THUG2", position, position, sdl_window_state().client_width_,
                                sdl_window_state().client_height_, flags);
    if (!sdl_window_state().window_) {
        debug_log::printf(debug_log::StageMap::kError, "Failed to create window! Error: %s\n", SDL_GetError());
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(sdl_window_state().window_, &info);
    render::renderer().set_game_window(std::bit_cast<WindowsWmInfo>(info).window);
    set_app_active(true);

    SDL_ShowCursor(0);
}

} // namespace sys
