// The game's own configuration: the command line, the registry's language, the names (see game_config.h).

#include "engine/sys/game_config.h"

#include <windows.h>

#include <cctype>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string_view>
#include <array>

#include "engine/core/dbg.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/core/checked_index.h"
#include "platform/registry.h"

namespace game_config {

bool g_from_cd{true};
int g_frame_rate{60};
int g_language;
namespace {
int g_territory;
} // namespace
int g_platform;
bool g_config_bootstrap;
namespace {
bool g_ntsc_flag;
} // namespace

int& get_language() { return g_language; }
int& territory() { return g_territory; }
int& hardware() { return g_platform; }
int& display_type() { return commands::g_config_ntsc; }
int& frame_rate() { return g_frame_rate; }
bool& from_cd() { return g_from_cd; }
bool& get_extra_memory() { return commands::g_got_extra_memory; }
bool& bootstrap() { return g_config_bootstrap; }
bool& ntsc_flag() { return g_ntsc_flag; }

namespace {

std::array<char, 128> g_path_prefix;
std::array<char, 128> g_bootstrap_argument;
std::array<char, 128> g_bootstrap_return;

std::array<char, 128>& path_prefix() { return g_path_prefix; }               // (0x80 bytes)
std::array<char, 128>& bootstrap_return() { return g_bootstrap_return; }     // where a bootstrap demo returns to
std::array<char, 128>& bootstrap_argument() { return g_bootstrap_argument; } // what it passes back
const char* const kEmpty = "";                              // ""

// strcpy (unbounded, as the original's).
void copy_string(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The C runtime's tolower (C locale).
char get_lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

} // namespace

// 0x005B5C90
bool argument_starts_with(const char* argument, const char* option)
{
    std::array<char, 100> copy{};
    if (!argument || !option)
        return false;
    const size_t length = std::strlen(option);
    if (std::strlen(argument) < length)
        return false;
    copy_string(copy.data(), argument);
    core::at(copy, length) = '\0';
    return std::strcmp(copy.data(), option) == 0;
}

// 0x005B5D30
const char* get_argument_value(const char* option, Arguments args)
{
    const char* value = nullptr;
    for (size_t i = 0; i < args.size(); ++i) {
        if (!argument_starts_with(core::at(args, i), option))
            continue;
        const char* after = std::next(core::at(args, i), static_cast<std::ptrdiff_t>(std::strlen(option)));
        const bool has_next = i + 1 < args.size();
        if (*after == '=') {
            if (*std::next(after))
                value = std::next(after);
            else if (has_next)
                value = core::at(args, i + 1);
        } else if (*after == '\0' && has_next) {
            const char* next = core::at(args, i + 1);
            if (*next == '=') {
                if (*std::next(next))
                    value = std::next(next);
                else if (i + 2 < args.size())
                    value = core::at(args, i + 2);
            }
        }
    }
    return value;
}

namespace {
// `text` in lower case, with its 0, in `out` (a text of 256 characters or more ends the game with a report, where the
// original wrote past its buffer).
void lower_into(std::array<char, 256>& out, const char* text)
{
    const std::string_view view(text);
    for (size_t i = 0; i < view.size(); ++i)
        core::at(out, i) = get_lower(core::at(view, i));
    core::at(out, view.size()) = get_lower('\0');
}
} // namespace

// 0x005B5DD0
bool got_argument(const char* option, Arguments args)
{
    std::array<char, 256> wanted{};
    std::array<char, 256> argument{};
    if (!option)
        return false;
    lower_into(wanted, option);
    for (const char* word : args) {
        lower_into(argument, word);
        if (std::strcmp(argument.data(), wanted.data()) == 0)
            return true;
    }
    return false;
}

// 0x005B5F10
const char* get_path_prefix() { return path_prefix().data(); }

// 0x005B5F20
const char* get_locale_label()
{
    switch (get_language()) {
    case 1:
        return "Japanese"; // "Japanese"
    case 2:
        return "English"; // "English"
    case 3:
        return "French"; // "French"
    case 4:
        return "Spanish"; // "Spanish"
    case 5:
        return "German"; // "German"
    case 6:
        return "Italian"; // "Italian"
    case 7:
        return "Dutch"; // "Dutch"
    case 8:
        return "Portuguese"; // "Portuguese"
    default:
        return "Unknown language"; // "Unknown language"
    }
}

// 0x005B5F90
const char* get_region_label()
{
    switch (territory()) {
    case 1:
        return "Europe"; // "Europe"
    case 2:
        return "US"; // "US"
    case 3:
        return "Asia"; // "Asia"
    default:
        return "Unknown territory"; // "Unknown territory"
    }
}

// 0x005B5FC0
const char* get_platform_label()
{
    switch (hardware()) {
    case 1:
        return "PS2"; // "PS2"
    case 2:
        return "PS2 ProView"; // "PS2 ProView"
    case 3:
        return "PS2 Dev System"; // "PS2 Dev System"
    case 4:
        return "XBox"; // "XBox"
    case 5:
        return "GameCube"; // "GameCube"
    case 7:
        return "Win32"; // "Win32"
    default:
        return "Unknown platform"; // "Unknown platform"
    }
}

// 0x005B6020
const char* get_screen_mode_label()
{
    switch (display_type()) {
    case 0:
        return "PAL"; // "PAL"
    case 1:
        return "NTSC"; // "NTSC"
    default:
        return "Unknown display type"; // "Unknown display type"
    }
}

// 0x005B6040
const char* fetch_platform_suffix()
{
    switch (hardware()) {
    case 1:
    case 2:
    case 3:
        return "ps2"; // "ps2"
    case 4:
        return "xbx"; // "xbx"
    case 5:
        return "ngc"; // "ngc"
    case 6:
        return "xen"; // "xen"
    default:
        return "???"; // "???"
    }
}

// 0x005B6090
void set_ntsc_flag(bool on)
{
    if (display_type() == 1)
        ntsc_flag() = on;
}

// 0x005B60B0
void prepare_all(Arguments args)
{
    copy_string(path_prefix().data(), kEmpty);
    read_registry(args);
    if (got_argument("CD", args)) // "CD"
        from_cd() = true;
    if (got_argument("NotCD", args)) // "NotCD"
        from_cd() = false;
    if (got_argument("NoExtraMem", args)) // "NoExtraMem"
        get_extra_memory() = false;
    if (got_argument("Bootstrap", args)) { // "Bootstrap"
        bootstrap() = true;
        if (args.size() > 2) {
            copy_string(bootstrap_return().data(), core::at(args, 2));
            if (args.size() > 3)
                copy_string(bootstrap_argument().data(), core::at(args, 3));
            else
                copy_string(bootstrap_argument().data(), "THUG2"); // "THUG2" (the original's sprintf)
            // "This bootstrap demo will return to %s passing back: %s\n"
            debug::printf("This bootstrap demo will return to %s passing back: %s\n", bootstrap_return().data(),
                          bootstrap_argument().data());
        }
    }
    if (got_argument("NotBootstrap", args)) // "NotBootstrap"
        bootstrap() = false;
    if (got_argument("Pal", args)) // "Pal"
        display_type() = 0;
    if (got_argument("NTSC", args)) // "NTSC"
        display_type() = 1;
    if (const char* rate = get_argument_value("FrameRate", args)) { // "FrameRate"
        if (std::memcmp(rate, "50", 3) == 0)
            frame_rate() = 50;
        if (std::memcmp(rate, "60", 3) == 0)
            frame_rate() = 60;
    }
}

// 0x005B6250
const char* get_bootstrap_prefix() { return bootstrap() ? "THUG2\\" : kEmpty; } // "THUG2\"

// 0x005B6270
void read_registry(Arguments)
{
    const char* path =
        R"(Software\Activision\Tony Hawk's Underground 2\Install)"; // "Software\Activision\Tony Hawk's Underground 2\Install"
    hardware() = 7;
    get_language() = 2;
    get_extra_memory() = false;
    DWORD language = 0;
    HKEY key = nullptr;
    DWORD type = 0;
    DWORD size = 0;
    if (RegCreateKeyA(HKEY_CURRENT_USER, path, &key) == ERROR_SUCCESS) {
        size = 4;
        sys::query_registry_value(key, "language", &type, &language, &size); // "language"
        RegCloseKey(key);
    }
    if (language == 0 && RegCreateKeyA(HKEY_LOCAL_MACHINE, path, &key) == ERROR_SUCCESS) {
        size = 4;
        sys::query_registry_value(key, "language", &type, &language, &size);
        RegCloseKey(key);
    }
    switch (language) {
    case 2:
        get_language() = 3;
        break; // French
    case 3:
        get_language() = 5;
        break; // German
    default:
        get_language() = 2;
        break;
    }
}

} // namespace game_config
