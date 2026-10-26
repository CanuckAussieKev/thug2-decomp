// The game's own configuration: the command line, the registry's language, the names (see game_config.h).

#include "game/sys/game_config.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace GameConfig {

int& Language() { return GameVariable<int>(0x007CEFA0); }
int& Territory() { return GameVariable<int>(0x007CEFA4); }
int& Hardware() { return GameVariable<int>(0x007CEFA8); }
int& DisplayType() { return GameVariable<int>(0x00684FB0); }
int& FrameRate() { return GameVariable<int>(0x00684FB4); }
bool& FromCD() { return GameVariable<bool>(0x00684FA8); }
bool& ExtraMemory() { return GameVariable<bool>(0x007CEFAC); }
bool& Bootstrap() { return GameVariable<bool>(0x007CEFAD); }
bool& NtscFlag() { return GameVariable<bool>(0x007CEFAF); }

namespace {

char* PathPrefix() { return &GameVariable<char>(0x007CEE20); }       // (0x80 bytes)
char* BootstrapReturn() { return &GameVariable<char>(0x007CEF20); }  // where a bootstrap demo returns to
char* BootstrapArgument() { return &GameVariable<char>(0x007CEEA0); } // what it passes back
const char* const kEmpty = reinterpret_cast<const char*>(0x006469B4); // ""

const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }

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
    case 1: return Text(0x00657D4C); // "Japanese"
    case 2: return Text(0x00653EBC); // "English"
    case 3: return Text(0x00656E78); // "French"
    case 4: return Text(0x00656E70); // "Spanish"
    case 5: return Text(0x00656E80); // "German"
    case 6: return Text(0x00656E68); // "Italian"
    case 7: return Text(0x00657D44); // "Dutch"
    case 8: return Text(0x00657D38); // "Portuguese"
    default: return Text(0x00657D24); // "Unknown language"
    }
}

// 0x005B5F90
const char* __cdecl GetTerritoryName()
{
    switch (Territory()) {
    case 1: return Text(0x00657D58); // "Europe"
    case 2: return Text(0x00657D60); // "US"
    case 3: return Text(0x00657D64); // "Asia"
    default: return Text(0x00657D6C); // "Unknown territory"
    }
}

// 0x005B5FC0
const char* __cdecl GetHardwareName()
{
    switch (Hardware()) {
    case 1: return Text(0x00657DCC); // "PS2"
    case 2: return Text(0x00657DC0); // "PS2 ProView"
    case 3: return Text(0x00657DB0); // "PS2 Dev System"
    case 4: return Text(0x00657DA8); // "XBox"
    case 5: return Text(0x00657D9C); // "GameCube"
    case 7: return Text(0x00657D94); // "Win32"
    default: return Text(0x00657D80); // "Unknown platform"
    }
}

// 0x005B6020
const char* __cdecl GetDisplayTypeName()
{
    switch (DisplayType()) {
    case 0: return Text(0x00653EC4); // "PAL"
    case 1: return Text(0x00657DD0); // "NTSC"
    default: return Text(0x00657DD8); // "Unknown display type"
    }
}

// 0x005B6040
const char* __cdecl GetPlatformExtension()
{
    switch (Hardware()) {
    case 1:
    case 2:
    case 3: return Text(0x00657E00); // "ps2"
    case 4: return Text(0x00657DFC); // "xbx"
    case 5: return Text(0x00657DF8); // "ngc"
    case 6: return Text(0x00657DF4); // "xen"
    default: return Text(0x00657DF0); // "???"
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
    if (GotArgument(Text(0x00655C24), argc, argv)) // "CD"
        FromCD() = true;
    if (GotArgument(Text(0x00655C1C), argc, argv)) // "NotCD"
        FromCD() = false;
    if (GotArgument(Text(0x00657E6C), argc, argv)) // "NoExtraMem"
        ExtraMemory() = false;
    if (GotArgument(Text(0x00655C28), argc, argv)) { // "Bootstrap"
        Bootstrap() = true;
        if (argc > 2) {
            CopyString(BootstrapReturn(), argv[2]);
            if (argc > 3)
                CopyString(BootstrapArgument(), argv[3]);
            else
                CopyString(BootstrapArgument(), Text(0x00657E64)); // "THUG2" (the original's sprintf)
            // "This bootstrap demo will return to %s passing back: %s\n"
            Dbg::Printf(Text(0x00657E2C), BootstrapReturn(), BootstrapArgument());
        }
    }
    if (GotArgument(Text(0x00657E1C), argc, argv)) // "NotBootstrap"
        Bootstrap() = false;
    if (GotArgument(Text(0x00657E18), argc, argv)) // "Pal"
        DisplayType() = 0;
    if (GotArgument(Text(0x00657DD0), argc, argv)) // "NTSC"
        DisplayType() = 1;
    if (const char* rate = GetArgumentValue(Text(0x00657E0C), argc, argv)) { // "FrameRate"
        if (std::memcmp(rate, "50", 3) == 0)
            FrameRate() = 50;
        if (std::memcmp(rate, "60", 3) == 0)
            FrameRate() = 60;
    }
}

// 0x005B6250
const char* __cdecl GetBootstrapPrefix() { return Bootstrap() ? Text(0x00657E78) : kEmpty; } // "THUG2\"

// 0x005B6270
void __cdecl ReadRegistry(int, char**)
{
    const char* path = Text(0x00657E8C); // "Software\Activision\Tony Hawk's Underground 2\Install"
    Hardware() = 7;
    Language() = 2;
    ExtraMemory() = false;
    DWORD language = 0;
    HKEY key;
    DWORD type;
    DWORD size;
    if (RegCreateKeyA(HKEY_CURRENT_USER, path, &key) == ERROR_SUCCESS) {
        size = 4;
        RegQueryValueExA(key, Text(0x00657E80), nullptr, &type, reinterpret_cast<BYTE*>(&language), &size); // "language"
        RegCloseKey(key);
    }
    if (language == 0 && RegCreateKeyA(HKEY_LOCAL_MACHINE, path, &key) == ERROR_SUCCESS) {
        size = 4;
        RegQueryValueExA(key, Text(0x00657E80), nullptr, &type, reinterpret_cast<BYTE*>(&language), &size);
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
