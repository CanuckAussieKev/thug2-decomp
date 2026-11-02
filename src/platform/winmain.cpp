// The program's entry point and its command line.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr, gethostbyname: what the game uses
#include "platform/winmain.h"
#include "platform/gamespy.h"

#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/net/gamenet_qr2.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/online_identity.h"
#include "engine/pad_input/device.h"
#include "platform/run_log.h"
#include "engine/sys/startup.h"
#include "platform/pc_setup.h"
#include "platform/platform.h"
#include "platform/platform_d3d.h"
#include "platform/platform_input.h"
#include "platform/platform_system.h"
#include "platform/crt.h"
#include "platform/registry.h"
#include "platform/sdl_window.h"
#include "engine/gfx/loading_console.h"
#include "engine/gfx/device.h"
#include "engine/gfx/movie.h"
#include "engine/script/game_cfuncs.h"
#include "engine/core/parse_number.h"
#include "engine/core/bits_as.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/checked_index.h"

// The command line's values, kept in globals other files own (the e-mail and the password: buddy_status.cpp, the
// player's name and password: net_state_cfuncs.cpp).

namespace sys {

namespace {
std::array<char, 256> g_command_line_server_name; // "-connect"'s value

// --- the command line's network options ----------------------------------------------------------------------

// A value: after any spaces, up to the next space - or, if it starts with a quote, up to the closing quote - at
// most `capacity` characters, then terminated.
void copy_value(std::string_view from, std::span<char> to, unsigned capacity)
{
    size_t at = 0;
    auto peek = [&]() { return at < from.size() ? core::at(from, at) : '\0'; };
    while (peek() == ' ')
        ++at;
    char end = ' ';
    if (peek() == '"') {
        end = '"';
        ++at;
    }
    unsigned length = 0;
    for (;;) {
        const char c = peek();
        ++at;
        if (c == '\0' || c == end)
            break;
        core::at(to, length++) = c;
        if (length >= capacity)
            break;
    }
    core::at(to, length) = '\0';
}

// A number: after any spaces, as atol reads it. (`from` is the end of the NUL-terminated command line.)
long get_number(std::string_view from)
{
    size_t at = 0;
    while (at < from.size() && core::at(from, at) == ' ')
        ++at;
    return text::to_int(std::string(from.substr(at)).c_str());
}

// `text` without its first `count` characters (an option's own name): empty at the line's end.
std::string_view after(std::string_view text, size_t count) { return text.substr(std::min(count, text.size())); }

// --- WinMain's callees -------------------------------------------------------------------------------------------

HWND game_window() { return render::g_game_window; }

// GameSpy (library code, not decompiled): is the game's back end up?
void gsi_start_available_check(const char* game) { game_spy::start_available_check(game); }
int gsi_available_check_think() { return game_spy::available_check_think(); } // 0: still waiting, 1: up
void get_msleep(unsigned milliseconds) { game_spy::get_msleep(milliseconds); }

// The game's C runtime: its rand() is the one the game's code draws from, and its exit functions run the game's
// own clean-up.
unsigned get_instant() { return crt::get_instant(); }
void get_srand(unsigned seed) { crt::get_srand(seed); }
void c_exit() { crt::c_exit(); }                              // _cexit: atexit functions, streams flushed
[[noreturn]] void exit_now(int code) { crt::exit_now(code); } // _exit

// The platform layer's parts without source yet.
char find_cd_drive(const char* volume) { return os_layer::find_cd_drive(volume); }
void init_direct_sound(HWND window) { os_layer::init_direct_sound(window); }
void release_direct_sound() { os_layer::release_direct_sound(); }
void release_direct_3d() { os_layer::release_direct_3d(); }
// A key's name (at most 14 characters), by its scan code (the table at 0x007D42F8).
void set_key_name(unsigned scan_code, const wchar_t* name)
{
    os_layer::set_key_name(static_cast<int>(scan_code), name);
}

constexpr auto kSettingsKey = std::to_array(R"(Software\Activision\Tony Hawk's Underground 2\Settings)");

// The default bindings, written to the settings unless already there: k0_0..k0_23 (the keyboard: DirectInput
// key codes - W S A D, Z X C V, the number pad 2 6 4 8, none, Tab, the number pad 1 3 7 9 0 and its point, Home,
// End, Delete, Page Down), gp0_0..gp0_23 (the first pad) and gp1_0..gp1_23 (the second).
const std::array<const char*, 3> kBindingPrefixes = {{"k0_", "gp0_", "gp1_"}};
const std::array<std::array<uint32_t, 24>, 3> kDefaultBindings = {{
    {0x11,       0x1F, 0x1E, 0x20, 0x2C, 0x2D, 0x2E, 0x2F, 0x50, 0x4D, 0x4B, 0x48,
     0x0FFFFFFF, 0x0F, 0x4F, 0x51, 0x47, 0x49, 0x52, 0x53, 0xC7, 0xCF, 0xD3, 0xD1},
    {2, 2, 1, 1, 0x0B, 0x0C, 0x0D, 0x0E},
    {},
}};

} // namespace

bool g_internet_game_host;
bool g_joining_internet_game;
bool g_app_active;

// 0x004E16B0
LRESULT CALLBACK get_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (message) {
    case WM_DESTROY:
    case WM_CLOSE:
        PostQuitMessage(0);
        render::g_quit_requested = true; // quit
        return 0;
    case WM_ACTIVATE:
        // (the game's window, not the message's)
        if (LOWORD(w_param) == WA_ACTIVE || LOWORD(w_param) == WA_CLICKACTIVE) {
            ShowWindow(game_window(), SW_MAXIMIZE);
            g_app_active = true; // active
        } else {
            ShowWindow(game_window(), SW_MINIMIZE);
            g_app_active = false;
            render::g_device_lost = true; // the device is taken as lost (reset when active again)
            InvalidateRect(nullptr, nullptr, TRUE);
        }
        return 0;
    case WM_PAINT:
        ValidateRect(window, nullptr);
        return 0;
    case WM_KEYDOWN:
        sio::special_key_input(static_cast<int>(w_param), static_cast<int>(l_param));
        return 0;
    case WM_CHAR:
        sio::key_input(static_cast<int>(w_param), static_cast<int>(l_param));
        return 0;
    case WM_SYSKEYDOWN:
        if (w_param == VK_F10) // (not the menu)
            return 0;
        break;
    case WM_SYSCOMMAND:
        if (w_param == SC_KEYMENU) // (no menu from Alt)
            return 0;
        break;
    case WM_POWERBROADCAST:
        return 0;
    }
    return DefWindowProcA(window, message, w_param, l_param);
}

// 0x004E1220
void parse_network_params(const char* command_line) { read_network_params(command_line); }

void read_network_params(const char* command_line)
{
    // The options are looked for in an upper-case copy; their values are read from the command line itself.
    // (The original copies it whole into its 1024 bytes - a longer command line overran its stack; ours looks
    // at the first 1023 characters.)
    std::array<char, 1024> upper{};
    strncpy_s(upper.data(), upper.size(), command_line, _TRUNCATE);
    _strupr_s(upper.data(), upper.size());
    const std::string_view line(command_line);
    const std::string_view upper_line(upper.data());
    // The command line from the option on, or none.
    auto find = [&](std::string_view option) -> std::optional<std::string_view> {
        const size_t at = upper_line.find(option);
        if (at == std::string_view::npos)
            return std::nullopt;
        return line.substr(at);
    };

    if (find("-HOST "))
        g_internet_game_host = true; // hosting a game
    if (const auto at = find("-HOSTNAME"))
        copy_value(after(*at, 9), commands::g_hosted_game_name, 15); // the hosted game's name
    if (const auto at = find("-EMAIL"))
        copy_value(after(*at, 6), online::g_command_line_email, 255);
    if (const auto at = find("-PPASS"))
        copy_value(after(*at, 6), online::g_command_line_password, 15);
    if (!g_internet_game_host) {
        // The server to join: its name, and its IPv4 address (as inet_addr gives it - INADDR_NONE if it isn't one
        // and the name isn't found either). (The value starts 9 characters on, one past "-CONNECT": the
        // character after it is skipped, normally a space.)
        if (const auto at = find("-CONNECT")) {
            const char* server = g_command_line_server_name.data();
            copy_value(after(*at, 9), g_command_line_server_name, 255);
            uint32_t address = inet_addr(server);
            if (address == INADDR_NONE) {
                if (const hostent* host = gethostbyname(server))
                    std::memcpy(&address, *host->h_addr_list, sizeof(address));
            }
            online::g_command_line_server = address;
            g_joining_internet_game = true; // joining a game
        }
    }
    // (The numbers are compared unsigned: a negative one counts as too big.)
    if (const auto at = find("-MAXPLAYERS")) {
        auto players = static_cast<uint32_t>(get_number(after(*at, 11)));
        memory::bits_as<uint32_t>(online::g_net_max_players) = players < 2 ? 2 : players > 8 ? 8 : players;
    }
    if (const auto at = find("-GROUP"))
        online::g_lobby_group_id = get_number(after(*at, 6));
    if (const auto at = find("-NTEAMS")) {
        auto teams = static_cast<uint32_t>(get_number(after(*at, 7)));
        online::g_network_team_mode = teams > 4 ? 0 : teams;
    }
    if (const auto at = find("-SKILLLEVEL")) {
        auto skill = static_cast<uint32_t>(get_number(after(*at, 11)));
        memory::bits_as<uint32_t>(online::g_net_skill_level) = skill > 4 ? 4 : skill;
    }
    if (const auto at = find("-NAME"))
        copy_value(after(*at, 5), commands::g_network_id, 15); // the player's name
    if (const auto at = find("-PASSWORD")) {
        char* password = commands::g_network_password.data();
        copy_value(after(*at, 9), commands::g_network_password, 9);
        if (g_joining_internet_game) {
            // Joining: it's the server's password (moved to where the join looks for it).
            std::memcpy(online::g_pending_password.data(), password, std::strlen(password) + 1); // (strcpy)
            std::memset(password, 0, 10);
        }
    }
}

// 0x004E2680
bool& allow_multiple_instances()
{
    static bool allow = config_int("features", "allow_multiple_instances", 0) != 0;
    return allow;
}

namespace {
std::string& startup_command_line_copy()
{
    static std::string value;
    return value;
}
} // namespace

const std::string& startup_command_line() { return startup_command_line_copy(); }

int WINAPI win_main(HINSTANCE instance, HINSTANCE /*previous*/, char* command_line, int /*show*/)
{
    startup_command_line_copy() = GetCommandLineA(); // (before anything parses it)
    // (the reference build's own patch loads a DLL here - LoadLibraryA(<the name at 0x0064B198>) in a code cave (0x005480A1)
    // called in place of gsi_start_available_check, which the cave then jumps to. PartyMod is part of the game's
    // source here, so nothing is loaded.)
    // (the game name: "thps6pc", or [Miscellaneous] OnlineGameName - engine/net/online_identity.h)
    gsi_start_available_check(net::get_game_name());
    int available = 0;
    while ((available = gsi_available_check_think()) == 0)
        get_msleep(5);
    if (available != 1)
        debug::printf("The backend is not available\n");

    // Originally from PartyMod Fixes: its set-up (thug2-decomp.ini, logging, the patched scripts, the SDL event pump, the online service's
    // hosts, the button glyphs, the graphics settings) before the command line is read (PartyMod called it in
    // place of parse_network_params, then that).
    init_pc_setup();
    parse_network_params(command_line);

    // The window title (the string at 0x0064B190, 25 characters - "...nd 2" - of which PartyMod's exe patch
    // leaves "THUG2"): if a window has it, the game is running already - that one is brought to the front.
    // (thug2-decomp.ini [features] allow_multiple_instances=1, not in the original: skip this check so a second copy
    // can run - e.g. to host and join a LAN game on one PC. Its socket falls back to a free port when the game's
    // port is taken (AppHost::Bind) and its log goes to thug2-decomp-2.log.)
    const auto title = std::to_array("THUG2");
    if (HWND running = allow_multiple_instances() ? nullptr : FindWindowExA(nullptr, nullptr, nullptr, title.data())) {
        ShowWindow(running, SW_RESTORE);
        BringWindowToTop(running);
        SetForegroundWindow(running);
        return 0;
    }

    // Originally from PartyMod Fixes: the SDL window (thug2-decomp.ini's size, windowed or not; the handle into 0x007D6A28) in place of
    //     char title[26] = <the title>;
    //     create_game_window(<the title> as the class name, title, 100, 100, 640, 480, get_window_proc 0x004E16B0,
    //                      instance, icon 0x66, true);                       // 0x005F5860 (true: full screen)
    create_sdl_window();
    os_layer::process_window_events();
    if (!game_window())
        return 0;
    // Originally from PartyMod Fixes: the window isn't shown here (SDL shows it). The original did
    //     ShowWindow(game_window(), render::g_show_window_flag ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    get_srand(get_instant());

    // The command line split at spaces (made terminators) into at most 20 arguments. The first is the command
    // line's first word - not the program's name.
    std::array<char*, 21> argv{};
    int argc = 1;
    core::at(argv, 0) = command_line;
    const std::span<char> line(command_line, std::strlen(command_line) + 1);
    size_t at = 0;
    for (;;) {
        while (core::at(line, at) != '\0' && core::at(line, at) != ' ')
            ++at;
        while (core::at(line, at) == ' ')
            core::at(line, at++) = '\0';
        if (core::at(line, at) == '\0')
            break;
        core::at(argv, argc++) = line.subspan(at).data();
        if (argc >= 20)
            break;
    }
    core::at(argv, argc) = nullptr;

    find_cd_drive("THUG CD2");
    init_direct_sound(game_window());
    for (int device = 0; device < 3; ++device) {
        for (int i = 0; i < 24; ++i) {
            std::array<char, 8> name = {};
            std::snprintf(name.data(), sizeof(name), "%s%d", core::at(kBindingPrefixes, device), i);
            write_default_setting(name.data(), core::at(core::at(kDefaultBindings, device), i));
        }
    }
    os_layer::init_input(instance, game_window(), static_cast<int>(cell::of(kSettingsKey.data())));

    // The keys' names: the character each scan code types in the keyboard layout (an acute accent shown as an
    // apostrophe, a semicolon as a colon); the function keys by name.
    for (unsigned scan_code = 0; scan_code < 256; ++scan_code) {
        std::array<char, 2> name = {{static_cast<char>(MapVirtualKeyA(MapVirtualKeyA(scan_code, MAPVK_VSC_TO_VK), MAPVK_VK_TO_CHAR)),
                        0}};
        if (static_cast<uint8_t>(core::at(name, 0)) == 0xB4)
            core::at(name, 0) = '\'';
        else if (core::at(name, 0) == ';')
            core::at(name, 0) = ':';
        std::array<wchar_t, 10> wide{};
        size_t converted = 0;
        mbstowcs_s(&converted, wide.data(), wide.size(), name.data(), _TRUNCATE);
        set_key_name(scan_code, wide.data());
    }
    set_key_name(0x3B, L"F1");
    set_key_name(0x3C, L"F2");
    set_key_name(0x3D, L"F3");
    set_key_name(0x3E, L"F4");
    set_key_name(0x3F, L"F5");
    set_key_name(0x40, L"F6");
    set_key_name(0x41, L"F7");
    set_key_name(0x42, L"F8");
    set_key_name(0x43, L"F9");
    set_key_name(0x44, L"F10");
    set_key_name(0x57, L"F11");
    set_key_name(0x58, L"F12");
    set_key_name(0x64, L"F13");
    set_key_name(0x65, L"F14");
    set_key_name(0x66, L"F15");

    game_main(argc, argv.data());

    // Originally from PartyMod Fixes: Main doesn't come back (fast_exit ends the process when the main loop returns). The original
    // went on to shut down:
    release_direct_sound();
    // Originally from PartyMod Fixes: DirectInput isn't released here (SDL input replaces it): the original called 0x005F39E0.
    gfx::loading_console::screen_hidden(); // (not original: the loading console's timer stopped)
    render::remove_loading_screen();       // (the renderer's clean-up)
    release_direct_3d();
    InvalidateRect(nullptr, nullptr, TRUE);
    ShowWindow(game_window(), SW_SHOWMINIMIZED);
    c_exit();
    memory::shut_down_manager();
    exit_now(0);
}

} // namespace sys
