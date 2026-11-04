// The program's entry point and its command line.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "app/winmain.h"
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

#include <SDL2/SDL.h>

#include "base/core/dbg.h"
#include "engine/audio/audio_device.h"
#include "engine/audio/music_player.h"
#include "engine/memory/manager.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "game/online/gamenet_qr2.h"
#include "game/online/gamenet_manager.h"
#include "game/online/online_identity.h"
#include "engine/pad_input/device.h"
#include "platform/run_log.h"
#include "app/startup.h"
#include "app/pc_setup.h"
#include "engine/pad_input/input_setup.h"
#include "render_d3d9/device.h"
#include "engine/pad_input/port_controls.h"
#include "platform/platform_system.h"
#include "platform/process_flags.h"
#include "platform/crt.h"
#include "platform/registry.h"
#include "platform/sdl_events.h"
#include "platform/sdl_window.h"
#include "engine/gfx/loading_console.h"
#include "game/commands/game_cfuncs.h"
#include "devtools/automation.h"
#include "base/core/parse_number.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/engine_parts.h"
#include "game/online/net_game_state.h"
#include "game/online/player_info.h"

// The command line's values, kept in globals other files own (the e-mail and the password: buddy_status.cpp, the
// player's name and password: net_state_cfuncs.cpp).

namespace sys {

// The command line: where each option's value starts (past the option's name and the separator after it) and how
// long a value may be.
constexpr int kHostNameValueAt = 9;
constexpr int kEmailValueAt = 6;
constexpr int kPpassValueAt = 6;
constexpr int kConnectValueAt = 9;
constexpr int kMaxPlayersValueAt = 11;
constexpr int kGroupValueAt = 6;
constexpr int kTeamsValueAt = 7;
constexpr int kSkillLevelValueAt = 11;
constexpr int kNameValueAt = 5;
constexpr int kPasswordValueAt = 9;
constexpr int kHostedGameNameMax = 15;
constexpr int kEmailMax = 255;
constexpr int kCommandPasswordMax = 15;
constexpr int kServerNameMax = 255;
constexpr int kPlayerNameMax = 15;
constexpr int kNetworkPasswordMax = 9;
constexpr int kNetworkPasswordBytes = 10;
constexpr std::size_t kArgvSlots = 21;
constexpr int kMaxArguments = 20;
constexpr std::size_t kServerNameSize = 256;
constexpr std::size_t kCommandLineCopySize = 1024;
constexpr int kAvailableCheckSleepMs = 5; // between the availability check's thinks
// The input bindings and the keys' names.
constexpr std::size_t kBindingNameSize = 8;
constexpr int kScanCodeCount = 256;
constexpr int kAcuteAccent = 0xB4;
constexpr std::size_t kKeyNameWideSize = 10;
constexpr int kScanF1 = 0x3B; // F1-F10 follow one another
constexpr int kScanF2 = 0x3C;
constexpr int kScanF3 = 0x3D;
constexpr int kScanF4 = 0x3E;
constexpr int kScanF5 = 0x3F;
constexpr int kScanF6 = 0x40;
constexpr int kScanF7 = 0x41;
constexpr int kScanF8 = 0x42;
constexpr int kScanF9 = 0x43;
constexpr int kScanF10 = 0x44;
constexpr int kScanF11 = 0x57;
constexpr int kScanF12 = 0x58;
constexpr int kScanF13 = 0x64;
constexpr int kScanF14 = 0x65;
constexpr int kScanF15 = 0x66;

namespace {
std::array<char, kServerNameSize>& command_line_server_name()
{
    static std::array<char, kServerNameSize> command_line_server_name{}; // "-connect"'s value
    return command_line_server_name;
}

// --- the command line's network options ----------------------------------------------------------------------

// A value: after any spaces, up to the next space - or, if it starts with a quote, up to the closing quote - at
// most `capacity` characters, then terminated.
void copy_value(std::string_view from, std::span<char> to, unsigned capacity)
{
    size_t at = 0;
    auto peek = [&]() { return at < from.size() ? core::at(from, at) : '\0'; };
    while (peek() == ' ') {
        ++at;
    }
    char end = ' ';
    if (peek() == '"') {
        end = '"';
        ++at;
    }
    unsigned length = 0;
    for (;;) {
        const char character = peek();
        ++at;
        if (character == '\0' || character == end) {
            break;
        }
        core::at(to, length++) = character;
        if (length >= capacity) {
            break;
        }
    }
    core::at(to, length) = '\0';
}

// A number: after any spaces, as atol reads it. (`from` is the end of the NUL-terminated command line.)
long get_number(std::string_view from)
{
    size_t at = 0;
    while (at < from.size() && core::at(from, at) == ' ') {
        ++at;
    }
    return text::to_int(std::string(from.substr(at)).c_str());
}

// `text` without its first `count` characters (an option's own name): empty at the line's end.
std::string_view after(std::string_view text, size_t count) { return text.substr(std::min(count, text.size())); }

// --- WinMain's callees -------------------------------------------------------------------------------------------

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
// (the sound output - DirectSound's device, through the installed audio::Device - kept in the audio's state:
// audio::get_sound_device)
void init_direct_sound(HWND window) { audio::set_sound_device(audio::device().open(window)); }
void release_direct_sound()
{
    audio::device().close(audio::get_sound_device());
    audio::set_sound_device(nullptr);
}
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

// The SDL window's focus events (sdl_window.h): the game's focus flag (0x007CCBE4) and whether the device may be
// recreated (0x00786AB4: off while the window is in the background).
void handle_window_event(SDL_Event* event)
{
    if (event->type != SDL_WINDOWEVENT) {
        return;
    }
    if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        render::engine_parts().set_device_lost(false);
        // Not PartyMod's - with the test-automation channel on (devtools/automation.h) the game keeps drawing in
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

// 0x004E16B0
LRESULT CALLBACK get_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message) {
    case WM_DESTROY:
    case WM_CLOSE:
        PostQuitMessage(0);
        render::engine_parts().set_quit_requested(true); // quit
        return 0;
    case WM_ACTIVATE:
        // (the game's window, not the message's)
        if (LOWORD(w_param) == WA_ACTIVE || LOWORD(w_param) == WA_CLICKACTIVE) {
            ShowWindow(sys::game_window(), SW_MAXIMIZE);
            set_app_active(true); // active
        } else {
            ShowWindow(sys::game_window(), SW_MINIMIZE);
            set_app_active(false);
            render::engine_parts().set_device_lost(true); // the device is taken as lost (reset when active again)
            InvalidateRect(nullptr, nullptr, TRUE);
        }
        return 0;
    case WM_PAINT:
        ValidateRect(window, nullptr);
        return 0;
    case WM_KEYDOWN:
        sio::special_key_input({.key = static_cast<int>(w_param), .flags = static_cast<int>(l_param)});
        return 0;
    case WM_CHAR:
        sio::key_input({.key = static_cast<int>(w_param), .flags = static_cast<int>(l_param)});
        return 0;
    case WM_SYSKEYDOWN:
        if (w_param == VK_F10) { // (not the menu)
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if (w_param == SC_KEYMENU) { // (no menu from Alt)
            return 0;
        }
        break;
    case WM_POWERBROADCAST:
        return 0;
    default:
        break;
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
    std::array<char, kCommandLineCopySize> upper{};
    strncpy_s(upper.data(), upper.size(), command_line, _TRUNCATE);
    _strupr_s(upper.data(), upper.size());
    const std::string_view line(command_line);
    const std::string_view upper_line(upper.data());
    // The command line from the option on, or none.
    auto find = [&](std::string_view option) -> std::optional<std::string_view> {
        const size_t at = upper_line.find(option);
        if (at == std::string_view::npos) {
            return std::nullopt;
        }
        return line.substr(at);
    };

    if (find("-HOST ")) {
        set_internet_game_host(true); // hosting a game
    }
    if (const auto at = find("-HOSTNAME")) {
        copy_value(after(*at, kHostNameValueAt), commands::hosted_game_name(), kHostedGameNameMax); // the hosted game's name
    }
    if (const auto at = find("-EMAIL")) {
        copy_value(after(*at, kEmailValueAt), online::command_line_email(), kEmailMax);
    }
    if (const auto at = find("-PPASS")) {
        copy_value(after(*at, kPpassValueAt), online::command_line_password(), kCommandPasswordMax);
    }
    if (!internet_game_host()) {
        // The server to join: its name, and its IPv4 address (as inet_addr gives it - INADDR_NONE if it isn't one
        // and the name isn't found either). (The value starts 9 characters on, one past "-CONNECT": the
        // character after it is skipped, normally a space.)
        if (const auto at = find("-CONNECT")) {
            const char* server = command_line_server_name().data();
            copy_value(after(*at, kConnectValueAt), command_line_server_name(), kServerNameMax);
            uint32_t address = inet_addr(server);
            if (address == INADDR_NONE) {
                if (const hostent* host = gethostbyname(server)) {
                    std::memcpy(&address, *host->h_addr_list, sizeof(address));
                }
            }
            online::net_game_state().set_command_line_server(address);
            set_joining_internet_game(true); // joining a game
        }
    }
    // (The numbers are compared unsigned: a negative one counts as too big.)
    if (const auto at = find("-MAXPLAYERS")) {
        auto players = static_cast<uint32_t>(get_number(after(*at, kMaxPlayersValueAt)));
        const uint32_t at_most = std::min(players, static_cast<uint32_t>(online::kPlayerSlotCount));
        online::net_game_state().set_net_max_players(static_cast<int>(players < 2 ? 2U : at_most));
    }
    if (const auto at = find("-GROUP")) {
        online::set_lobby_group_id(get_number(after(*at, kGroupValueAt)));
    }
    if (const auto at = find("-NTEAMS")) {
        auto teams = static_cast<uint32_t>(get_number(after(*at, kTeamsValueAt)));
        online::net_game_state().set_network_team_mode(teams > 4 ? 0 : teams);
    }
    if (const auto at = find("-SKILLLEVEL")) {
        auto skill = static_cast<uint32_t>(get_number(after(*at, kSkillLevelValueAt)));
        online::net_game_state().set_net_skill_level(static_cast<int>(skill > 4 ? 4 : skill));
    }
    if (const auto at = find("-NAME")) {
        copy_value(after(*at, kNameValueAt), commands::network_id(), kPlayerNameMax); // the player's name
    }
    if (const auto at = find("-PASSWORD")) {
        char* password = commands::network_password().data();
        copy_value(after(*at, kPasswordValueAt), commands::network_password(), kNetworkPasswordMax);
        if (joining_internet_game()) {
            // Joining: it's the server's password (moved to where the join looks for it).
            std::memcpy(online::pending_password().data(), password, std::strlen(password) + 1); // (strcpy)
            std::memset(password, 0, kNetworkPasswordBytes);
        }
    }
}

// 0x004E2680
bool& allow_multiple_instances()
{
    static bool allow = config_int("features", "allow_multiple_instances", 0) != 0;
    return allow;
}

int WINAPI win_main(HINSTANCE instance, HINSTANCE /*previous*/, char* command_line, int /*show*/)
{
    set_startup_command_line(GetCommandLineA()); // (before anything parses it)
    // (the reference build's own patch loads a DLL here - LoadLibraryA(<the name at 0x0064B198>) in a code cave (0x005480A1)
    // called in place of gsi_start_available_check, which the cave then jumps to. PartyMod is part of the game's
    // source here, so nothing is loaded.)
    // (the game name: "thps6pc", or [Miscellaneous] OnlineGameName - game/online/online_identity.h)
    gsi_start_available_check(net::get_game_name());
    int available = 0;
    while ((available = gsi_available_check_think()) == 0) {
        get_msleep(kAvailableCheckSleepMs);
    }
    if (available != 1) {
        debug::printf("The backend is not available\n");
    }

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
    //     create_game_window(<the title> as the class name, title, 100, {.width = 100, .height = 640}, 480, get_window_proc 0x004E16B0,
    //                      instance, icon 0x66, true);                       // 0x005F5860 (true: full screen)
    os_layer::register_event_handler(handle_window_event);
    sys::game_window() = create_sdl_window();
    os_layer::process_window_events();
    if (!sys::game_window()) {
        return 0;
    }
    // Originally from PartyMod Fixes: the window isn't shown here (SDL shows it). The original did
    //     ShowWindow(sys::game_window(), render::kShowWindowFlag ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    get_srand(get_instant());

    // The command line split at spaces (made terminators) into at most 20 arguments. The first is the command
    // line's first word - not the program's name.
    std::array<char*, kArgvSlots> argv{};
    int argc = 1;
    core::at(argv, 0) = command_line;
    const std::span<char> line(command_line, std::strlen(command_line) + 1);
    size_t at = 0;
    for (;;) {
        while (core::at(line, at) != '\0' && core::at(line, at) != ' ') {
            ++at;
        }
        while (core::at(line, at) == ' ') {
            core::at(line, at++) = '\0';
        }
        if (core::at(line, at) == '\0') {
            break;
        }
        core::at(argv, argc++) = line.subspan(at).data();
        if (argc >= kMaxArguments) {
            break;
        }
    }
    core::at(argv, argc) = nullptr;

    find_cd_drive("THUG CD2");
    init_direct_sound(sys::game_window());
    for (int device = 0; device < 3; ++device) {
        for (int i = 0; i < os_layer::kBindings; ++i) {
            std::array<char, kBindingNameSize> name = {};
            text::format_to(name, "%s%d", core::at(kBindingPrefixes, device), i);
            write_default_setting(name.data(), core::at(core::at(kDefaultBindings, device), i));
        }
    }
    os_layer::init_input(instance, sys::game_window(), kSettingsKey.data());

    // The keys' names: the character each scan code types in the keyboard layout (an acute accent shown as an
    // apostrophe, a semicolon as a colon); the function keys by name.
    for (unsigned scan_code = 0; scan_code < kScanCodeCount; ++scan_code) {
        std::array<char, 2> name = {{static_cast<char>(MapVirtualKeyA(MapVirtualKeyA(scan_code, MAPVK_VSC_TO_VK), MAPVK_VK_TO_CHAR)),
                        0}};
        if (static_cast<uint8_t>(core::at(name, 0)) == kAcuteAccent) {
            core::at(name, 0) = '\'';
        } else if (core::at(name, 0) == ';') {
            core::at(name, 0) = ':';
        }
        std::array<wchar_t, kKeyNameWideSize> wide{};
        size_t converted = 0;
        mbstowcs_s(&converted, wide.data(), wide.size(), name.data(), _TRUNCATE);
        set_key_name(scan_code, wide.data());
    }
    set_key_name(kScanF1, L"F1");
    set_key_name(kScanF2, L"F2");
    set_key_name(kScanF3, L"F3");
    set_key_name(kScanF4, L"F4");
    set_key_name(kScanF5, L"F5");
    set_key_name(kScanF6, L"F6");
    set_key_name(kScanF7, L"F7");
    set_key_name(kScanF8, L"F8");
    set_key_name(kScanF9, L"F9");
    set_key_name(kScanF10, L"F10");
    set_key_name(kScanF11, L"F11");
    set_key_name(kScanF12, L"F12");
    set_key_name(kScanF13, L"F13");
    set_key_name(kScanF14, L"F14");
    set_key_name(kScanF15, L"F15");

    game_main(argc, argv.data());

    // Originally from PartyMod Fixes: Main doesn't come back (fast_exit ends the process when the main loop returns). The original
    // went on to shut down:
    release_direct_sound();
    // Originally from PartyMod Fixes: DirectInput isn't released here (SDL input replaces it): the original called 0x005F39E0.
    gfx::loading_console::screen_hidden(); // (not original: the loading console's timer stopped)
    render::remove_loading_screen();       // (the renderer's clean-up)
    release_direct_3d();
    InvalidateRect(nullptr, nullptr, TRUE);
    ShowWindow(sys::game_window(), SW_SHOWMINIMIZED);
    c_exit();
    memory::shut_down_manager();
    exit_now(0);
}

} // namespace sys
