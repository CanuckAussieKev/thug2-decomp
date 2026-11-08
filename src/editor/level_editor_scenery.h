#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor.h"

namespace render {
class HitMesh;
class Geom;
struct MeshData;
class WorldCell;
struct WorldScene;
} // namespace render

// NOT ORIGINAL - the level editor's scenery (ours; see level_editor.h): picking, outlining and moving the level's
// static scene sectors.
//
// What a sector is (render::WorldCell, 0x1C bytes, vtable 0x00649A40): +0x08 flags (0x40: its collision is in the scene's
// super sectors, 0x20: moved - the super sectors to be updated), +0x10 its quarter turns, +0x14 its geometry (the PC
// CGeom, vtable 0x0064A368: +0x14 / +0x24 the bounding box's min / max as loaded, +0x3C / +0x40 its meshes and their
// count, +0x48 active, +0x4C an instance - null for level geometry), +0x18 its collision (StaticCollider, vtable
// 0x00645C18: +0x04 the sector's checksum). A scene (0x006FA7EC's list) finds its sectors by checksum in a hash
// table at +0xD0 (0x0049CB00).
//
// Moving one is what the park editor does with its pieces (0x00594730): the geometry's place_at_absolute_point (vfunc +0x48,
// 0x004B55D0: every mesh's MeshRef::change_place 0x004D6E00 moves its vertices by the change and uploads them), the
// collision moved by the same change (StaticCollider::place_at_absolute_point 0x00410C20 translates by the position given
// minus a constant origin, so it is handed the change), the sector flagged 0x20, and later the scene's
// UpdateSuperSectors (0x0049D780) puts its collision where it now is. Quarter turns: WorldCell::spin_around_up
// (0x0049E760: collision and meshes turned about the geometry's world position).
namespace frontend::level_editor::scenery {

// The sectors under a ray up to `max_t` along the (unit) direction: the game's collision ray (static collision only,
// skipping collision without geometry - invisible walls) and every shown piece's meshes' triangles (their boxes only to
// skip the rest quickly; a piece with collision too - its drawn parts the collision doesn't cover: TR's hanging lights'
// housings), each piece at its nearest. What's nearest occludes what's behind it (level_editor_picking.h's Resolve):
//   - `checksum` / `t`: what a plain click takes - the first pickable piece with nothing occluding in front of it;
//   - `blocked` / `block_t_`: the first occluding surface (a collision face met, or an opaque mesh - whatever the piece:
//     pickable, structure, locked);
//   - `passed` / `passed_t_`: the nearest piece a plain click passes (structure, see-through; not locked) - Alt+click;
//   - `nearest` / `nearestT`: the nearest pickable piece, occlusion aside (not locked) - Alt+click.
// Hidden pieces (H) aren't met at all. True when anything but a locked piece was found.
struct Hit {
    uint32_t checksum = 0;
    float t = 0.0F;
    uint32_t passed = 0;
    float passed_t_ = 0.0F;
    bool blocked = false;
    float block_t_ = 0.0F;
    uint32_t nearest = 0;
    float nearest_t_ = 0.0F;
};
// `layers` (click-to-cycle): every piece the ray meets (not locked), nearest first, each once.
struct Layer {
    uint32_t checksum;
    float t;
    bool passed;
};
bool get_pick(V3 origin, V3 unit_dir, float max_t, Hit* hit, std::vector<Layer>* layers = nullptr);
// Every mesh see-through (Picking::material_see_through: drawn transparent, or added / subtracted / modulated /
// brightened onto what's behind): a decal, a web, a shadow, a glow.
bool get_see_through(uint32_t checksum);
// What a plain click passes (level_editor_picking.h): structure for a camera at `camera` (with the piece's level top -
// get_level_share), or see-through.
bool get_passed(uint32_t checksum, V3 camera);
// The level part of a piece's upward-facing surface (Picking::get_level_share; 1 without geometry). Measured once a piece
// while the editor runs.
float get_level_share(uint32_t checksum);

// A sector's box where it is now (false: no such sector, or no geometry).
bool cuboid(uint32_t checksum, OrientedBox* box);
// Why it can't be moved (null: it can).
const char* why_not_movable(uint32_t checksum);
// For the HUD: "scenery <name> <checksum> ..." for the hovered sector.
std::string describe_hover(uint32_t checksum);
std::string describe_selection();

// The selection: sectors (not objects).
bool has_selection();
const std::vector<uint32_t>& get_selection();
bool get_selected(uint32_t checksum);
void clear_selection();
// The sector, the rest of its cluster (the node's TrickObject Cluster=) and the pieces built onto them (piece_fits /
// piece_touches: the fire truck's ladder) that can be moved, their shadows (is_shadow) - and their grind rails (rail nodes of those clusters, or
// lying within a selected sector's box), which move and turn with them.
void select_group(uint32_t checksum);
// What select_group would select for that sector (kept while it stays the same: the hover's highlight).
const std::vector<uint32_t>& hover_group(uint32_t checksum);
// Alt+click: one sector in or out (the rails chosen again). Also Ctrl+click and the Inspector's x on a selected piece.
void flip_over(uint32_t checksum);
// A group's shadows (level_editor_shadows.h) are selected with it: whether a sector has been taken as one (`caster`:
// the member whose name it carries, or the one it lies under when taken by place). They move, turn, scale and undo with the rest; the pivot
// and the bottom are the rest's.
bool is_shadow(uint32_t checksum, uint32_t* caster = nullptr);
// The selection's rail points carried along (by name); one dropped from them (Ctrl+click on its gizmo, the Inspector's
// x: not carried again until another group is selected).
const std::vector<uint32_t>& selected_rail_names();
bool carries_rail(uint32_t name);
void drop_rail(uint32_t name);
// The Inspector's "Keep only this": that sector alone (its rails with it).
void keep_only(uint32_t checksum);
// The selection's rails: each segment (a rail node to the next) where it is now; how many rail nodes.
std::vector<std::pair<V3, V3>> selected_rails();
int selected_rail_count();

// Grind rail points one by one (the node layer and the node edits - level_editor_nodes.h, level_editor_node_edit.h).
// The rail manager's point is where skaters grind (and where a moved sector's rails were taken): the node array's pos=
// stays as the level has it, so these are what the gizmos, the Inspector and the moves use.
// A rail point's place now (false: no such point - not a rail node, or the level's rails ride an object). `climbing`:
// a ladder's point (a ClimbingNode) is one too.
bool get_rail_point(uint32_t name, V3* pos, bool climbing = false);
// Where the level has it (before the editor moved it, by its scenery or by itself).
bool rail_origin(uint32_t name, V3* pos);
// Put at `pos` (from where the level has it: a plain move - the `rail` record of a layout), its segments' boxes (the
// one from it and the one to it) made again. False: no such rail point.
bool set_rail_point(uint32_t name, V3 pos);
// Its rail's points: `name` first, then the rest of the rail from its first point to its last (the rail manager's
// links: next / prev).
std::vector<uint32_t> rail_of(uint32_t name);
// A number that changes whenever rail points move (the node layer reads their places again).
uint32_t rail_generation();
// The rail manager's nodes were made again (level_editor_chains.h - its rebuild_rails): the rail points the editor tracks
// found again by name (those gone forgotten), and the generation moved on.
void rails_rebuilt();
// Every rail and ladder point with its place now (the node layer).
void for_each_rail_point(void (*each)(void* context, uint32_t name, V3 pos), void* context);
// The editor starting: no selection, node lookups forgotten.
void get_reset();

// The selection's pivot (the centre of its boxes when selected, moved with it) and moving it there; turning it about
// the pivot by quarter turns. Both move the sectors at once (vertices and collision); Commit updates the scenes' super
// sectors (collision lookups) - after a drag, not every frame.
V3 get_pivot();
// The height of the selection's bottom (its boxes' lowest point, moved with it).
float get_bottom();
// Exactly these sectors selected (those that can still be moved; their rails with them) - undo.
void set_selection(const std::vector<uint32_t>& sectors);
void move_to(V3 pivot);
void get_turn(int quarters);
// Turned by `radians` about the pivot - any angle (whole quarter turns as get_turn; the rest written into the vertices
// and collision: level_editor_scale.h). False (nothing turned) when a piece can't take it: compressed collision too
// wide to stay within its 16-bit range at every angle (can_turn_freely).
bool turn_by(float radians);
bool can_turn_freely();
// The first selected piece's heading (its placement's quarter turns and twist, radians).
float selection_heading();
// Scaling (level_editor_scale.h): the selection's scale (its first piece's, along its own axes as first touched; 1, 1, 1
// when not scaled) and quarter turns, and the selection scaled by `factor` along the world's axes about the middle of
// its bottom - limited so every piece stays within 0.25x..4x on each axis (less for compressed collision); `uniform`:
// the same factor on every axis (the most any axis allows). Returns the factor used (1, 1, 1: none). Vertices and
// collision at once, as Turn; Commit after.
V3 selection_scale();
int selection_quarters();
V3 scale_selection(V3 factor, bool uniform);
void finalise();
// A sector's collision moved by something else (level_editor_walls.h: an invisible wall): flagged for the super sectors'
// update as moves are, its scene updated at the next Commit.
void note_collision_moved(uint32_t checksum);
// How many sectors have been moved since the level loaded.
int get_moved_count();

// The selection's state: which sectors are selected, where the editor has put each (relative to where the level has
// it) and whether it's hidden, and the same for its rails. Undo / redo save it before a change and restore it; reset
// is every placement back to none, hiding is every piece hidden (WorldCell::change_live: neither drawn nor collided with -
// its rails turned off too).
struct SectorState {
    uint32_t checksum{};
    Placement placement;
    bool hidden{};
};
struct RailState {
    uint32_t name{};
    Placement placement;
    bool hidden{};
};
struct State {
    std::vector<uint32_t> selection;
    std::vector<SectorState> sectors;
    std::vector<RailState> rails;
};
State save_state();
void restore_state(const State& state); // (selects state.selection again; collision updated)
// Those states put in place without touching the selection (layouts); collision updated.
void apply_states(const std::vector<SectorState>& sectors, const std::vector<RailState>& rails);
// What the editor has changed in this level: sectors moved or hidden (copies left out), rails moved or turned off.
std::vector<SectorState> changed_sectors();
std::vector<RailState> changed_rails();
// The level's main scene (the first that isn't a sky) and a scene's name.
render::WorldScene* main_scene();
// Whether a scene is still one of the level's (in the scene list, not a sky).
bool scene_live(render::WorldScene* scene);
uint32_t world_scene_key(render::WorldScene* scene);

// Copies of sectors (Ctrl+D, the browser): a full copy (WorldScene::duplicate_scene_sector 0x0049DF30, as the park editor makes its
// pieces) of the source as it is now, in its scene, ready to move like any sector. Its checksum (0: not made).
uint32_t get_duplicate(uint32_t source);
// The copies made this level: the copy, its source, and where the source had been put when it was copied (a copy's
// place relative to the source as loaded is its placement after that one - Compose).
struct Replicate {
    uint32_t checksum{};
    uint32_t source{};
    Placement source_at_copy_;
};
std::vector<Replicate>& get_clones();
bool is_clone(uint32_t checksum);
// Where the editor has put a sector (none: false), and whether it's hidden.
bool placement_now(uint32_t checksum, Placement* placement);
bool get_hidden(uint32_t checksum);
// Shift+click: a sector's group added to the selection.
void add_group(uint32_t checksum);
// The browser's pieces: the main scene's shown sectors that can be moved (copies left out).
std::vector<uint32_t> movable_pieces();
// Home: the selection back where the level has it (and shown).
void reset_selection();
// Delete: the selection hidden, and deselected.
void hide_selection();
// Where the first selected sector has been put (false: not moved).
bool selection_moved(Placement* placement);

// [features] debug_editor: the scenes and their sectors.
void log_census();

// The geometry of a sector (null: none).
render::Geom* sector_geometry(uint32_t checksum);
// A PC geom's meshes as the graphics backend lists them (render::Device::geom_mesh_count / geom_mesh / geom_mesh_list):
// none without a mesh array (or for another kind of geom); `list` names the array (it changes when the meshes are made
// again).
struct GeomMeshes {
    const render::Geom* geom = nullptr;
    uint32_t count = 0;
    const void* list = nullptr;
    [[nodiscard]] render::MeshData* at(uint32_t index) const;
};
GeomMeshes meshes_of(const render::Geom* geom);
// The sector of that checksum (null: none), and every sector of the level's scenes (skies left out) - the view
// options (level_editor_view.h) read their collision and visibility.
render::WorldCell* sector_of(uint32_t checksum);
void for_each_level_sector(void (*each)(void* context, uint32_t checksum, render::WorldCell* sector), void* context);

// Highlighting: geometry tinted with the engine's own colour (CGeom::change_tint, vfunc +0x18 - what SetObjectColor uses
// for graffiti: each mesh's colour, or an instance's), and put back exactly as it was when no longer wanted. Each
// frame: Tint what should show, then end_tints puts back the rest (`all`: everything - the editor stopping). Who owns
// a geometry: a sector (`index` -1, `id` its checksum) or an object (its id, the geometry's index in its model);
// `resolve` finds that owner's geometry now, and a geometry is only put back when it is still that.
struct TintOwner {
    uint32_t id;
    int index;
};
bool tintable(void* geom); // (the PC geometry class)
void get_tint(TintOwner owner, void* geom, uint32_t rgba);
void end_tints(void* (*resolve)(TintOwner), bool all);

} // namespace frontend::level_editor::scenery
