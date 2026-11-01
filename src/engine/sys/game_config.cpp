// The game's own configuration: the command line, the registry's language, the names (see game_config.h).

#include "engine/sys/game_config.h"

#include <windows.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/script/cfuncs_scene.h"

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

char g_path_prefix[128];
char g_bootstrap_argument[128];
char g_bootstrap_return[128];

char* path_prefix() { return g_path_prefix; }               // (0x80 bytes)
char* bootstrap_return() { return g_bootstrap_return; }     // where a bootstrap demo returns to
char* bootstrap_argument() { return g_bootstrap_argument; } // what it passes back
const char* const kEmpty = "";                              // ""

// strcpy (unbounded, as the original's).
void copy_string(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The C runtime's tolower (C locale).
char get_lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

} // namespace

// 0x005B5C90
bool argument_starts_with(const char* argument, const char* option)
{
    char copy[100];
    if (!argument || !option)
        return false;
    const size_t length = std::strlen(option);
    if (std::strlen(argument) < length)
        return false;
    copy_string(copy, argument);
    copy[length] = '\0';
    return std::strcmp(copy, option) == 0;
}

// 0x005B5D30
const char* get_argument_value(const char* option, int argc, char** argv)
{
    const char* value = nullptr;
    for (int i = 0; i < argc; ++i) {
        if (!argument_starts_with(argv[i], option))
            continue;
        const char* after = argv[i] + std::strlen(option);
        if (*after == '=') {
            if (after[1])
                value = after + 1;
            else if (i < argc - 1)
                value = argv[i + 1];
        } else if (*after == '\0' && i < argc - 1) {
            const char* next = argv[i + 1];
            if (*next == '=') {
                if (next[1])
                    value = next + 1;
                else if (i < argc - 2)
                    value = argv[i + 2];
            }
        }
    }
    return value;
}

// 0x005B5DD0
bool got_argument(const char* option, int argc, char** argv)
{
    char wanted[256];
    char argument[256];
    if (!option)
        return false;
    for (size_t i = 0; i < std::strlen(option) + 1; ++i)
        wanted[i] = get_lower(option[i]);
    for (int i = 0; i < argc; ++i) {
        for (size_t j = 0; j < std::strlen(argv[i]) + 1; ++j)
            argument[j] = get_lower(argv[i][j]);
        if (std::strcmp(argument, wanted) == 0)
            return true;
    }
    return false;
}

// 0x005B5F10
const char* get_path_prefix() { return path_prefix(); }

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
void prepare_all(int argc, char** argv)
{
    copy_string(path_prefix(), kEmpty);
    read_registry(argc, argv);
    if (got_argument("CD", argc, argv)) // "CD"
        from_cd() = true;
    if (got_argument("NotCD", argc, argv)) // "NotCD"
        from_cd() = false;
    if (got_argument("NoExtraMem", argc, argv)) // "NoExtraMem"
        get_extra_memory() = false;
    if (got_argument("Bootstrap", argc, argv)) { // "Bootstrap"
        bootstrap() = true;
        if (argc > 2) {
            copy_string(bootstrap_return(), argv[2]);
            if (argc > 3)
                copy_string(bootstrap_argument(), argv[3]);
            else
                copy_string(bootstrap_argument(), "THUG2"); // "THUG2" (the original's sprintf)
            // "This bootstrap demo will return to %s passing back: %s\n"
            debug::printf("This bootstrap demo will return to %s passing back: %s\n", bootstrap_return(),
                          bootstrap_argument());
        }
    }
    if (got_argument("NotBootstrap", argc, argv)) // "NotBootstrap"
        bootstrap() = false;
    if (got_argument("Pal", argc, argv)) // "Pal"
        display_type() = 0;
    if (got_argument("NTSC", argc, argv)) // "NTSC"
        display_type() = 1;
    if (const char* rate = get_argument_value("FrameRate", argc, argv)) { // "FrameRate"
        if (std::memcmp(rate, "50", 3) == 0)
            frame_rate() = 50;
        if (std::memcmp(rate, "60", 3) == 0)
            frame_rate() = 60;
    }
}

// 0x005B6250
const char* get_bootstrap_prefix() { return bootstrap() ? "THUG2\\" : kEmpty; } // "THUG2\"

// 0x005B6270
void read_registry(int, char**)
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
        RegQueryValueExA(key, "language", nullptr, &type, reinterpret_cast<BYTE*>(&language), &size); // "language"
        RegCloseKey(key);
    }
    if (language == 0 && RegCreateKeyA(HKEY_LOCAL_MACHINE, path, &key) == ERROR_SUCCESS) {
        size = 4;
        RegQueryValueExA(key, "language", nullptr, &type, reinterpret_cast<BYTE*>(&language), &size);
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
