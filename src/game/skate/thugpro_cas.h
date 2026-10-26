#pragma once

#include <cstdint>
#include <string>
#include <vector>

// NOT ORIGINAL - THUGPRO's own CAS parts and boards, read from the user's THUGPRO install by reference (register:
// tools/pipeline/compat/thugpro/README.md "Run time", TP-R10). Nothing of THUGPRO's is
// copied into this repository or the data overlay: each start the game reads them from the install, read only.
//
// THUGPRO (Morten1337's THUG2 mod) adds ~110 parts to THUG2's CAS lists (THUG1 / THAW pros' bodies, secret skaters,
// heads, hats, boards) in its own versions of the CAS scripts (data\pre\thugpro_qb.prx: qb\game\cas_skater_m.qb ...,
// qb\thugpro\thugpro_boards.qb) and ships their models as loose files (data\models\skater_pro, skater_secret,
// CAS_ITEMS, boards...). A THUGPRO player's appearance names them; with them here a remote THUGPRO skater is drawn as
// they look in THUGPRO, without them the appearance guard (appearance_guard.h) falls back to THUG2's parts.
//   - Once the game's scripts are loaded (the main loop's first frames), every array of those files that is also one
//     of our part lists (a global array of structures with desc_id) gets THUGPRO's entries we haven't, appended with
//     `hidden` and `no_random` (never offered in our CAS menus or the random skater: our own skaters stay THUG2's).
//   - Only entries whose files can all be found: each string naming a path (a mesh, a texture swap's `with`) must be
//     the game's own or a loose file of THUGPRO's (not in its archives: those aren't read). Others are left out.
//   - Those loose files are then read from the install when the game asks for them (DataOverlay::OverlayPath's last
//     step) - only files the game itself hasn't, named by an added part.
// [features] thugpro_parts=0 turns it off, =2 offers them in our CAS menus too (without `hidden` / `no_random`: to try
// them offline - a THUG2 player without them then sees our skater with THUG2's parts); thugpro_folder= the install's
// folder (default %LOCALAPPDATA%\THUG Pro).
namespace Script {
class CArray;
class CStruct;
} // namespace Script

namespace Obj::ThugproCas {

// From the main loop once a frame until it has run (it waits for the CAS lists to exist).
void LoadOnce();

// The data overlay's last step: `relative` (DataOverlay::OverlayName's form: lower case, '\', no "data\") is a file
// of one of THUGPRO's added parts, in its install: its full path in `path`.
bool FilePath(const char* relative, char (&path)[260]);

// --- the pieces (exposed for the self-tests) -------------------------------------------------------------------------

// An archive's (.prx) files named in `names` (lower case, '\'), decompressed; false if it isn't one.
bool ReadArchive(const std::vector<uint8_t>& archive, const std::vector<std::string>& names,
                 std::vector<std::pair<std::string, std::vector<uint8_t>>>* out);
// The archives' LZSS (a 4 KB window of spaces, written from 4078): `size` bytes out, or fewer if the input runs out.
std::vector<uint8_t> DecodeLzss(const uint8_t* in, size_t length, size_t size);
// A .qb file's top-level `name = [` arrays: their names and where each array starts (its '[' token). The walk stops
// at its end token, at `size` bytes, or at a token it can't move past (the file must end in zeros: the tokens are read
// without bounds, as the game reads them).
std::vector<std::pair<uint32_t, const uint8_t*>> TopLevelArrays(const uint8_t* qb, size_t size);
// A game path's key: lower case, '\', its extensions taken off (.xbx, then .skin / .tex / .cas / .col / .img / .png /
// .mdl / .ske ...), as both a part's path and the game's request for one of its files give it.
std::string PathKey(const char* path);
// Of THUGPRO's array `theirs`, the entries to append to our `ours`: structures with a desc_id ours hasn't, every path
// they name found by `found` (which notes the THUGPRO ones). Their indices in `theirs`.
std::vector<uint32_t> PickNew(Script::CArray* ours, Script::CArray* theirs, bool (*found)(const char* path));

} // namespace Obj::ThugproCas
