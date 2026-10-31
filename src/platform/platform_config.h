#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstddef>

// The PC settings: thug2-decomp.ini next to the exe ([features] and the others). Window, graphics, input, glyph, online and logging options are read from its
// sections below at start-up. (Not the game's own registry settings.)
namespace build_info {

constexpr const char* kFileName = "thug2-decomp.ini";

// Its sections.
constexpr const char* kGraphics = "Graphics";
constexpr const char* kMiscellaneous = "Miscellaneous";
constexpr const char* kKeybinds = "Keybinds";
constexpr const char* kGamepad = "Gamepad";
constexpr const char* kLogging = "Logging";

// Find the exe's folder and the settings file in it (before any of the getters).
void prepare_all();
// The exe's folder, ending in a backslash.
const char* executable_directory();

// GetPrivateProfileInt != 0.
bool fetch_flag(const char* section, const char* key, bool fallback);
// GetPrivateProfileInt.
int get_int(const char* section, const char* key, int fallback);
// GetPrivateProfileString: the characters copied into `out`.
int fetch_text(const char* section, const char* key, const char* fallback, char* out, size_t size);

} // namespace Config
