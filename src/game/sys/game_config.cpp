// The game's own configuration: the command line, the registry's language, the names (see game_config.h).

#include "game/sys/game_config.h"

#include <windows.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/script/cfuncs_scene.h"

namespace GameConfig {

bool g_fromCD{true};
int g_frameRate{60};
int g_language;
int g_territory;
int g_platform;
bool g_configBootstrap;
bool g_ntscFlag;

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

char g_pathPrefix[128];
char g_bootstrapArgument[128];
char g_bootstrapReturn[128];

char* PathPrefix() { return g_pathPrefix; }       // (0x80 bytes)
char* BootstrapReturn() { return g_bootstrapReturn; }  // where a bootstrap demo returns to
char* BootstrapArgument() { return g_bootstrapArgument; } // what it passes back
const char* const kEmpty = ""; // ""

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
    case 1: return "Japanese"; // "Japanese"
    case 2: return "English"; // "English"
    case 3: return "French"; // "French"
    case 4: return "Spanish"; // "Spanish"
    case 5: return "German"; // "German"
    case 6: return "Italian"; // "Italian"
    case 7: return "Dutch"; // "Dutch"
    case 8: return "Portuguese"; // "Portuguese"
    default: return "Unknown language"; // "Unknown language"
    }
}

// 0x005B5F90
const char* __cdecl GetTerritoryName()
{
    switch (Territory()) {
    case 1: return "Europe"; // "Europe"
    case 2: return "US"; // "US"
    case 3: return "Asia"; // "Asia"
    default: return "Unknown territory"; // "Unknown territory"
    }
}

// 0x005B5FC0
const char* __cdecl GetHardwareName()
{
    switch (Hardware()) {
    case 1: return "PS2"; // "PS2"
    case 2: return "PS2 ProView"; // "PS2 ProView"
    case 3: return "PS2 Dev System"; // "PS2 Dev System"
    case 4: return "XBox"; // "XBox"
    case 5: return "GameCube"; // "GameCube"
    case 7: return "Win32"; // "Win32"
    default: return "Unknown platform"; // "Unknown platform"
    }
}

// 0x005B6020
const char* __cdecl GetDisplayTypeName()
{
    switch (DisplayType()) {
    case 0: return "PAL"; // "PAL"
    case 1: return "NTSC"; // "NTSC"
    default: return "Unknown display type"; // "Unknown display type"
    }
}

// 0x005B6040
const char* __cdecl GetPlatformExtension()
{
    switch (Hardware()) {
    case 1:
    case 2:
    case 3: return "ps2"; // "ps2"
    case 4: return "xbx"; // "xbx"
    case 5: return "ngc"; // "ngc"
    case 6: return "xen"; // "xen"
    default: return "???"; // "???"
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
    if (GotArgument("CD", argc, argv)) // "CD"
        FromCD() = true;
    if (GotArgument("NotCD", argc, argv)) // "NotCD"
        FromCD() = false;
    if (GotArgument("NoExtraMem", argc, argv)) // "NoExtraMem"
        ExtraMemory() = false;
    if (GotArgument("Bootstrap", argc, argv)) { // "Bootstrap"
        Bootstrap() = true;
        if (argc > 2) {
            CopyString(BootstrapReturn(), argv[2]);
            if (argc > 3)
                CopyString(BootstrapArgument(), argv[3]);
            else
                CopyString(BootstrapArgument(), "THUG2"); // "THUG2" (the original's sprintf)
            // "This bootstrap demo will return to %s passing back: %s\n"
            Dbg::Printf("This bootstrap demo will return to %s passing back: %s\n", BootstrapReturn(), BootstrapArgument());
        }
    }
    if (GotArgument("NotBootstrap", argc, argv)) // "NotBootstrap"
        Bootstrap() = false;
    if (GotArgument("Pal", argc, argv)) // "Pal"
        DisplayType() = 0;
    if (GotArgument("NTSC", argc, argv)) // "NTSC"
        DisplayType() = 1;
    if (const char* rate = GetArgumentValue("FrameRate", argc, argv)) { // "FrameRate"
        if (std::memcmp(rate, "50", 3) == 0)
            FrameRate() = 50;
        if (std::memcmp(rate, "60", 3) == 0)
            FrameRate() = 60;
    }
}

// 0x005B6250
const char* __cdecl GetBootstrapPrefix() { return Bootstrap() ? "THUG2\\" : kEmpty; } // "THUG2\"

// 0x005B6270
void __cdecl ReadRegistry(int, char**)
{
    const char* path = "Software\\Activision\\Tony Hawk's Underground 2\\Install"; // "Software\Activision\Tony Hawk's Underground 2\Install"
    Hardware() = 7;
    Language() = 2;
    ExtraMemory() = false;
    DWORD language = 0;
    HKEY key;
    DWORD type;
    DWORD size;
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
    case 2: Language() = 3; break; // French
    case 3: Language() = 5; break; // German
    default: Language() = 2; break;
    }
}

} // namespace GameConfig
