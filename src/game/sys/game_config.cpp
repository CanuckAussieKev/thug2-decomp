// The game's own configuration: the command line, the registry's language, the names (see game_config.h).

#include "game/sys/game_config.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/script/cfuncs_scene.h"

namespace GameConfig {


GAME_GLOBAL(bool, g_fromCD, 0x00684FA8, true);
GAME_GLOBAL(int, g_frameRate, 0x00684FB4, 60);
GAME_GLOBAL(int, g_language, 0x007CEFA0);
GAME_GLOBAL(int, g_territory, 0x007CEFA4);
GAME_GLOBAL(int, g_platform, 0x007CEFA8);
GAME_GLOBAL(bool, g_configBootstrap, 0x007CEFAD);
GAME_GLOBAL(bool, g_ntscFlag, 0x007CEFAF);

int& Language() { return g_language; }
int& Territory() { return g_territory; }
int& Hardware() { return g_platform; }
int& DisplayType() { return CFuncs::g_configNtsc; }
int& FrameRate() { return g_frameRate; }
bool& FromCD() { return g_fromCD; }
bool& ExtraMemory() { return CFuncs::g_gotExtraMemory; }
bool& Bootstrap() { return g_configBootstrap; }
bool& NtscFlag() { return g_ntscFlag; }

namespace {

GAME_GLOBAL(char[128], g_pathPrefix, 0x007CEE20);
GAME_GLOBAL(char[128], g_bootstrapArgument, 0x007CEEA0);
GAME_GLOBAL(char[128], g_bootstrapReturn, 0x007CEF20);

char* PathPrefix() { return g_pathPrefix; }       // (0x80 bytes)
char* BootstrapReturn() { return g_bootstrapReturn; }  // where a bootstrap demo returns to
char* BootstrapArgument() { return g_bootstrapArgument; } // what it passes back
const char* const kEmpty = GAME_TEXT(0x006469B4, ""); // ""

// strcpy (unbounded, as the original's).
void CopyString(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The C runtime's tolower (C locale).
char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

} // namespace

// 0x005B5C90
bool ArgumentStartsWith(const char* argument, const char* option)
{
    char copy[100];
    if (!argument || !option)
        return false;
    size_t length = std::strlen(option);
    if (std::strlen(argument) < length)
        return false;
    CopyString(copy, argument);
    copy[length] = '\0';
    return std::strcmp(copy, option) == 0;
}

namespace {
bool __cdecl ArgumentStartsWithRegisters(const char* argument, const char* option)
{
    return ArgumentStartsWith(argument, option);
}
} // namespace

// 0x005B5C90: the argument in ECX, the option in EBX; the result in AL.
__declspec(naked) void ArgumentStartsWithStub()
{
    __asm {
        push ecx
        push edx
        push ebx
        push ecx
        call ArgumentStartsWithRegisters
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// 0x005B5D30
const char* __cdecl GetArgumentValue(const char* option, int argc, char** argv)
{
    const char* value = nullptr;
    for (int i = 0; i < argc; ++i) {
        if (!ArgumentStartsWith(argv[i], option))
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
bool __cdecl GotArgument(const char* option, int argc, char** argv)
{
    char wanted[256];
    char argument[256];
    if (!option)
        return false;
    for (size_t i = 0; i < std::strlen(option) + 1; ++i)
        wanted[i] = Lower(option[i]);
    for (int i = 0; i < argc; ++i) {
        for (size_t j = 0; j < std::strlen(argv[i]) + 1; ++j)
            argument[j] = Lower(argv[i][j]);
        if (std::strcmp(argument, wanted) == 0)
            return true;
    }
    return false;
}

// 0x005B5F10
const char* __cdecl GetPathPrefix() { return PathPrefix(); }

// 0x005B5F20
const char* __cdecl GetLanguageName()
{
    switch (Language()) {
    case 1: return GAME_TEXT(0x00657D4C, "Japanese"); // "Japanese"
    case 2: return GAME_TEXT(0x00653EBC, "English"); // "English"
    case 3: return GAME_TEXT(0x00656E78, "French"); // "French"
    case 4: return GAME_TEXT(0x00656E70, "Spanish"); // "Spanish"
    case 5: return GAME_TEXT(0x00656E80, "German"); // "German"
    case 6: return GAME_TEXT(0x00656E68, "Italian"); // "Italian"
    case 7: return GAME_TEXT(0x00657D44, "Dutch"); // "Dutch"
    case 8: return GAME_TEXT(0x00657D38, "Portuguese"); // "Portuguese"
    default: return GAME_TEXT(0x00657D24, "Unknown language"); // "Unknown language"
    }
}

// 0x005B5F90
const char* __cdecl GetTerritoryName()
{
    switch (Territory()) {
    case 1: return GAME_TEXT(0x00657D58, "Europe"); // "Europe"
    case 2: return GAME_TEXT(0x00657D60, "US"); // "US"
    case 3: return GAME_TEXT(0x00657D64, "Asia"); // "Asia"
    default: return GAME_TEXT(0x00657D6C, "Unknown territory"); // "Unknown territory"
    }
}

// 0x005B5FC0
const char* __cdecl GetHardwareName()
{
    switch (Hardware()) {
    case 1: return GAME_TEXT(0x00657DCC, "PS2"); // "PS2"
    case 2: return GAME_TEXT(0x00657DC0, "PS2 ProView"); // "PS2 ProView"
    case 3: return GAME_TEXT(0x00657DB0, "PS2 Dev System"); // "PS2 Dev System"
    case 4: return GAME_TEXT(0x00657DA8, "XBox"); // "XBox"
    case 5: return GAME_TEXT(0x00657D9C, "GameCube"); // "GameCube"
    case 7: return GAME_TEXT(0x00657D94, "Win32"); // "Win32"
    default: return GAME_TEXT(0x00657D80, "Unknown platform"); // "Unknown platform"
    }
}

// 0x005B6020
const char* __cdecl GetDisplayTypeName()
{
    switch (DisplayType()) {
    case 0: return GAME_TEXT(0x00653EC4, "PAL"); // "PAL"
    case 1: return GAME_TEXT(0x00657DD0, "NTSC"); // "NTSC"
    default: return GAME_TEXT(0x00657DD8, "Unknown display type"); // "Unknown display type"
    }
}

// 0x005B6040
const char* __cdecl GetPlatformExtension()
{
    switch (Hardware()) {
    case 1:
    case 2:
    case 3: return GAME_TEXT(0x00657E00, "ps2"); // "ps2"
    case 4: return GAME_TEXT(0x00657DFC, "xbx"); // "xbx"
    case 5: return GAME_TEXT(0x00657DF8, "ngc"); // "ngc"
    case 6: return GAME_TEXT(0x00657DF4, "xen"); // "xen"
    default: return GAME_TEXT(0x00657DF0, "???"); // "???"
    }
}

// 0x005B6090
void __cdecl SetNtscFlag(bool on)
{
    if (DisplayType() == 1)
        NtscFlag() = on;
}

// 0x005B60B0
void __cdecl Init(int argc, char** argv)
{
    CopyString(PathPrefix(), kEmpty);
    ReadRegistry(argc, argv);
    if (GotArgument(GAME_TEXT(0x00655C24, "CD"), argc, argv)) // "CD"
        FromCD() = true;
    if (GotArgument(GAME_TEXT(0x00655C1C, "NotCD"), argc, argv)) // "NotCD"
        FromCD() = false;
    if (GotArgument(GAME_TEXT(0x00657E6C, "NoExtraMem"), argc, argv)) // "NoExtraMem"
        ExtraMemory() = false;
    if (GotArgument(GAME_TEXT(0x00655C28, "Bootstrap"), argc, argv)) { // "Bootstrap"
        Bootstrap() = true;
        if (argc > 2) {
            CopyString(BootstrapReturn(), argv[2]);
            if (argc > 3)
                CopyString(BootstrapArgument(), argv[3]);
            else
                CopyString(BootstrapArgument(), GAME_TEXT(0x00657E64, "THUG2")); // "THUG2" (the original's sprintf)
            // "This bootstrap demo will return to %s passing back: %s\n"
            Dbg::Printf(GAME_TEXT(0x00657E2C, "This bootstrap demo will return to %s passing back: %s\n"), BootstrapReturn(), BootstrapArgument());
        }
    }
    if (GotArgument(GAME_TEXT(0x00657E1C, "NotBootstrap"), argc, argv)) // "NotBootstrap"
        Bootstrap() = false;
    if (GotArgument(GAME_TEXT(0x00657E18, "Pal"), argc, argv)) // "Pal"
        DisplayType() = 0;
    if (GotArgument(GAME_TEXT(0x00657DD0, "NTSC"), argc, argv)) // "NTSC"
        DisplayType() = 1;
    if (const char* rate = GetArgumentValue(GAME_TEXT(0x00657E0C, "FrameRate"), argc, argv)) { // "FrameRate"
        if (std::memcmp(rate, "50", 3) == 0)
            FrameRate() = 50;
        if (std::memcmp(rate, "60", 3) == 0)
            FrameRate() = 60;
    }
}

// 0x005B6250
const char* __cdecl GetBootstrapPrefix() { return Bootstrap() ? GAME_TEXT(0x00657E78, "THUG2\\") : kEmpty; } // "THUG2\"

// 0x005B6270
void __cdecl ReadRegistry(int, char**)
{
    const char* path = GAME_TEXT(0x00657E8C, "Software\\Activision\\Tony Hawk's Underground 2\\Install"); // "Software\Activision\Tony Hawk's Underground 2\Install"
    Hardware() = 7;
    Language() = 2;
    ExtraMemory() = false;
    DWORD language = 0;
    HKEY key;
    DWORD type;
    DWORD size;
    if (RegCreateKeyA(HKEY_CURRENT_USER, path, &key) == ERROR_SUCCESS) {
        size = 4;
        RegQueryValueExA(key, GAME_TEXT(0x00657E80, "language"), nullptr, &type, reinterpret_cast<BYTE*>(&language), &size); // "language"
        RegCloseKey(key);
    }
    if (language == 0 && RegCreateKeyA(HKEY_LOCAL_MACHINE, path, &key) == ERROR_SUCCESS) {
        size = 4;
        RegQueryValueExA(key, GAME_TEXT(0x00657E80, "language"), nullptr, &type, reinterpret_cast<BYTE*>(&language), &size);
        RegCloseKey(key);
    }
    switch (language) {
    case 2: Language() = 3; break; // French
    case 3: Language() = 5; break; // German
    default: Language() = 2; break;
    }
}

} // namespace GameConfig

REPLACE_UNCHECKED(0x005B5C90, GameConfig::ArgumentStartsWithStub);
REPLACE(0x005B5D30, GameConfig::GetArgumentValue);
REPLACE(0x005B5DD0, GameConfig::GotArgument);
REPLACE(0x005B5F10, GameConfig::GetPathPrefix);
REPLACE(0x005B5F20, GameConfig::GetLanguageName);
REPLACE(0x005B5F90, GameConfig::GetTerritoryName);
REPLACE(0x005B5FC0, GameConfig::GetHardwareName);
REPLACE(0x005B6020, GameConfig::GetDisplayTypeName);
REPLACE(0x005B6040, GameConfig::GetPlatformExtension);
REPLACE(0x005B6090, GameConfig::SetNtscFlag);
REPLACE(0x005B60B0, GameConfig::Init);
REPLACE(0x005B6250, GameConfig::GetBootstrapPrefix);
REPLACE(0x005B6270, GameConfig::ReadRegistry);
