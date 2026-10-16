// The program's entry point and its command line.
// Contains MIT-licensed code (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md (the PC port's changes in it).

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, gethostbyname: what the game uses
#include "game/sys/winmain.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/sio/device.h"
#include "game/sys/main.h"
#include "game/sys/pc_setup.h"
#include "game/sys/platform.h"
#include "game/sys/registry.h"
#include "game/sys/sdl_window.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"

namespace Sys {

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

char* GameString(uintptr_t address) { return reinterpret_cast<char*>(address); }

// --- WinMain's callees -------------------------------------------------------------------------------------------

HWND GameWindow() { return GameVariable<HWND>(0x007D6A28); }

// GameSpy (library code, not decompiled): is the game's back end up?
void GsiStartAvailableCheck(const char* game) { GameFunction<void(__cdecl*)(const char*)>(0x005F8690)(game); }
int GsiAvailableCheckThink() { return GameFunction<int(__cdecl*)()>(0x005F8840)(); } // 0: still waiting, 1: up
void Msleep(unsigned milliseconds) { GameFunction<void(__cdecl*)(unsigned)>(0x005F8160)(milliseconds); }

// The game's C runtime: its rand() is the one the game's code draws from, and its exit functions run the game's
// own clean-up.
unsigned Time() { return GameFunction<unsigned(__cdecl*)(unsigned*)>(0x00625138)(nullptr); }
void Srand(unsigned seed) { GameFunction<void(__cdecl*)(unsigned)>(0x00623B96)(seed); }
void CExit() { GameFunction<void(__cdecl*)()>(0x00624B20)(); } // _cexit: atexit functions, streams flushed
[[noreturn]] void ExitNow(int code) // _exit
{
    GameFunction<void(__cdecl*)(int)>(0x00624B0F)(code);
    __assume(0);
}

// The platform layer's parts without source yet.
char FindCdDrive(const char* volume) { return GameFunction<char(__cdecl*)(const char*)>(0x005F5740)(volume); }
void InitDirectSound(HWND window) { GameFunction<void(__cdecl*)(HWND)>(0x005F59D0)(window); }
void ReleaseDirectSound() { GameFunction<void(__cdecl*)()>(0x005F5A00)(); }
void ReleaseDirect3D() { GameFunction<void(__cdecl*)()>(0x005F55E0)(); }
// A key's name (at most 14 characters), by its scan code (the table at 0x007D42F8).
void SetKeyName(unsigned scanCode, const wchar_t* name)
{
    GameFunction<void(__cdecl*)(unsigned, const wchar_t*)>(0x005F3BF0)(scanCode, name);
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

// 0x004E16B0
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_DESTROY:
    case WM_CLOSE:
        PostQuitMessage(0);
        GameVariable<bool>(0x007D6A2C) = true; // quit
        return 0;
    case WM_ACTIVATE:
        // (the game's window, not the message's)
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            ShowWindow(GameWindow(), SW_MAXIMIZE);
            GameVariable<bool>(0x007CCBE4) = true; // active
        } else {
            ShowWindow(GameWindow(), SW_MINIMIZE);
            GameVariable<bool>(0x007CCBE4) = false;
            GameVariable<bool>(0x00786AB4) = true; // the device is taken as lost (reset when active again)
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
    RunStageOnce(TestStage::Late); // the test runner's Late stage: the C runtime and the memory manager are up
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
        GameVariable<bool>(0x007CC260) = true; // hosting a game
    if (const char* at = find("-HOSTNAME"))
        CopyValue(at + 9, GameString(0x0067EFBC), 15); // the hosted game's name (the exe has "Beenox")
    if (const char* at = find("-EMAIL"))
        CopyValue(at + 6, GameString(0x007CCA90), 255);
    if (const char* at = find("-PPASS"))
        CopyValue(at + 6, GameString(0x007CCB90), 15);
    if (!GameVariable<bool>(0x007CC260)) {
        // The server to join: its name, and its IPv4 address (as inet_addr gives it - INADDR_NONE if it isn't one
        // and the name isn't found either). (The value starts 9 characters on, one past "-CONNECT": the
        // character after it is skipped, normally a space.)
        if (const char* at = find("-CONNECT")) {
            char* server = GameString(0x007CC980);
            CopyValue(at + 9, server, 255);
            uint32_t address = inet_addr(server);
            if (address == INADDR_NONE) {
                if (const hostent* host = gethostbyname(server))
                    std::memcpy(&address, host->h_addr_list[0], sizeof(address));
            }
            GameVariable<uint32_t>(0x0067EFB8) = address;
            GameVariable<bool>(0x007CCA80) = true; // joining a game
        }
    }
    // (The numbers are compared unsigned: a negative one counts as too big.)
    if (const char* at = find("-MAXPLAYERS")) {
        auto players = static_cast<uint32_t>(Number(at + 11));
        GameVariable<uint32_t>(0x0067EFD0) = players < 2 ? 2 : players > 8 ? 8 : players;
    }
    if (const char* at = find("-GROUP"))
        GameVariable<int32_t>(0x007CC264) = Number(at + 6);
    if (const char* at = find("-NTEAMS")) {
        auto teams = static_cast<uint32_t>(Number(at + 7));
        GameVariable<uint32_t>(0x007CCBE0) = teams > 4 ? 0 : teams;
    }
    if (const char* at = find("-SKILLLEVEL")) {
        auto skill = static_cast<uint32_t>(Number(at + 11));
        GameVariable<uint32_t>(0x0067EFD4) = skill > 4 ? 4 : skill;
    }
    if (const char* at = find("-NAME"))
        CopyValue(at + 5, GameString(0x007CCBC4), 15); // the player's name
    if (const char* at = find("-PASSWORD")) {
        char* password = GameString(0x007CCBD4);
        CopyValue(at + 9, password, 9);
        if (GameVariable<bool>(0x007CCA80)) {
            // Joining: it's the server's password (moved to where the join looks for it).
            std::memcpy(GameString(0x007CCDFC), password, std::strlen(password) + 1); // (strcpy)
            std::memset(password, 0, 10);
        }
    }
}

// 0x004E2680
int WINAPI WinMain(HINSTANCE instance, HINSTANCE /*previous*/, char* commandLine, int /*show*/)
{
    // (THUG2PM.exe's own patch loads a DLL here - LoadLibraryA(<the name at 0x0064B198>) in a code cave (0x005480A1)
    // called in place of GsiStartAvailableCheck, which the cave then jumps to. The PC port is part of the game's
    // source here, so nothing is loaded.)
    GsiStartAvailableCheck("thps6pc");
    int available;
    while ((available = GsiAvailableCheckThink()) == 0)
        Msleep(5);
    if (available != 1)
        Dbg::Printf("The backend is not available\n");

    // PC port: its set-up (thug2-decomp.ini, logging, the patched scripts, the SDL event pump, the online service's
    // hosts, the button glyphs, the graphics settings) before the command line is read (the PC port called it in
    // place of ParseNetworkParams, then that).
    InitPcSetup();
    ParseNetworkParams(commandLine);

    // The window title (the string at 0x0064B190, 25 characters - "...nd 2" - of which the PC port's exe patch
    // leaves "THUG2"): if a window has it, the game is running already - that one is brought to the front.
    const char kTitle[] = "THUG2";
    if (HWND running = FindWindowExA(nullptr, nullptr, nullptr, kTitle)) {
        ShowWindow(running, SW_RESTORE);
        BringWindowToTop(running);
        SetForegroundWindow(running);
        return 0;
    }

    // PC port: the SDL window (thug2-decomp.ini's size, windowed or not; the handle into 0x007D6A28) in place of
    //     char title[26] = <the title>;
    //     CreateGameWindow(<the title> as the class name, title, 100, 100, 640, 480, WindowProc 0x004E16B0,
    //                      instance, icon 0x66, true);                       // 0x005F5860 (true: full screen)
    CreateSdlWindow();
    Platform::ProcessWindowEvents();
    if (!GameWindow())
        return 0;
    // PC port: the window isn't shown here (SDL shows it). The original did
    //     ShowWindow(GameWindow(), GameVariable<bool>(0x00786AB7) ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
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

    // PC port: Main doesn't come back (FastExit ends the process when the main loop returns). The original
    // went on to shut down:
    ReleaseDirectSound();
    // PC port: DirectInput isn't released here (SDL input replaces it): the original called 0x005F39E0.
    GameFunction<void(__cdecl*)()>(0x004908C0)(); // (the renderer's clean-up)
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
