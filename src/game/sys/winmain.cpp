// The program's entry point and its command line.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, gethostbyname: what the game uses
#include "game/sys/winmain.h"
#include "game/net/gamespy.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/loading_bar_cfuncs.h"
#include "game/net/gamenet_qr2.h"
#include "game/net/gamenet_manager.h"
#include "game/net/online_identity.h"
#include "game/sio/device.h"
#include "hook/game_global.h"
#include "game/sys/log.h"
#include "game/sys/main.h"
#include "game/sys/pc_setup.h"
#include "game/sys/platform.h"
#include "game/sys/platform_d3d.h"
#include "game/sys/platform_input.h"
#include "game/sys/platform_system.h"
#include "game/sys/crt.h"
#include "game/sys/registry.h"
#include "game/sys/sdl_window.h"
#include "game/gfx/loading_console.h"
#include "hook/game.h"
#include "hook/hook.h"
#ifndef THUG2_STANDALONE
#include "hook/selftest.h"
#endif
#include "game/gfx/device.h"
#include "game/gfx/movie.h"

// The command line's values, kept in globals other files own (the e-mail and the password: buddy_status.cpp, the
// player's name and password: net_state_cfuncs.cpp).
namespace GameNet {
GAME_GLOBAL_EXTERN(char[256], g_commandLineEmail);
GAME_GLOBAL_EXTERN(char[16], g_commandLinePassword);
GAME_GLOBAL_EXTERN(char[10], g_pendingPassword);
} // namespace GameNet
namespace CFuncs {
GAME_GLOBAL_EXTERN(char[16], g_networkId);
GAME_GLOBAL_EXTERN(char[12], g_networkPassword);
GAME_GLOBAL_EXTERN(char[16], g_hostedGameName);
} // namespace CFuncs

namespace Sys {

GAME_GLOBAL(char[256], g_commandLineServerName, 0x007CC980); // "-connect"'s value

namespace {

// --- the command line's network options ----------------------------------------------------------------------

// A value: after any spaces, up to the next space - or, if it starts with a quote, up to the closing quote - at
// most `capacity` characters, then terminated.
void CopyValue(const char* from, char* to, unsigned capacity)
{
    while (*from == ' ')
        ++from;
    char end = ' ';
    if (*from == '"') {
        end = '"';
        ++from;
    }
    unsigned length = 0;
    for (;;) {
        char c = *from++;
        if (c == '\0' || c == end)
            break;
        to[length++] = c;
        if (length >= capacity)
            break;
    }
    to[length] = '\0';
}

// A number: after any spaces, as atol reads it.
long Number(const char* from)
{
    while (*from == ' ')
        ++from;
    return std::atol(from);
}

// --- WinMain's callees -------------------------------------------------------------------------------------------

HWND GameWindow() { return Nx::g_gameWindow; }

// GameSpy (library code, not decompiled): is the game's back end up?
void GsiStartAvailableCheck(const char* game) { GameSpy::StartAvailableCheck(game); }
int GsiAvailableCheckThink() { return GameSpy::AvailableCheckThink(); } // 0: still waiting, 1: up
void Msleep(unsigned milliseconds) { GameSpy::Msleep(milliseconds); }

// The game's C runtime: its rand() is the one the game's code draws from, and its exit functions run the game's
// own clean-up.
unsigned Time() { return Crt::Time(); }
void Srand(unsigned seed) { Crt::Srand(seed); }
void CExit() { Crt::CExit(); } // _cexit: atexit functions, streams flushed
[[noreturn]] void ExitNow(int code) { Crt::ExitNow(code); } // _exit

// The platform layer's parts without source yet.
char FindCdDrive(const char* volume) { return Platform::FindCdDrive(volume); }
void InitDirectSound(HWND window) { Platform::InitDirectSound(window); }
void ReleaseDirectSound() { Platform::ReleaseDirectSound(); }
void ReleaseDirect3D() { Platform::ReleaseDirect3D(); }
// A key's name (at most 14 characters), by its scan code (the table at 0x007D42F8).
void SetKeyName(unsigned scanCode, const wchar_t* name)
{
    Platform::SetKeyName(static_cast<int>(scanCode), name);
}

const char kSettingsKey[] = "Software\\Activision\\Tony Hawk's Underground 2\\Settings";

// The default bindings, written to the settings unless already there: k0_0..k0_23 (the keyboard: DirectInput
// key codes - W S A D, Z X C V, the number pad 2 6 4 8, none, Tab, the number pad 1 3 7 9 0 and its point, Home,
// End, Delete, Page Down), gp0_0..gp0_23 (the first pad) and gp1_0..gp1_23 (the second).
const char* const kBindingPrefixes[3] = {"k0_", "gp0_", "gp1_"};
const uint32_t kDefaultBindings[3][24] = {
    {0x11, 0x1F, 0x1E, 0x20, 0x2C, 0x2D, 0x2E, 0x2F, 0x50, 0x4D, 0x4B, 0x48,
     0x0FFFFFFF, 0x0F, 0x4F, 0x51, 0x47, 0x49, 0x52, 0x53, 0xC7, 0xCF, 0xD3, 0xD1},
    {2, 2, 1, 1, 0x0B, 0x0C, 0x0D, 0x0E},
    {},
};

} // namespace

GAME_GLOBAL(bool, g_internetGameHost, 0x007CC260);
GAME_GLOBAL(bool, g_joiningInternetGame, 0x007CCA80);
GAME_GLOBAL(bool, g_appActive, 0x007CCBE4);

// 0x004E16B0
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_DESTROY:
    case WM_CLOSE:
        PostQuitMessage(0);
        Nx::g_quitRequested = true; // quit
        return 0;
    case WM_ACTIVATE:
        // (the game's window, not the message's)
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            ShowWindow(GameWindow(), SW_MAXIMIZE);
            g_appActive = true; // active
        } else {
            ShowWindow(GameWindow(), SW_MINIMIZE);
            g_appActive = false;
            Nx::g_deviceLost = true; // the device is taken as lost (reset when active again)
            InvalidateRect(nullptr, nullptr, TRUE);
        }
        return 0;
    case WM_PAINT:
        ValidateRect(window, nullptr);
        return 0;
    case WM_KEYDOWN:
        Sio::SpecialKeyInput(static_cast<int>(wParam), static_cast<int>(lParam));
        return 0;
    case WM_CHAR:
        Sio::KeyInput(static_cast<int>(wParam), static_cast<int>(lParam));
        return 0;
    case WM_SYSKEYDOWN:
        if (wParam == VK_F10) // (not the menu)
            return 0;
        break;
    case WM_SYSCOMMAND:
        if (wParam == SC_KEYMENU) // (no menu from Alt)
            return 0;
        break;
    case WM_POWERBROADCAST:
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

// 0x004E1220
void __cdecl ParseNetworkParams(const char* commandLine)
{
#ifndef THUG2_STANDALONE
    RunStageOnce(TestStage::Late); // the test runner's Late stage: the C runtime and the memory manager are up
#endif
    ReadNetworkParams(commandLine);
}

void ReadNetworkParams(const char* commandLine)
{
    // The options are looked for in an upper-case copy; their values are read from the command line itself.
    // (The original copies it whole into its 1024 bytes - a longer command line overran its stack; ours looks
    // at the first 1023 characters.)
    char upper[1024];
    strncpy_s(upper, commandLine, _TRUNCATE);
    _strupr_s(upper);
    auto find = [&](const char* option) -> const char* {
        const char* at = std::strstr(upper, option);
        return at ? commandLine + (at - upper) : nullptr;
    };

    if (find("-HOST "))
        g_internetGameHost = true; // hosting a game
    if (const char* at = find("-HOSTNAME"))
        CopyValue(at + 9, CFuncs::g_hostedGameName, 15); // the hosted game's name
    if (const char* at = find("-EMAIL"))
        CopyValue(at + 6, GameNet::g_commandLineEmail, 255);
    if (const char* at = find("-PPASS"))
        CopyValue(at + 6, GameNet::g_commandLinePassword, 15);
    if (!g_internetGameHost) {
        // The server to join: its name, and its IPv4 address (as inet_addr gives it - INADDR_NONE if it isn't one
        // and the name isn't found either). (The value starts 9 characters on, one past "-CONNECT": the
        // character after it is skipped, normally a space.)
        if (const char* at = find("-CONNECT")) {
            char* server = g_commandLineServerName;
            CopyValue(at + 9, server, 255);
            uint32_t address = inet_addr(server);
            if (address == INADDR_NONE) {
                if (const hostent* host = gethostbyname(server))
                    std::memcpy(&address, host->h_addr_list[0], sizeof(address));
            }
            GameNet::g_commandLineServer = address;
            g_joiningInternetGame = true; // joining a game
        }
    }
    // (The numbers are compared unsigned: a negative one counts as too big.)
    if (const char* at = find("-MAXPLAYERS")) {
        auto players = static_cast<uint32_t>(Number(at + 11));
        reinterpret_cast<uint32_t&>(GameNet::g_netMaxPlayers) = players < 2 ? 2 : players > 8 ? 8 : players;
    }
    if (const char* at = find("-GROUP"))
        GameNet::g_lobbyGroupId = Number(at + 6);
    if (const char* at = find("-NTEAMS")) {
        auto teams = static_cast<uint32_t>(Number(at + 7));
        GameNet::g_networkTeamMode = teams > 4 ? 0 : teams;
    }
    if (const char* at = find("-SKILLLEVEL")) {
        auto skill = static_cast<uint32_t>(Number(at + 11));
        reinterpret_cast<uint32_t&>(GameNet::g_netSkillLevel) = skill > 4 ? 4 : skill;
    }
    if (const char* at = find("-NAME"))
        CopyValue(at + 5, CFuncs::g_networkId, 15); // the player's name
    if (const char* at = find("-PASSWORD")) {
        char* password = CFuncs::g_networkPassword;
        CopyValue(at + 9, password, 9);
        if (g_joiningInternetGame) {
            // Joining: it's the server's password (moved to where the join looks for it).
            std::memcpy(GameNet::g_pendingPassword, password, std::strlen(password) + 1); // (strcpy)
            std::memset(password, 0, 10);
        }
    }
}

// 0x004E2680
bool& AllowMultipleInstances()
{
    static bool allow = ConfigInt("features", "allow_multiple_instances", 0) != 0;
    return allow;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE /*previous*/, char* commandLine, int /*show*/)
{
    // (the reference build's own patch loads a DLL here - LoadLibraryA(<the name at 0x0064B198>) in a code cave (0x005480A1)
    // called in place of GsiStartAvailableCheck, which the cave then jumps to. PartyMod is part of the game's
    // source here, so nothing is loaded.)
    // (the game name: "thps6pc", or [Miscellaneous] OnlineGameName - game/net/online_identity.h)
    GsiStartAvailableCheck(Net::GameName());
    int available;
    while ((available = GsiAvailableCheckThink()) == 0)
        Msleep(5);
    if (available != 1)
        Dbg::Printf("The backend is not available\n");

    // Originally from PartyMod Fixes: its set-up (thug2-decomp.ini, logging, the patched scripts, the SDL event pump, the online service's
    // hosts, the button glyphs, the graphics settings) before the command line is read (PartyMod called it in
    // place of ParseNetworkParams, then that).
    InitPcSetup();
    ParseNetworkParams(commandLine);

    // The window title (the string at 0x0064B190, 25 characters - "...nd 2" - of which PartyMod's exe patch
    // leaves "THUG2"): if a window has it, the game is running already - that one is brought to the front.
    // (thug2-decomp.ini [features] allow_multiple_instances=1, not in the original: skip this check so a second copy
    // can run - e.g. to host and join a LAN game on one PC. Its socket falls back to a free port when the game's
    // port is taken (App::Bind) and its log goes to thug2-decomp-2.log.)
    const char kTitle[] = "THUG2";
    if (HWND running = AllowMultipleInstances() ? nullptr : FindWindowExA(nullptr, nullptr, nullptr, kTitle)) {
        ShowWindow(running, SW_RESTORE);
        BringWindowToTop(running);
        SetForegroundWindow(running);
        return 0;
    }

    // Originally from PartyMod Fixes: the SDL window (thug2-decomp.ini's size, windowed or not; the handle into 0x007D6A28) in place of
    //     char title[26] = <the title>;
    //     CreateGameWindow(<the title> as the class name, title, 100, 100, 640, 480, WindowProc 0x004E16B0,
    //                      instance, icon 0x66, true);                       // 0x005F5860 (true: full screen)
    CreateSdlWindow();
    Platform::ProcessWindowEvents();
    if (!GameWindow())
        return 0;
    // Originally from PartyMod Fixes: the window isn't shown here (SDL shows it). The original did
    //     ShowWindow(GameWindow(), Nx::g_showWindowFlag ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    Srand(Time());

    // The command line split at spaces (made terminators) into at most 20 arguments. The first is the command
    // line's first word - not the program's name.
    char* argv[21];
    int argc = 1;
    argv[0] = commandLine;
    char* p = commandLine;
    for (;;) {
        while (*p != '\0' && *p != ' ')
            ++p;
        while (*p == ' ')
            *p++ = '\0';
        if (*p == '\0')
            break;
        argv[argc++] = p;
        if (argc >= 20)
            break;
    }
    argv[argc] = nullptr;

    FindCdDrive("THUG CD2");
    InitDirectSound(GameWindow());
    for (int device = 0; device < 3; ++device) {
        for (int i = 0; i < 24; ++i) {
            char name[8] = {};
            std::snprintf(name, sizeof(name), "%s%d", kBindingPrefixes[device], i);
            WriteDefaultSetting(name, kDefaultBindings[device][i]);
        }
    }
    Platform::InitInput(instance, GameWindow(), reinterpret_cast<int>(kSettingsKey));

    // The keys' names: the character each scan code types in the keyboard layout (an acute accent shown as an
    // apostrophe, a semicolon as a colon); the function keys by name.
    for (unsigned scanCode = 0; scanCode < 256; ++scanCode) {
        char name[2] = {static_cast<char>(MapVirtualKeyA(MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK), MAPVK_VK_TO_CHAR)), 0};
        if (static_cast<uint8_t>(name[0]) == 0xB4)
            name[0] = '\'';
        else if (name[0] == ';')
            name[0] = ':';
        wchar_t wide[10];
        size_t converted;
        mbstowcs_s(&converted, wide, name, _TRUNCATE);
        SetKeyName(scanCode, wide);
    }
    SetKeyName(0x3B, L"F1");
    SetKeyName(0x3C, L"F2");
    SetKeyName(0x3D, L"F3");
    SetKeyName(0x3E, L"F4");
    SetKeyName(0x3F, L"F5");
    SetKeyName(0x40, L"F6");
    SetKeyName(0x41, L"F7");
    SetKeyName(0x42, L"F8");
    SetKeyName(0x43, L"F9");
    SetKeyName(0x44, L"F10");
    SetKeyName(0x57, L"F11");
    SetKeyName(0x58, L"F12");
    SetKeyName(0x64, L"F13");
    SetKeyName(0x65, L"F14");
    SetKeyName(0x66, L"F15");

    Main(argc, argv);

    // Originally from PartyMod Fixes: Main doesn't come back (FastExit ends the process when the main loop returns). The original
    // went on to shut down:
    ReleaseDirectSound();
    // Originally from PartyMod Fixes: DirectInput isn't released here (SDL input replaces it): the original called 0x005F39E0.
    Gfx::LoadingConsole::ScreenHidden(); // (not original: the loading console's timer stopped)
    Nx::RemoveLoadingScreen(); // (the renderer's clean-up)
    ReleaseDirect3D();
    InvalidateRect(nullptr, nullptr, TRUE);
    ShowWindow(GameWindow(), SW_SHOWMINIMIZED);
    CExit();
    Mem::ShutDownManager();
    ExitNow(0);
}

} // namespace Sys

REPLACE(0x004E16B0, Sys::WindowProc); // (the SDL window doesn't use it)
REPLACE(0x004E2680, Sys::WinMain);
REPLACE(0x004E1220, Sys::ParseNetworkParams);
