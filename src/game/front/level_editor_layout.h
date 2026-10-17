#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor_scenery.h"

// NOT ORIGINAL - the level editor's saved layouts (ours; see level_editor.h): what the editor changed in a level,
// written to thug2-decomp-layouts\<the level's scene>.txt next to the exe (Ctrl+S in the editor) and put back each
// time that level loads (or with Ctrl+O). Not in network games: the other players' levels would differ.
//
// The file is text, a record a line ('#' starts a comment; unknown records are skipped, so later versions can add
// some):
//   thug2-decomp layout 1
//   scene <checksum>                                   the level's main scene (its sectors' table)
//   sector <checksum> <quarters> <x> <y> <z> <hidden>  a sector moved by (x, y, z) after `quarters` quarter turns
//   rail <name> <quarters> <x> <y> <z> <hidden>        a grind rail node, the same
//   clone <source> <quarters> <x> <y> <z>             a copy of a sector, placed relative to its source as loaded
//   object <id> <x> <y> <z> <9 matrix numbers>         an object's position and orientation (rows: right, up, at)
//   copy <name> <source> <x> <y> <z> <9 numbers>       a copy of an object (made from its source's node)
// Checksums are 8 hex digits; numbers are written with 9 significant digits (floats survive the round trip).
namespace Front::LevelEditor::Layout {

struct ObjectRecord {
    uint32_t id;
    float pos[3];
    float rows[9];
};
struct CopyRecord {
    uint32_t name;
    uint32_t source;
    float pos[3];
    float rows[9];
};
struct CloneRecord {
    uint32_t source;
    Placement placement;
};
struct File {
    uint32_t scene = 0;
    std::vector<Scenery::SectorState> sectors;
    std::vector<Scenery::RailState> rails;
    std::vector<CloneRecord> clones;
    std::vector<ObjectRecord> objects;
    std::vector<CopyRecord> copies;
};

// The text of a layout, and a layout read back (false: not a layout file; `line` the first line that isn't
// understood).
std::string Write(const File& file);
bool Read(const std::string& text, File* file, int* line);

// The game side.
// Ctrl+S: the level's changes written (false: nothing to write to - not in a level; `message` says what happened).
bool Save(std::string* message);
// Ctrl+O / a level loading: the saved layout put back (false: none, or not in a level).
bool Load(std::string* message);
// A scene loaded (Nx::CFunc_LoadScene): once the level is up, its layout is put back (Tick).
void OnSceneLoaded(const char* name, bool isNet);
// Once a frame (the editor's Update, on or off).
void Tick();

} // namespace Front::LevelEditor::Layout
