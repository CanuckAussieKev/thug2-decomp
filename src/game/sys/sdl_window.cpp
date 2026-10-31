// The game's SDL window.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/sys/sdl_window.h"

#include <windows.h>

#include <cstdint>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include "game/front/automation.h"
#include "game/sys/platform_config.h"
#include "game/sys/debug_log.h"
#include "game/sys/sdl_events.h"
#include "game/sys/winmain.h"
#include "game/gfx/device.h"
#include "game/gfx/movie.h"

namespace SystemCore {

namespace {

bool s_windowed = false;
bool s_borderless = false;
int s_clientWidth = 0;
int s_clientHeight = 0;
SDL_Window* s_window = nullptr;

HWND& GameWindow() { return Engine3D::g_gameWindow; }
bool& Focused() { return g_appActive; }
// Whether the device may be recreated (off while the window is in the background).
bool& RecreateDevice() { return Engine3D::g_deviceLost; }

// A size no display mode reaches in both directions (width and height checked separately) becomes 0x0 (the
// desktop's).
void EnforceMaxResolution()
{
    DEVMODEA mode;
    bool widthFits = false;
    bool heightFits = false;
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode); ++i) {
        if (mode.dmPelsWidth >= static_cast<DWORD>(s_clientWidth))
            widthFits = true;
        if (mode.dmPelsHeight >= static_cast<DWORD>(s_clientHeight))
            heightFits = true;
    }
    if (!widthFits || !heightFits) {
        s_clientWidth = 0;
        s_clientHeight = 0;
    }
}

void LoadWindowSettings()
{
    s_clientWidth = BuildInfo::GetInt(BuildInfo::kGraphics, "ResolutionX", 0);
    s_clientHeight = BuildInfo::GetInt(BuildInfo::kGraphics, "ResolutionY", 0);
    if (!s_windowed)
        s_windowed = BuildInfo::FetchFlag(BuildInfo::kGraphics, "Windowed", false);
    s_borderless = BuildInfo::FetchFlag(BuildInfo::kGraphics, "Borderless", false);
}

void HandleWindowEvent(SDL_Event* event)
{
    if (event->type != SDL_WINDOWEVENT)
        return;
    if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        RecreateDevice() = false;
        // Not PartyMod's - with the test-automation channel on (game/front/automation.h) the game keeps drawing in
        // the background, so a test's picture of its window (PrintWindow) shows the frame now, not the last one
        // drawn before the window lost the focus (the main loop draws nothing while the flag is off).
        // Original behaviour (PartyMod v1.0.4): Focused() = false always.
        if (!MenuSystem::Automation::IsSwitchedOn())
            Focused() = false;
    } else if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        RecreateDevice() = true;
        Focused() = true;
    }
}

} // namespace

bool& Windowed() { return s_windowed; }
int& WindowWidth() { return s_clientWidth; }
int& WindowHeight() { return s_clientHeight; }
SDL_Window* SdlWindow() { return s_window; }

void CreateSdlWindow()
{
    LoadWindowSettings();
    OsLayer::RegisterEventHandler(HandleWindowEvent);
    SDL_Init(SDL_INIT_VIDEO);

    uint32_t flags = s_windowed ? SDL_WINDOW_SHOWN : SDL_WINDOW_FULLSCREEN;
    if (s_borderless && s_windowed)
        flags |= SDL_WINDOW_BORDERLESS;

    EnforceMaxResolution();
    if (s_clientWidth == 0 || s_clientHeight == 0) {
        SDL_DisplayMode desktop;
        SDL_GetDesktopDisplayMode(0, &desktop);
        s_clientWidth = desktop.w;
        s_clientHeight = desktop.h;
    }
    if (s_clientWidth < 640)
        s_clientWidth = 640;
    if (s_clientHeight < 480)
        s_clientHeight = 480;

    s_window = SDL_CreateWindow("THUG2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, s_clientWidth, s_clientHeight,
                                flags);
    if (!s_window)
        DebugLog::Printf(DebugLog::StageMap::kError, "Failed to create window! Error: %s\n", SDL_GetError());

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(s_window, &info);
    GameWindow() = info.info.win.window;
    Focused() = true;

    SDL_ShowCursor(0);
}

} // namespace Sys
