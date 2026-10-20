#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_level_props.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_triggers.h"
#include "game/front/level_editor_walls.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// NOT ORIGINAL - the level editor's saved layouts (ours; see level_editor.h): what the editor changed in a level,
// written to thug2-decomp-layouts\<the level's scene>.txt next to the exe (Ctrl+S in the editor).
//
// A layout is the level's EDITED COPY - a level of its own, never the level itself (the user's rule, 2026-10-20):
//   - the level as shipped always loads as shipped: picked from the level select's own entries, by -level / the
//     automation, by career or any script, it gets no layout;
//   - the edited copy is the level select's "<LEVEL> (EDITED)" entry, in the section EDITED LEVELS (the last; Up from
//     the first) - made by the overlay (tools/menu_level/level_select.ts) from LevelEditor_EditedLevels: one entry for
//     each level with a saved layout. Choosing it runs level_select_change_level with `edited_layout`, whose first
//     line (the overlay's) is LevelEditor_PickLevel: the request (that load script, edited or not) is kept, and the
//     layout is put in place once that level is up (Tick) - or at once, when the level picked is the one already
//     loaded (the level select doesn't load it again: the level switches in place between shipped and edited);
//   - LEVEL EDITOR in the pause menu of a level as shipped (or -edit <level>) edits its edited copy: the level becomes
//     the edited copy (its saved layout put in place first, if it has one; the file is made by the first Ctrl+S). The
//     toolbar (and the old HUD's top line) say which one is being edited. One edited copy per level (its scene).
//   - Network: never. No layout is put in place, and the editor doesn't start, while a network session is on (online,
//     a network game - host or client, LAN or online - or a scene loaded for one), whatever entry was picked; the
//     EDITED LEVELS section isn't offered in the network level select (in_server_options) or in a network game, and
//     LevelEditor_PickLevel refuses `edited_layout` there.
//   - Older layout files (no change to the format) are the edited copies of their levels.
//
// The file is text, a record a line ('#' starts a comment; unknown records are skipped, so later versions can add
// some):
//   thug2-decomp layout 2                              the version: 1 had no `set` records; both are read (an older
//                                                      DLL, which knows only 1, refuses a version 2 file as a whole)
//   scene <checksum>                                   the level's main scene (its sectors' table)
//   sector <checksum> <quarters> <x> <y> <z> <hidden>  a sector moved by (x, y, z) after `quarters` quarter turns
//          [scale=<s> | scale=<x>,<y>,<z>]
//   rail <name> <quarters> <x> <y> <z> <hidden>        a grind rail node, the same (moved with its scenery, or by
//                                                      itself / its whole rail - level_editor_node_edit.h)
//   clone <source> <quarters> <x> <y> <z>             a copy of a sector, placed relative to its source as loaded
//   object <id> <x> <y> <z> <9 matrix numbers>         an object's position and orientation (rows: right, up, at)
//          [scale=...]                                 and its scale along its own axes (level_editor_scale.h)
//   copy <name> <source> <x> <y> <z> <9 numbers>       a copy of an object (made from its source's node)
//          [scale=...]
//   set <node> pos=<x>,<y>,<z> angles=<x>,<y>,<z>      (version 2) a level node's pos= / angles= (a point node moved or
//                                                      turned - level_editor_node_edit.h); either may be left out,
//                                                      other key=value fields are skipped (for later versions)
//   node <name> <Class> type=<T> pos=x,y,z angles=x,y,z [types=A,B] [color=r,g,b] [brightness=b] [inner=i]
//        [outer=o] [radius=r]                            (version 2, phase 3) a node the editor added
//                                                      (level_editor_node_create.h: the rest of the node follows from
//                                                      these); in the overlay's order - the order added crowns count in
//   order crown <name> <name>...                       the added crowns' order (reordered: as the node records')
//   hide <node>                                        a level's node hidden (not removed: its indexes stay)
//   lock sector <checksum> / lock object <id>          a scenery piece / an object locked: clicks pass it
//                                                      (level_editor_picking.h)
//   trigger <name> <kind> pos=x,y,z yaw=r size=x,y,z ...  a trigger volume the editor added (level_editor_triggers.h:
//                                                      its node is the overlay's, not a `node` record)
// An older (phase 2) reader skips node / order / hide records; an older reader of version 2 skips lock and trigger
// records.
//   point <name> <Class> pos=x,y,z [like=<node>] [terrain=<T>] [links=<node>,...]
//                                                      (phase 4) a rail / path point the editor added
//   links <node> <node>,... | -                        (phase 4) a level node's links= as edited (a chain relinked, a
//                                                      ped's / car's path)
//   drop <node>                                        (phase 4) a level's rail / path point deleted (hidden, relinked)
//   terrain <node> <TERRAIN_...>                       (phase 4) a level rail point's terrain (level_editor_chains.h)
//   set <node> ... f.<field>=<value>...                (phase 6) a level node's other fields as edited in the
//                                                      Inspector (level_editor_fields.h: typed values, `del`)
//   level [name=<text>] [start=<node>] [tod=<state>] [time=0|1] [ambient=r,g,b] [fog=r,g,b,a,d]
//                                                      (phase 6) the edited copy's level properties
//                                                      (level_editor_level_props.h)
//   wall <checksum> <quarters> <x> <y> <z> <off>       a sector's collision (level_editor_walls.h): an invisible wall's
//                                                      moved / turned as a sector's, and whether it's switched off;
//                                                      a drawn piece's collision switched off by itself (no move: its
//                                                      sector record moves it)
//   ... scale=<s>                                      (after a sector / rail / clone / object / copy record's numbers,
//   ... scale=<x>,<y>,<z>                              when not 1) a uniform or a per-axis scale (level_editor_scale.h):
//                                                      the point x as loaded is at QuarterTurn((x.x * sx, x.y * sy,
//                                                      x.z * sz), quarters) + (x, y, z); an object's along its own
//                                                      axes, about its position (its model and its collision)
// An older (phase 2) reader skips node / order / hide records; an older reader of version 2 skips lock records and
// the phase 4 ones (an added rail's points aren't `node` records: an older reader would make them without their rail),
// and reads past a record's scale= (the piece then placed unscaled); an older reader of version 2 skips wall records (the
// walls then as the level has them). A reader before the per-axis scale refuses a file with a scale=<x>,<y>,<z> on a
// sector / rail / clone record (a uniform scale is still written scale=<s>); it ignores an object's / copy's scale.
// Checksums are 8 hex digits; numbers are written with 9 significant digits (floats survive the round trip).
namespace Front::LevelEditor::Layout {

struct ObjectRecord {
    uint32_t id;
    float pos[3];
    float rows[9];
    V3 scale = {1.0f, 1.0f, 1.0f};
};
struct CopyRecord {
    uint32_t name;
    uint32_t source;
    float pos[3];
    float rows[9];
    V3 scale = {1.0f, 1.0f, 1.0f};
};
struct CloneRecord {
    uint32_t source;
    Placement placement;
};
struct SetRecord {
    uint32_t name;
    bool hasPos, hasAngles;
    float pos[3];
    float angles[3];
    // (phase 6) the node's other fields as edited: f.<field>=<value> (level_editor_fields.h)
    std::vector<Fields::Edit> fields;
};
// The version written.
constexpr int kVersion = 2;
struct File {
    int version = kVersion; // (the file's, as read)
    uint32_t scene = 0;
    std::vector<Scenery::SectorState> sectors;
    std::vector<Scenery::RailState> rails;
    std::vector<CloneRecord> clones;
    std::vector<ObjectRecord> objects;
    std::vector<CopyRecord> copies;
    std::vector<SetRecord> nodes;
    std::vector<NodeCreate::Spec> added;  // `node` records, in order
    std::vector<std::string> crownOrder;  // `order crown`
    std::vector<uint32_t> hidden;         // `hide`
    std::vector<Picking::Lock> locks;     // `lock`
    std::vector<Triggers::Placed> triggers; // `trigger`
    Chains::State chains;                 // (phase 4) `point`, `links`, `drop`, `terrain`
    LevelProps::Props level;              // (phase 6) `level` (level_editor_level_props.h)
    std::vector<Walls::WallState> walls;  // `wall`: collision moved / switched off
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
// The saved layout put in place (false: none, or not in a level, or a network session).
bool Load(std::string* message);
// A scene loaded (Nx::CFunc_LoadScene): once the level is up, its layout is put back if it was picked as the edited
// copy (Tick).
void OnSceneLoaded(const char* name, bool isNet);
// Once a frame (the editor's Update, on or off).
void Tick();
// (phase 6) The node manager added a node array (SkateScript::SetNodeArrayAddedHook): the edited copy's level array
// gets its layout's field edits now, before the level's objects are made from it (level_editor_fields.h).
void OnNodeArrayAdded(uint32_t name);
// The edited copy's level properties: whether this level's saved layout names one (`props`) - for the level select.
bool SavedProps(uint32_t scene, LevelProps::Props* props);

// --- the edited copy (see the top) ---------------------------------------------------------------------------------

// The pure parts (self-tested: src/hook/selftests_editor_levels.cpp).
// Whether the level that came up (`levelLoad`: its load script's checksum) gets its layout: it was picked as the
// edited copy (`request`: that load script; 0 none) and no network session is on.
bool ApplyEditedCopy(uint32_t request, uint32_t levelLoad, bool network);
// The request once a level has come up: kept for that level (loaded again, it's still the edited copy), dropped for
// any other level or a network session.
uint32_t RequestAfterLoad(uint32_t request, uint32_t levelLoad, bool network);
// What LevelEditor_PickLevel keeps: the load script when `edited` is asked for and no network session is on, else 0.
uint32_t RequestFromPick(uint32_t load, bool edited, bool network);
// A network session from its signs: online, a network game, a scene loaded for one.
bool NetworkSession(bool online, bool netGame, bool netScene);
// The level select's text for an edited copy ("TRAINING" -> "TRAINING (EDITED)").
std::string EditedText(const char* text);
// A file of the layouts' folder that is a saved layout - "<8 hex digits>.txt", not an autosave - and its scene.
bool SavedLayoutScene(const std::string& fileName, uint32_t* scene);
// A level structure (levels.qb's Level_TR: load_script=Load_TR, level="TR") seen as `load` -> its scene: the scene's
// checksum is the checksum of its `level` text (CFunc_LoadScene names the scene by it).
uint32_t SceneOfLevelText(const char* level);

// The game side.
// Whether a network session is on now (layouts never apply then).
bool NetworkActive();
// The level loaded now is its edited copy (its layout put in place, or the editor opened in it).
bool EditingCopy();
// Whether this level has a saved layout (an edited copy in the level select).
bool HasSavedLayout();
// The level's name for the toolbar: its level structure's name= ("Training"), else the scene's name.
std::string LevelTitle();
// Entering the editor: the level becomes its edited copy - its saved layout put in place first if it wasn't (false,
// `message` why: not in a level, or a network session - the editor doesn't start).
bool EnterEditedCopy(std::string* message);
// Reset level: everything the editor changed in this level put back as the level has it (moved scenery, objects,
// rails, nodes; hidden things shown; copies and added nodes removed), and the saved layout and its autosave deleted.
// `before`: the layout as it was (Write's text) - PutBack puts it in place again (undo, this session; the files stay
// deleted until saved again). False: not in a level.
bool ResetLevel(std::string* before, std::string* message);
bool PutBack(const std::string& before, std::string* message);
// The level's changes as they are now, as a layout's text (false: not in a level) - redo of an undone reset.
bool CaptureText(std::string* text);

// Script commands (registered with the editor's: LevelEditor::RegisterScriptFunctions).
// LevelEditor_PickLevel level=<load script> [edited_layout]: the level select chose a level (the first line of
// level_select_change_level) - the edited copy or the level as shipped.
bool __cdecl CFunc_PickLevel(Script::CStruct* params, Script::CScript* script);
// LevelEditor_EditedLevels: the level select's entries for the edited copies (edited_list, edited_count) - each the
// level's own entry (from level_select_menu_level_info and the ported levels' lists) with the text "<TEXT> (EDITED)"
// and the flag edited_layout. False (nothing returned) when there are none or a network session is on.
bool __cdecl CFunc_EditedLevels(Script::CStruct* params, Script::CScript* script);

} // namespace Front::LevelEditor::Layout
