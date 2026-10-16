#pragma once

// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

#include <cstddef>

// The PC settings file: partymod.ini next to the exe (the name PartyMod gave it, kept so existing settings
// keep working). Window, graphics, input, glyph, online and logging options are read from it at start-up.
// (Not the game's own registry settings, and not thug2-decomp.ini, which is the hook DLL's.)
namespace Config {

constexpr const char* kFileName = "partymod.ini";

// Its sections.
constexpr const char* kGraphics = "Graphics";
constexpr const char* kMiscellaneous = "Miscellaneous";
constexpr const char* kKeybinds = "Keybinds";
constexpr const char* kGamepad = "Gamepad";
constexpr const char* kLogging = "Logging";

// Find the exe's folder and the settings file in it (before any of the getters).
void Init();
// The exe's folder, ending in a backslash.
const char* ExecutableDirectory();

// GetPrivateProfileInt != 0.
bool GetBool(const char* section, const char* key, bool fallback);
// GetPrivateProfileInt.
int GetInt(const char* section, const char* key, int fallback);
// GetPrivateProfileString: the characters copied into `out`.
int GetString(const char* section, const char* key, const char* fallback, char* out, size_t size);

} // namespace Config
