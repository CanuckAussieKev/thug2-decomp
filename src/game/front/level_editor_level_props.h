#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "game/front/level_editor_fields.h"

namespace Script {
class CStruct;
}
namespace Gfx {
struct TODParams;
}

// NOT ORIGINAL - the level editor's level properties (ours; see level_editor.h - phase 6 of the plan): settings of the
// edited copy as a whole, kept in its layout and put in place only when the edited copy loads (never the level as
// shipped, never in a network session - level_editor_layout.h):
//   - its name in the level select: the EDITED LEVELS entry reads "<name> (EDITED)" (LevelEditor_EditedLevels) and the
//     toolbar shows it - next time the level select opens;
//   - where the skater starts: one of its restarts - the skater is put there (ResetSkaters node_name=..., as F8 does)
//     once the edited copy is up;
//   - the time of day it starts in (tod_state: morning, afternoon, evening, night) and whether time passes
//     (tod_manager_active) - load_level reads both from the level structure (Levels.qb: TODManager_SetTODInstant
//     <tod_state>, TODManager_SetActive); the state also changes at once in the editor (Gfx::CTODManager::
//     SetTODInstant);
//   - its ambient light colour and fog (colour, alpha, distance) - live in the editor and on load (map4). Studied
//     (the reference build): nothing reads a level structure's ambient_red / _green / _blue or fog_* keys - the checksums appear
//     only in the time-of-day manager's state parser (0x00571A00, a TODManager_SetParams state structure such as
//     tod_manager.qb's tod_manager_default_afternoon), CFunc_TODManager_GetCurrentTODParams (0x00572C00, its output) and
//     CFunc_GetLightCurrentColor (0x00447130, its output); Levels.qb's ambient_* in some level structures are left over
//     and do nothing (no level structure has fog_*). The engine's ambient light and fog come from the manager:
//     ApplyTODParams (0x00572A10) sends the state's ambient_* / fog_* (SetAmbientColor 0x0048E8F0, the fog's 0x00490DE0 /
//     0x00490E20 / 0x00490E00 - what the SetFogColor / SetFogDistance / EnableFog script commands call) whenever the
//     state changes or is refreshed. So the edited copy's are put over the state's there (Gfx::SetTODParamsFilter -
//     Override below), and sent again at once when they change (Reapply: the manager's ApplyTODParams with its current
//     parameters). Kept in the level structure as ed_ambient=[r g b] / ed_fog=[r g b alpha distance] (keys of ours -
//     a bake writes the same: tools/pipeline/game/apply_layout.ts), read once a level is up (OnLevelUp) - offline only;
//     the edited copy's own record wins over them;
//   - its sky (the feature map's "sky" row): another level's sky scene - the level structure's sky= ("TR_Sky"), which
//     load_level loads before the level's own scene (Levels.qb: `if gotparam sky { LoadScene scene=<sky> }`; a scene
//     whose name holds "_sky" is the sky - Nx::CFunc_LoadScene's is_sky). The skies are loose files
//     (Data\levels\<X>_Sky\<X>_Sky.scn / .tex / .col, the ported levels' in the data overlay), so any level's loads
//     with any other: Skies() lists the sky= of every level structure the game has (THUG2's 21 and the ported ones) -
//     on reload (the sky scene is loaded with the level, before it).
// How: LevelEditor_PickLevel (the level select chose the edited copy, before load_level runs) writes them into the
// level's structure (Level_<x>, which load_level includes - its values are read as the load goes), and once the edited
// copy is up (Layout::Tick) the structure is put back as the level has it: a level loaded any other way never sees
// them. Opened with -edit or the pause menu's LEVEL EDITOR, the level was already loaded as shipped: the time of day
// shows at once, the rest when the edited copy is next loaded from the level select.
//
// Layout record (version 2 still; an older reader skips the kind):
//   level [name=<text>] [start=<node>] [tod=<state>] [time=0|1] [ambient=r,g,b] [fog=r,g,b,alpha,distance]
//         [sky=<scene>]
// <text> and <scene> %-escaped (level_editor_fields.h); <node> 8 hex digits.
namespace Front::LevelEditor::LevelProps {

// --- the pure parts -----------------------------------------------------------------------------------------------

constexpr int kStates = 4;
// "morning", "afternoon", "evening", "night" (0..3); null otherwise.
const char* TodName(int tod);
// The state of a name (case doesn't matter); false: not one.
bool TodFromName(const std::string& name, int* tod);

struct Props {
    std::string name;  // the level select's text ("": the level's own)
    uint32_t start = 0; // a restart (0: the level's own first)
    int tod = -1;      // 0..3 (-1: the level's)
    int time = -1;     // time passes: 0 / 1 (-1: the level's)
    bool hasAmbient = false;
    int ambient[3] = {};
    bool hasFog = false;
    int fog[5] = {}; // r, g, b, alpha, distance
    std::string sky; // the sky scene ("": the level's own) - another level's, e.g. "BO_Sky"
};
bool Same(const Props& a, const Props& b);
bool Empty(const Props& props);
// The record's line ("" when there's nothing to keep), and one read (after the word "level"; false: malformed).
std::string WriteRecord(const Props& props);
bool ReadRecord(std::istringstream& in, Props* props);
// The level structure's keys the properties set, with their values (in this order).
std::vector<std::pair<const char*, Fields::Value>> StructureValues(const Props& props);
// Whether a sky scene's name may be kept (1-64 characters, letters, digits, '_', and holding "_sky" - what the load
// takes as a sky: Nx::CFunc_LoadScene).
bool ValidSky(const std::string& sky);
// A colour channel / distance typed in, kept in range.
int Channel(int value);
int Distance(int value);
// (map4) The ambient light and fog in effect put over a time-of-day state's parameters: the edited copy's (`edited`)
// where it sets them, else the level structure's ed_ambient / ed_fog (`level` - a bake's); channels and the distance
// kept in range. True when a fog is put there (the fog then shown even where the manager's is off).
bool Override(const Props& edited, const Props& level, Gfx::TODParams* params);
// The level structure's ed_ambient=[r g b] / ed_fog=[r g b alpha distance] read into `out` (hasAmbient / hasFog: the
// key there with the right count of integers).
void ReadOverrideKeys(Script::CStruct* level, Props* out);

// --- the game side ------------------------------------------------------------------------------------------------

// The edited copy's properties now (as edited in this session, or as its layout put them).
const Props& Current();
// Changed (the editor's panel, undo, a layout put in place): the time of day, the ambient light and the fog shown at
// once; the rest on reload.
void Set(const Props& props);
// A level is up (Layout::Tick, as shipped or the edited copy - before the structure is put back): its structure's
// ed_ambient / ed_fog taken (not in a network session - `network`) and sent to the engine.
void OnLevelUp(bool network);
// The ambient light and fog in effect now (edited over the level's ed_ keys) - sent again through the time-of-day
// manager (its ApplyTODParams with its current parameters, our Override over them).
void Reapply();
// The level's own values, from its structure (what the panel shows when a property isn't set): its ambient / fog
// those the time-of-day manager's current state has (or the level structure's ed_ keys).
bool LevelDefaults(Props* props);
// A new level: forgotten.
void ForgetLevel();
// The edited copy chosen (`level`: its level structure): its properties written into the structure (the old values
// kept); Restore puts them back (once the level is up, or when another level is chosen).
void WriteIntoStructure(Script::CStruct* level, const Props& props);
void RestoreStructure();
// The edited copy is up: the skater put at its start.
void OnEditedCopyUp(const Props& props);
// The skies there are: every level structure's sky= (the game's Levels.qb and the overlay's ported levels), each once
// (case doesn't matter), sorted - made once a level.
const std::vector<std::string>& Skies();

} // namespace Front::LevelEditor::LevelProps
