// The game's SDL window.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/sdl_window.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <SDL3/SDL.h>

#include "platform/app_window.h"
#include "platform/platform_config.h"
#include "platform/test_workspace.h"
#include "platform/debug_log.h"
#include "platform/process_flags.h"
#include "base/core/checked_index.h"
#include "base/core/ints.h"
#include "base/core/no_destroy.h"

namespace sys {

constexpr int kMinClientWidth = 640;
constexpr int kMinClientHeight = 480;

namespace {

// SDL 3's name for its Windows DPI-awareness setting (read when its video starts; no SDL_HINT_ constant names it).
constexpr const char* kDpiAwarenessHint = "SDL_WINDOWS_DPI_AWARENESS";

// NOT ORIGINAL: [Graphics] dpi_aware (default 1) - the process is per-monitor DPI aware (V2; approach from PartyMod's
// partymod-common, which declares the same awareness): on a display scaled above 100% the window is the game's size in
// real pixels, drawn sharp, not stretched by Windows. ResolutionX / ResolutionY are then the window's client area and
// the back buffer, both in physical pixels (on Windows SDL 3's window coordinates are pixels with or without its high
// pixel density flag, so the mouse, the window size the menus and the editor read and both renderers' back buffers
// agree), and 0 is the desktop's physical size. A window moved to a display of another scale keeps its client size
// in pixels (SDL keeps it). 0: DPI-unaware, as SDL 2 left the process - Windows scales the window by the display's
// scale (blurred, and larger than its pixels).
const char* dpi_awareness()
{
    return build_info::fetch_flag(build_info::kGraphics, "dpi_aware", true) ? "permonitorv2" : "unaware";
}

// NOT ORIGINAL grouping - sdl_window.cpp's state (the original's globals). Made on first use, never destroyed
// (sdl_window_state()).
struct SdlWindowState {
    bool windowed_ = false;
    bool borderless_ = false;
    int client_width_ = 0;
    int client_height_ = 0;
    SDL_Window* window_ = nullptr;
    HWND game_window_ = nullptr; // (WinMain's: the handle create_sdl_window returned)
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
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode) != FALSE; ++i) {
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

// NOT ORIGINAL - the application window's first size (app_window.h: none kept yet): most of the display it opens on,
// at least 640x480.
constexpr int kAppWindowPercent = 80;
constexpr int kPercent = 100;

void load_window_settings()
{
    // Not original - an application's window (app_window.h): windowed, its kept size - [Graphics]' size and full
    // screen aren't read.
    if (app_window::requested()) {
        const app_window::Placement placement = app_window::read_placement();
        sdl_window_state().windowed_ = true;
        sdl_window_state().borderless_ = false;
        sdl_window_state().client_width_ = placement.width;
        sdl_window_state().client_height_ = placement.height;
        return;
    }
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
// The display [Graphics] WindowDisplay names (0: none named, or not there).
SDL_DisplayID chosen_display()
{
    const int display = build_info::get_int(build_info::kGraphics, "WindowDisplay", 0);
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID chosen = 0;
    if (displays && display >= 1 && display <= count) {
        // (SDL 3 names a display by its id: the n-th of SDL's list is SDL 2's display index n - 1)
        const std::span<const SDL_DisplayID> listed(displays, static_cast<std::size_t>(count));
        chosen = core::at(listed, display - 1);
    }
    SDL_free(displays);
    return chosen;
}

int window_position()
{
    const SDL_DisplayID chosen = chosen_display();
    return chosen != 0 ? static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(chosen))
                       : static_cast<int>(SDL_WINDOWPOS_CENTERED);
}

// Full screen as SDL 2 made it: the mode of the window's display closest to the window's size (SDL 3's full screen
// keeps the desktop's mode unless one is chosen).
void choose_fullscreen_mode(SDL_Window* window)
{
    SDL_DisplayMode mode{};
    const bool found = SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window), sdl_window_state().client_width_,
                                                           sdl_window_state().client_height_, 0.0F, false, &mode);
    SDL_SetWindowFullscreenMode(window, found ? &mode : nullptr);
    SDL_SetWindowFullscreen(window, true);
}

// NOT ORIGINAL - a log line: the window's size in pixels, the display's scale and the DPI awareness asked for.
void log_window_size(SDL_Window* window)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    debug_log::printf(debug_log::StageMap::kInfo, "Window: %dx%d pixels, display scale %.2f (DPI awareness %s)\n",
                      width, height, static_cast<double>(SDL_GetWindowDisplayScale(window)), dpi_awareness());
}

} // namespace

bool& get_windowed() { return sdl_window_state().windowed_; }
int& get_window_width() { return sdl_window_state().client_width_; }
int& get_window_height() { return sdl_window_state().client_height_; }
SDL_Window* sdl_window() { return sdl_window_state().window_; }
SDL_Window* sdl_window_by_id(uint32_t id) { return SDL_GetWindowFromID(id); }
HWND& game_window() { return sdl_window_state().game_window_; }

void keep_text_input()
{
    SDL_Window* window = sdl_window_state().window_;
    if (window && !SDL_TextInputActive(window)) {
        SDL_StartTextInput(window);
    }
}

HWND create_sdl_window(WindowApi api)
{
    load_window_settings();
    SDL_SetHint(kDpiAwarenessHint, dpi_awareness());
    // (no on-screen keyboard with the text input - SDL 2 had none on Windows)
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    // Not original - a test workspace's game (platform/test_workspace.h) opens behind the user's window: shown and
    // raised without taking the focus (its tests post keys and capture the window, which needs no focus).
    if (platform::test_workspace()) {
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
    }
    SDL_Init(SDL_INIT_VIDEO);

    // (full screen: made hidden, given its mode, then shown - choose_fullscreen_mode)
    SDL_WindowFlags flags = sdl_window_state().windowed_ ? 0 : SDL_WINDOW_HIDDEN;
    // (not original: a test workspace's window made hidden too, then shown without the focus below - a window made
    // visible is activated by Windows as it's made, before SDL's hints apply)
    const bool behind = platform::test_workspace();
    if (behind) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (sdl_window_state().borderless_ && sdl_window_state().windowed_) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    // Not original - an application's window (app_window.h): resizable, maximised as it was left; its taskbar group.
    const bool app = app_window::requested();
    const app_window::Placement placement = app ? app_window::read_placement() : app_window::Placement{};
    if (app) {
        flags |= SDL_WINDOW_RESIZABLE;
        if (placement.maximised) {
            flags |= SDL_WINDOW_MAXIMIZED;
        }
        app_window::prepare_process();
        if (sdl_window_state().client_width_ == 0 || sdl_window_state().client_height_ == 0) {
            SDL_Rect usable{};
            const SDL_DisplayID display = chosen_display() != 0 ? chosen_display() : SDL_GetPrimaryDisplay();
            if (SDL_GetDisplayUsableBounds(display, &usable)) {
                sdl_window_state().client_width_ = usable.w * kAppWindowPercent / kPercent;
                sdl_window_state().client_height_ = usable.h * kAppWindowPercent / kPercent;
            }
        }
    }

    if (api == WindowApi::kVulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }

    if (!app) {
        enforce_max_resolution();
    }
    if (sdl_window_state().client_width_ == 0 || sdl_window_state().client_height_ == 0) {
        if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
            sdl_window_state().client_width_ = desktop->w;
            sdl_window_state().client_height_ = desktop->h;
        }
    }
    sdl_window_state().client_width_ = std::max(sdl_window_state().client_width_, kMinClientWidth);
    sdl_window_state().client_height_ = std::max(sdl_window_state().client_height_, kMinClientHeight);

    const int position = window_position();
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                          app ? app_window::settings().title.c_str() : "THUG2");
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, placement.have_place ? placement.x : position);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, placement.have_place ? placement.y : position);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, sdl_window_state().client_width_);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, sdl_window_state().client_height_);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<int64_t>(flags));
    sdl_window_state().window_ = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);

    HWND handle = nullptr;
    if (!sdl_window_state().window_) {
        debug_log::printf(debug_log::StageMap::kError, "Failed to create window! Error: %s\n", SDL_GetError());
    } else {
        if (!sdl_window_state().windowed_) {
            choose_fullscreen_mode(sdl_window_state().window_);
            SDL_ShowWindow(sdl_window_state().window_);
        } else if (behind) {
            SDL_ShowWindow(sdl_window_state().window_); // (SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN=0: shown, not activated)
        }
        handle = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_window_state().window_),
                                                         SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (app && handle) {
            SDL_SetWindowMinimumSize(sdl_window_state().window_, kMinClientWidth, kMinClientHeight);
            app_window::prepare_window(handle);
            // (a maximised window's client area: what the back buffer is made at - and what the next size change is
            // measured against)
            SDL_GetWindowSizeInPixels(sdl_window_state().window_, &sdl_window_state().client_width_,
                                      &sdl_window_state().client_height_);
            app_window::note_made(sdl_window_state().window_);
        }
        log_window_size(sdl_window_state().window_);
    }
    keep_text_input();
    set_app_active(true);

    SDL_HideCursor();
    return handle;
}

} // namespace sys
