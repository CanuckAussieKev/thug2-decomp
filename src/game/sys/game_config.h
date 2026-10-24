#pragma once

#include <cstdint>

// The game's own configuration (0x005B5C90-0x005B6370): the command line's options (CD / NotCD, NoExtraMem,
// Bootstrap, Pal / NTSC, FrameRate=50|60), the language from the registry (HKCU, then HKLM, ...\Install\language),
// and the names of the language, territory, hardware, display type and platform. (Not the hook's thug2-decomp.ini
// settings: those are sys/config.h.)
namespace GameConfig {

// The settings it keeps.
int& Language();        // 0x007CEFA0 (1 Japanese, 2 English, 3 French, 4 Spanish, 5 German, 6 Italian, 7 Dutch, 8 Portuguese)
int& Territory();       // 0x007CEFA4 (1 Europe, 2 US, 3 Asia)
int& Hardware();        // 0x007CEFA8 (1 PS2, 2 PS2 ProView, 3 PS2 Dev System, 4 XBox, 5 GameCube, 7 Win32)
int& DisplayType();     // 0x00684FB0 (0 PAL, 1 NTSC)
int& FrameRate();       // 0x00684FB4
bool& FromCD();         // 0x00684FA8 (the script's CD)
bool& ExtraMemory();    // 0x007CEFAC
bool& Bootstrap();      // 0x007CEFAD
bool& NtscFlag();       // 0x007CEFAF (set through SetNtscFlag, NTSC only)

// 0x005B5C90 (the argument in ECX, the option in EBX; a naked stub): whether `argument` starts with `option` (both
// non-null; the argument copied into a 100-byte buffer - unbounded, as the original - and cut to the option's
// length).
bool ArgumentStartsWith(const char* argument, const char* option);
void ArgumentStartsWithStub();
// 0x005B5D30: the value of the last `option` on the command line: option=value, option= value, option =value or
// option = value (null if none).
const char* __cdecl GetArgumentValue(const char* option, int argc, char** argv);
// 0x005B5DD0: whether the command line has `option` (case-insensitively; each one copied into a 256-byte buffer -
// unbounded, as the original).
bool __cdecl GotArgument(const char* option, int argc, char** argv);
// 0x005B5F10: the path prefix (0x007CEE20, set empty by Init).
const char* __cdecl GetPathPrefix();
// 0x005B5F20 / 0x005B5F90 / 0x005B5FC0 / 0x005B6020 / 0x005B6040: the names ("English", "US", "Win32", "NTSC",
// "xbx"...; "Unknown ..." / "???" for anything else).
const char* __cdecl GetLanguageName();
const char* __cdecl GetTerritoryName();
const char* __cdecl GetHardwareName();
const char* __cdecl GetDisplayTypeName();
const char* __cdecl GetPlatformExtension();
// 0x005B6090: the NTSC flag set (only with an NTSC display).
void __cdecl SetNtscFlag(bool on);
// 0x005B60B0: the configuration from the registry (ReadRegistry) and the command line.
void __cdecl Init(int argc, char** argv);
// 0x005B6250: "THUG2\" when bootstrapped, else "".
const char* __cdecl GetBootstrapPrefix();
// 0x005B6270: Win32 hardware, English, no extra memory; then the language from the registry - HKCU's key, else
// HKLM's (each opened with RegCreateKeyA: made if missing) - 2 French, 3 German, anything else English. The
// arguments aren't used.
void __cdecl ReadRegistry(int argc, char** argv);

} // namespace GameConfig
