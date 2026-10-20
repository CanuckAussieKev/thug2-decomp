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
//   - its ambient light colour (ambient_red / _green / _blue) and fog (fog_red / _green / _blue / _alpha / _dist):
//     level structure keys as the levels set them (Levels.qb; the converters write the same keys) - on reload.
// How: LevelEditor_PickLevel (the level select chose the edited copy, before load_level runs) writes them into the
// level's structure (Level_<x>, which load_level includes - its values are read as the load goes), and once the edited
// copy is up (Layout::Tick) the structure is put back as the level has it: a level loaded any other way never sees
// them. Opened with -edit or the pause menu's LEVEL EDITOR, the level was already loaded as shipped: the time of day
// shows at once, the rest when the edited copy is next loaded from the level select.
//
// Layout record (version 2 still; an older reader skips the kind):
//   level [name=<text>] [start=<node>] [tod=<state>] [time=0|1] [ambient=r,g,b] [fog=r,g,b,alpha,distance]
// <text> %-escaped (level_editor_fields.h); <node> 8 hex digits.
namespace Front::LevelEditor::LevelProps {

// --- the pure parts (self-tested: src/hook/selftests_editor_fields.cpp) -------------------------------------------

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
};
bool Same(const Props& a, const Props& b);
bool Empty(const Props& props);
// The record's line ("" when there's nothing to keep), and one read (after the word "level"; false: malformed).
std::string WriteRecord(const Props& props);
bool ReadRecord(std::istringstream& in, Props* props);
// The level structure's keys the properties set, with their values (in this order).
std::vector<std::pair<const char*, Fields::Value>> StructureValues(const Props& props);
// A colour channel / distance typed in, kept in range.
int Channel(int value);
int Distance(int value);

// --- the game side ------------------------------------------------------------------------------------------------

// The edited copy's properties now (as edited in this session, or as its layout put them).
const Props& Current();
// Changed (the editor's panel, undo, a layout put in place): the time of day shown at once; the rest on reload.
void Set(const Props& props);
// The level's own values, from its structure (what the panel shows when a property isn't set).
bool LevelDefaults(Props* props);
// A new level: forgotten.
void ForgetLevel();
// The edited copy chosen (`level`: its level structure): its properties written into the structure (the old values
// kept); Restore puts them back (once the level is up, or when another level is chosen).
void WriteIntoStructure(Script::CStruct* level, const Props& props);
void RestoreStructure();
// The edited copy is up: the skater put at its start.
void OnEditedCopyUp(const Props& props);

} // namespace Front::LevelEditor::LevelProps
