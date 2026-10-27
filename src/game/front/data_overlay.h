#pragma once

#include <string>

// NOT ORIGINAL - the data overlay (ours, added at the user's request; not the game's and not PartyMod's): game files
// made by tools/pipeline/menu_level/make_menu_level.ts from the game's own (the playable skate shop, the level select's extra
// signs, the THPS4 levels, the pause menu's LEVEL EDITOR, the standard main menu) in a folder next to the exe,
// thug2-decomp-data, read before the game's archives and data folder (Pip::Load / LoadFileInto, File::Open / Exist,
// the async file system).
//   - Files at the top of the folder are always used.
//   - Files in its front_end folder are used first while the new front end is on ([features] new_frontend, new_frontend.h Enabled).
//   - Files in its anteara folder (anteara\front_end first while the front end is on) are used before both while
//     thug2-decomp.ini [features] anteara_mod=1 (default 0): Anteara Mod, the user's own 2010/2011 script mod
//     (tools/anteara/README.md). make_menu_level.ts writes there the mod's complete script set - its qdir.txt, every
//     file it lists, localtext.qb - made from the game's loose Data\scripts with the mod's changes and this overlay's
//     own edits (levels, level select, LEVEL EDITOR, the front end's roundbar menu) re-applied. Off: the game as
//     before. One switch for the whole mod for now (per-feature switches would go in the mod's scripts).
//   - Files in its hd_ui folder (the menus' images and fonts upscaled, make_menu_level.ts --hd-ui) are used before all
//     of those while [features] hd_ui=1 (default 0) and are never read otherwise (src/game/gfx/hd_ui.h).
namespace Front::DataOverlay {

// The overlay folder (<game folder>\thug2-decomp-data).
const std::string& Folder();

// The overlay's file for a game file name ("levels\mainmenu\mainmenu.qb", "data/..." or a development machine's
// "c:\...\data\..." path), if it exists: its full path in `path`.
bool OverlayPath(const char* name, char (&path)[260]);

// [features] anteara_mod (read once; logs a line at start-up when on).
bool AntearaMod();

// The folders under the overlay folder a file is looked for in, first match wins ("anteara\front_end\",
// "anteara\", "front_end\", "" - the last always): Anteara Mod's while it's on, the front end's while the
// skate-shop front end is on. Returns how many are in `folders`.
int SearchFolders(bool anteara, bool frontEnd, const char* (&folders)[4]);

// The game file name as the overlay keeps it: '/' as '\', lower case, without anything up to "data\" (exposed for
// the self-tests).
void OverlayName(const char* name, char (&out)[260]);

} // namespace Front::DataOverlay
