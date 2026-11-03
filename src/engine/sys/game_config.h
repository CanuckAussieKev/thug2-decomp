#pragma once

#include <cstdint>
#include <span>

// The game's own configuration (0x005B5C90-0x005B6370): the command line's options (CD / NotCD, NoExtraMem,
// Bootstrap, Pal / NTSC, FrameRate=50|60), the language from the registry (HKCU, then HKLM, ...\Install\language),
// and the names of the language, territory, hardware, display type and platform. (Not thug2-decomp.ini
// settings: those are sys/config.h.)
namespace game_config {

// The command line: its words (the program name first).
using Arguments = std::span<char* const>;

// The language, territory, hardware and display numbers (the settings below hold one of each).
constexpr int kLanguageJapanese = 1;
constexpr int kLanguageEnglish = 2;
constexpr int kLanguageFrench = 3;
constexpr int kLanguageSpanish = 4;
constexpr int kLanguageGerman = 5;
constexpr int kLanguageItalian = 6;
constexpr int kLanguageDutch = 7;
constexpr int kLanguagePortuguese = 8;
constexpr int kTerritoryEurope = 1;
constexpr int kTerritoryUs = 2;
constexpr int kTerritoryAsia = 3;
constexpr int kHardwarePs2 = 1;
constexpr int kHardwarePs2ProView = 2;
constexpr int kHardwarePs2DevSystem = 3;
constexpr int kHardwareXbox = 4;
constexpr int kHardwareGameCube = 5;
constexpr int kHardwareXenon = 6; // (only in fetch_platform_suffix's table)
constexpr int kHardwareWin32 = 7;
constexpr int kDisplayPal = 0;
constexpr int kDisplayNtsc = 1;

// The settings it keeps.
int& get_language(); // 0x007CEFA0 (1 Japanese, 2 English, 3 French, 4 Spanish, 5 German, 6 Italian, 7 Dutch, 8 Portuguese)
int& territory();    // 0x007CEFA4 (1 Europe, 2 US, 3 Asia)
int& hardware();     // 0x007CEFA8 (1 PS2, 2 PS2 ProView, 3 PS2 Dev System, 4 XBox, 5 GameCube, 7 Win32)
int& display_type();      // 0x00684FB0 (0 PAL, 1 NTSC)
int& frame_rate();        // 0x00684FB4
bool& from_cd();          // 0x00684FA8 (the script's CD)
bool& get_extra_memory(); // 0x007CEFAC
bool& bootstrap();        // 0x007CEFAD
bool& ntsc_flag();        // 0x007CEFAF (set through set_ntsc_flag, NTSC only)

// 0x005B5C90: whether `argument` starts with `option` (both
// non-null; the argument copied into a 100-byte buffer - unbounded, as the original - and cut to the option's
// length).
bool argument_starts_with(const char* argument, const char* option);
// 0x005B5D30: the value of the last `option` on the command line: option=value, option= value, option =value or
// option = value (null if none).
const char* get_argument_value(const char* option, Arguments args);
// 0x005B5DD0: whether the command line has `option` (case-insensitively; each one copied into a 256-byte buffer -
// unbounded, as the original).
bool got_argument(const char* option, Arguments args);
// 0x005B5F10: the path prefix (0x007CEE20, set empty by Init).
const char* get_path_prefix();
// 0x005B5F20 / 0x005B5F90 / 0x005B5FC0 / 0x005B6020 / 0x005B6040: the names ("English", "US", "Win32", "NTSC",
// "xbx"...; "Unknown ..." / "???" for anything else).
const char* get_locale_label();
const char* get_region_label();
const char* get_platform_label();
const char* get_screen_mode_label();
const char* fetch_platform_suffix();
// 0x005B6090: the NTSC flag set (only with an NTSC display).
void set_ntsc_flag(bool on);
// 0x005B60B0: the configuration from the registry (read_registry) and the command line.
void prepare_all(Arguments args);
// 0x005B6250: "THUG2\" when bootstrapped, else "".
const char* get_bootstrap_prefix();
// 0x005B6270: Win32 hardware, English, no extra memory; then the language from the registry - HKCU's key, else
// HKLM's (each opened with RegCreateKeyA: made if missing) - 2 French, 3 German, anything else English. The
// arguments aren't used.
void read_registry(Arguments args);

} // namespace game_config
