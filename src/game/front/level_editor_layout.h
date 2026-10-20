#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor_scenery.h"

// NOT ORIGINAL - the level editor's saved layouts (ours; see level_editor.h): what the editor changed in a level,
// written to thug2-decomp-layouts\<the level's scene>.txt next to the exe (Ctrl+S in the editor) and put back each
// time that level loads. Not in network games: the other players' levels would differ.
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

// --- autosave ---------------------------------------------------------------------------------------------------
//
// While the editor has unsaved changes, the level's layout as it is now is also kept in
// thug2-decomp-layouts\<scene>.autosave.txt (the same format): written every 60 s and on leaving the editor, and from
// the crash logger (hook/log.h SetCrashHook - a text made after each change, written with no allocation). Never over
// <scene>.txt: Ctrl+S saves there and deletes the autosave. Entering the editor with an autosave newer than the saved
// layout that this run didn't write (a crash, or the game closed without saving) offers it: Ctrl+R puts it in place of
// the current layout, Ctrl+Shift+R deletes it. While it is offered, nothing is autosaved over it.

// The files' names (pure): "<8 hex digits>.txt" and "<8 hex digits>.autosave.txt".
std::string SavedFileName(uint32_t scene);
std::string AutosaveFileName(uint32_t scene);
// Whether an autosave is worth offering (pure): there is one, and it was written after the saved layout (or there is
// none). Times: the files' last-write times (FILETIME as a number).
bool AutosaveNewer(bool haveAutosave, uint64_t autosaveTime, bool haveSaved, uint64_t savedTime);

// The game side.
// Ctrl+S: the level's changes written (false: nothing to write to - not in a level; `message` says what happened);
// the level's autosave deleted.
bool Save(std::string* message);
// The level's layout as it is now made into the autosave's text (after each change; false: not in a level). Kept
// until WriteAutosave / the crash hook writes it.
bool Snapshot();
// The snapshot written to the autosave, if it changed since it was last written (false: nothing written - nothing new,
// or held back while an older autosave is offered, or it couldn't be written).
bool WriteAutosave();
// From the crash logger: the snapshot written as it is (no allocation; nothing if it was already written).
void WriteAutosaveFromCrash();
// Entering the editor: whether this level has an autosave to offer (newer than its saved layout, not this run's).
bool AutosaveOffered();
// Ctrl+R: the current layout put back as the level has it, then the autosave's put in place (false: none, or it
// couldn't be read). Ctrl+Shift+R: the autosave deleted. Either way it isn't offered again.
bool RestoreAutosave(std::string* message);
bool DiscardAutosave(std::string* message);
// How many times a level's scenes have loaded (the editor's unsaved changes belong to one level).
int LevelLoads();
// Ctrl+O / a level loading: the saved layout put back (false: none, or not in a level).
bool Load(std::string* message);
// A scene loaded (Nx::CFunc_LoadScene): once the level is up, its layout is put back (Tick).
void OnSceneLoaded(const char* name, bool isNet);
// Once a frame (the editor's Update, on or off).
void Tick();

} // namespace Front::LevelEditor::Layout
