#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstddef>

// The PC settings: thug2-decomp.ini next to the exe (the file the hook DLL reads its own [replacements],
// [selftests] and [features] from). Window, graphics, input, glyph, online and logging options are read from its
// sections below at start-up. (Not the game's own registry settings.)
namespace Config {

constexpr const char* kFileName = "thug2-decomp.ini";

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
