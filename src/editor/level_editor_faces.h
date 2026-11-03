#pragma once

#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "engine/render/coll_obj.h"

struct ImFont;

// NOT ORIGINAL - the level editor's collision face flags (ours; see level_editor.h - the feature map's "per-face
// collision flags" row): with the collision shown, faces are picked one at a time (K: Faces mode - a click takes a
// face, Shift+click adds / removes one) or a whole piece at once (the Inspector's All faces), and their face flags set
// or cleared in the Inspector: skatable 0x01, not skatable 0x02, wallride 0x04, vert 0x08, not collidable 0x10,
// trigger 0x40, camera passes 0x80, not taggable 0x2000 (what each does: level_editor_view.h's Classify notes -
// the skater's feeler check 0x005003A0, the feelers' ignore masks).
//
// Where: the face's u16 flags (TriMeshSource face +0) in the sector's collision, written live - the feelers read
// them at every query (StaticCollider's line tests take the face's flags from the geometry; the collision cache holds
// objects, not faces). They are kept through everything the editor does to the piece: its moves and turns
// (TriMeshSource::Slide / RotateY move vertices and the tree only), its scale (level_editor_scale.h rebuilds the
// vertices from its copy - faces untouched), and its shared collision (level_editor_shared_coll.h): a face taken along by
// a moved piece's companion is edited in the companion (and in the flags given back when the companion goes) - the
// original keeps its 0x10 while it is taken. A copy (Ctrl+D) copies its source's faces as they are (the engine's
// duplicate_scene_sector); a copy's own faces can't be edited (its checksum is new each session: nothing to save).
//
// A face is named by its sector (the collision object's checksum - the level's, never a copy) and its index in that
// collision as loaded (the .col file's order: apply_layout.ts bakes it there). Undo: one step a change (the faces'
// flags before). Home with faces selected puts them back as the level has them; Reset level puts every face back.
//
// Layout record (version 2 still; an older reader skips the kind):
//   faces <checksum> <face>:<flags>,<face>:<flags>...    the faces' flags as edited (4 hex digits), faces ascending
namespace frontend::level_editor::faces {


// --- the pure parts ------------------------------------------------------------------------------------------------

// The flags the Inspector offers, in its order.
struct FlagInfo {
    uint16_t bit;
    const char* name;
    const char* what; // (what the game does with it - the tooltip)
};
constexpr int kFlagCount = 8;
const FlagInfo& bit_mark(int index);
// The flags set and cleared: (flags | set) & ~clear.
uint16_t get_applied(uint16_t flags, uint16_t set, uint16_t clear);
// A flag over several faces (the Inspector's checkbox): -1 none, 0 clear on all, 1 set on all, 2 mixed.
int get_combined(const std::vector<uint16_t>& flags, uint16_t bit);
// What the original face carries while a companion has taken it along (not collidable: 0x10).
inline uint16_t while_taken(uint16_t flags) { return static_cast<uint16_t>(flags | render::kFaceNotCollidable); }

struct FaceKey {
    uint32_t checksum;
    uint16_t face;
    bool operator==(const FaceKey& o) const { return checksum == o.checksum && face == o.face; }
    bool operator<(const FaceKey& o) const { return checksum != o.checksum ? checksum < o.checksum : face < o.face; }
};
// A face clicked: `add` (Shift) toggles it in the selection, else the selection is just it. Returns whether it is
// selected now.
bool get_click(std::vector<FaceKey>* selection, FaceKey key, bool add);

// A sector's faces as edited (the record): face index -> flags, ascending.
struct SectorFaces {
    uint32_t checksum = 0;
    std::vector<std::pair<uint16_t, uint16_t>> faces;
};
// The records' text ("" for none) and one read (after the word "faces"; false: malformed - a face twice, a flags value
// that isn't 1-4 hex digits, nothing listed).
std::string write_records(const std::vector<SectorFaces>& sectors);
bool read_record(std::istringstream& in, SectorFaces* out);
// Faces whose flags differ from the level's (each face, the level's flags, the flags now) grouped into records: by
// sector ascending, faces ascending; those back as the level has them left out.
struct Change {
    FaceKey key;
    uint16_t origin, now;
};
std::vector<SectorFaces> group_set(const std::vector<Change>& changes);

// --- the game side ------------------------------------------------------------------------------------------------

// Faces mode (K): clicks in the view take faces (the collision view is switched on with it).
bool get_picking();
void set_picking(bool on);
// The editor starting / stopping (stopping: Faces mode off, the selection dropped).
void kickoff();
void halt();
// The selection.
const std::vector<FaceKey>& get_selection();
bool has_selection();
void clear_selection();
// A click in Faces mode (`add`: Shift): the face under the ray. False: no face drawn there.
bool click_at(V3 origin, V3 unit_dir, bool add);
// The face under the cursor in Faces mode (drawn outlined; none: Hover with no ray).
void get_hover(V3 origin, V3 unit_dir, bool have_ray);
// Every face of a piece's collision selected (`add`: added to the selection) - its own collision's and the faces its
// shared collision took along. False: it has none the editor can edit.
bool select_piece(uint32_t checksum, bool add);
// The selected faces' flags: `bit` set (on) or cleared - one undo step. False: nothing changed.
bool set_flag(uint16_t bit, bool on);
// Home with faces selected: they're put back as the level has them (one undo step). False: no faces selected.
bool home_selection();
// The flags of a face now (as the skater meets it: a taken face's in its companion). False: no such face.
bool get_take_in(FaceKey key, uint16_t* flags);
// Layouts: the faces changed now; records put in place (how many faces); everything put back; a new level (forgotten,
// nothing written).
std::vector<SectorFaces> get_changed();
int get_apply(const std::vector<SectorFaces>& sectors);
void reset_all();
void forget_level();
// Once a frame while the editor runs: what the Inspector asked for.
void tick_frame();
// The selected faces (cyan) and the hovered one (yellow) outlined over the view.
void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines);
// The Inspector's section (the selected faces: their flags as checkboxes, All faces of the piece, Home); `mono` /
// `small`: the UI's fonts.
void inspector_section(ImFont* mono, ImFont* small_font);
// The status line: what Faces mode is doing.
std::string describe();

} // namespace frontend::level_editor::faces
