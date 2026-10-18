#pragma once

#include <string>

// NOT ORIGINAL - the data overlay (ours, added at the user's request; not the game's and not PartyMod's): game files
// made by tools/menu_level/make_menu_level.py from the game's own (the playable skate shop, the level select's extra
// signs, the THPS4 levels, the pause menu's LEVEL EDITOR, the standard main menu) in a folder next to the exe,
// thug2-decomp-data, read before the game's archives and data folder (Pip::Load / LoadFileInto, File::Open / Exist,
// the async file system).
//   - Files at the top of the folder are always used.
//   - Files in its front_end folder are used first while the skate-shop front end is on (menu_backdrop.h Active).
namespace Front::DataOverlay {

// The overlay folder (<game folder>\thug2-decomp-data).
const std::string& Folder();

// The overlay's file for a game file name ("levels\mainmenu\mainmenu.qb", "data/..." or a development machine's
// "c:\...\data\..." path), if it exists: its full path in `path`.
bool OverlayPath(const char* name, char (&path)[260]);

// The game file name as the overlay keeps it: '/' as '\', lower case, without anything up to "data\" (exposed for
// the self-tests).
void OverlayName(const char* name, char (&out)[260]);

} // namespace Front::DataOverlay
