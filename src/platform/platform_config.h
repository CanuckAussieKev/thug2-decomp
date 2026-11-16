#pragma once

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include <cstddef>
#include <string>

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

// The settings reads below are answered from memory (NOT ORIGINAL: a read is a visit to the disk, and a game whose
// settings were read twice a second by its menus' restyle stood still for it while the disk was busy): the first
// read of a setting is the file's, an answer older than half a second is given again as it is and read anew by a
// thread of its own - a settings file changed by hand shows within a second -, and forget_cached_reads() (the game
// has just written the file) makes the next read the file's.
void forget_cached_reads();
// GetPrivateProfileInt of `path`, cached (the reads below, and run_log's config_int).
int cached_profile_int(const char* path, const char* section, const char* key, int fallback);
// GetPrivateProfileString of `path`, cached: the characters copied into `out` (at most `size` with the end).
int cached_profile_text(const char* path, const char* section, const char* key, const char* fallback, char* out,
                        size_t size);

// GetPrivateProfileInt != 0.
bool fetch_flag(const char* section, const char* key, bool fallback);
// GetPrivateProfileInt.
int get_int(const char* section, const char* key, int fallback);
// GetPrivateProfileString: the characters copied into `out`.
int fetch_text(const char* section, const char* key, const char* fallback, char* out, size_t size);
// NOT ORIGINAL - the settings file's whole text as it is now ("" when there is none): the settings model reads every
// key from it at once (engine/config/settings_state.h).
std::string file_text();

} // namespace build_info
