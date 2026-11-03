#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "engine/render/coll_obj.h"

// NOT ORIGINAL - the level editor's view options (ours; see level_editor.h): what the editor shows, never what the game
// does. Nothing here is saved in a layout or changes play; everything is put back when the editor closes.
//
//   - Show collision (C, the toolbar's Collision; [editor] collision=0/1 in thug2-decomp.ini): the level's collision
//     near the camera drawn over the view as see-through triangles (ImGui's background draw list, as the gizmos - not
//     the renderer), each face coloured by what it is for the skater (Classify below). Collision with nothing to draw
//     (a sector whose geometry is null - invisible walls, blockers) in a strong magenta, with its edges. Read from each
//     sector's collision (StaticCollider, vtable 0x00645C18: +0x08 flags - 4 off -, +0x0C its TriMeshSource, whose
//     vertices are where the sector is now: moving collision moves them), cached per sector and made again when its
//     box changes (the editor moved it). The nearest sectors first, up to kMaxTriangles drawn, within kRadius (the
//     collision-only ones within kWallRadius).
//   - Invisible walls: with the collision shown, Shift+click on a magenta face selects that collision-only sector -
//     its name, box, faces and their flags in the Inspector, its faces outlined. Delete switches its collision off
//     (Delete / Home / the Inspector's Restore on again), and it moves and turns like scenery (level_editor_walls.h).
//     Collision switched off (a wall, or a drawn piece's by the Inspector's Collision) stays drawn faded, a wall's edges
//     dashed, so it can be found and switched on again.
//   - Hide (H) / Isolate (I or /) / Show all (Alt+H): the selected scenery and objects hidden from the view and from
//     clicks, or everything else hidden; while editing only. Scenery: the sector's visibility mask (WorldCell::
//     toggle_shown 0x0049E680 / fetch_shown 0x0049E690 -> the geometry's +0x44 and each mesh's viewport bits +0x1B,
//     which the scene render tests - what the LevelGeometry Visible / Invisible node commands set, 0x005A3B80) - not
//     its active flag (WorldCell::change_live also turns its collision off, and is what the editor's Delete and the layouts
//     use). Objects: their model's geometries turned off (the geometry's change_live, vfunc +0x34 - for an instance its
//     +0x4C; the model's suspend pass 0x00493C60 turns them on and off when its decision changes, so Tick turns hidden
//     ones off again every frame) - their collision and logic untouched.
namespace frontend::level_editor::view {

// --- the pieces (pure) --------------------------------------------------------------------------------------------

// What a collision face is for the skater, from its face flags (TriMeshSource face +0: u16) - in this order (the
// first that applies):
//   kInvisibleWall: the sector has collision but nothing drawn (its geometry is null);
//   kTrigger:       0x40 - a trigger face (0x004131F0 notes it in the hit, +0x49); with 0x10 a trigger volume the
//                   skater passes through (kill zones, water: the feeler at 0x004E5390 asks for exactly 0x50 faces);
//   kNoCollide:     0x10 - not collidable (every feeler's default ignore mask, 0x00417760 / 0x00417250);
//   kWallride:      0x04 - wall-ridable (the skater's feeler check 0x005003A0: not skatable, +0x22A set);
//   kVert:          0x08 - vert (0x005003A0: skatable, +0x229 set);
//   kNotSkatable:   0x02 - not skatable (0x005003A0, unless 0x01 or 0x08);
//   kSkatable:      0x01 - skatable whatever its slope (0x005003A0);
//   kPlain:         none of those: skatable when flat enough (0x005003A0 compares the slope with a physics angle).
enum class Surface : uint8_t {
    kInvisibleWall,
    kTrigger,
    kNoCollide,
    kWallride,
    kVert,
    kNotSkatable,
    kSkatable,
    kPlain,
    kCount
};
Surface get_classify(render::FaceFlag face_flags, bool collision_only);
// The colour drawn (ARGB, see-through) and the legend's name.
uint32_t surface_colour(Surface surface);
const char* surface_name(Surface surface);
// Face flags other passes read (the legend's notes, not coloured): 0x80 the camera's feeler ignores (0x004E4425:
// set_excluded(0x90) - the camera goes through), 0x1000 / 0x2000 the graffiti feelers ignore (0x0051DF71: 0x3000 - not
// taggable), 0x800 one skater feeler ignores (0x004F8698: 0x810), 0x20 three feelers ask for (0x00422F51 / 0x004F8341
// / 0x0052716D: only 0x20 faces).

// A triangle clipped to the camera's near plane (depth along the view >= nearZ): `out` gets 0 (all behind), 3 or 4
// points (a polygon, in order).
int clip_triangle_to_near(V3 camera_pos, const TickCycle& frame, float near_z, std::span<const V3, 3> in, std::span<V3, 4> out);
// The distance from a point to an axis-aligned box (0 inside).
float distance_to_box(V3 point, V3 min, V3 max);

// The editor-only hidden set: ids hidden; Isolate keeps `keep` shown and hides `all` but those.
struct HiddenSet {
private:
    std::vector<uint32_t> ids;

public:
    [[nodiscard]] bool get_has(uint32_t id) const;
    // Added (false: already there).
    bool append_one(uint32_t id);
};
std::vector<uint32_t> isolate_others(const std::vector<uint32_t>& all, std::span<const uint32_t> keep);
// A line drawn as dashes (dash long, gap between, from its start; at most 256 an edge) - collision switched off.
void add_dashed(std::vector<ui::Seg>* lines, const ui::Seg& line, float dash, float gap);
// A colour faded (its alpha at most 0x30) - collision switched off.
uint32_t get_faded(uint32_t argb);

// How a collision face is drawn, from what it is:
//   - its fill: a level trigger's face (0x40) in its kind's colour (level_editor_triggers.h: kill, teleport, gap...),
//     else its Surface's; the selected wall lighter, the wall under the cursor (Shift held) yellow; faded when its
//     collision is switched off, or when it is a face a moved piece took along (shared collision - drawn where it was,
//     classified as it was before it was turned off);
//   - its edges: a moved piece's shared collision where it is now (a companion) solid cyan; a face taken from here
//     dashed cyan; an invisible wall's magenta (the selected one white, the hovered one yellow, thicker), dashed while
//     switched off; other faces none.
struct FaceFacts {
    render::FaceFlag flags{};
    bool collision_only_ = false; // (its sector has nothing drawn: an invisible wall)
    bool off = false;             // (its sector's collision switched off)
    bool taken = false;           // (turned off for a moved piece's shared collision)
    bool companion = false;       // (a moved piece's shared collision)
    bool selected = false, hovered = false;
    uint32_t trigger_colour_ = 0; // (a level trigger's kind colour, ARGB; 0: not one)
};
struct FaceStyle {
    uint32_t fill = 0;
    uint32_t edge = 0; // (0: no edges)
    bool dashed = false;
    float thickness = 1.0F;
};
constexpr uint32_t kCompanionEdge = 0xE040E0FFU, kTakenEdge = 0xC040E0FFU, kHoverWall = 0xC8FFF080U;
FaceStyle style_of(const FaceFacts& facts);

// --- the game side --------------------------------------------------------------------------------------------------

constexpr int kMaxTriangles = 12000;
constexpr float kRadius = 3000.0F;
constexpr float kWallRadius = 6000.0F;

// The editor starting / stopping (Stop shows everything hidden and drops the caches).
void kickoff();
void halt();
// Once a frame (Update): hidden objects kept off.
void tick();

// Show collision: the setting ([editor] collision, read at Start) and switching it (written to the ini).
bool show_collision();
void set_show_collision(bool on);
// The collision near the camera, as triangles over the view (back buffer pixels, painted far to near) and the
// selected wall's edges as lines.
void add_collision(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                   std::vector<ui::Overlay::Triangle>* triangles, std::vector<ui::Seg>* lines);
struct Attributes {
    int triangles = 0, sectors = 0, walls = 0, off = 0; // (off: pieces whose collision is switched off)
    int companions = 0, taken = 0; // (moved pieces' shared collision drawn; faces drawn where it was taken from)
    int triggers = 0;              // (level trigger faces coloured by kind)
    bool capped = false;
};
Attributes last_stats();

// Shift+click with the collision shown: the nearest drawn face under the ray, if it is an invisible wall's (a
// collision-only sector) - its checksum, else 0.
uint32_t pick_wall(V3 origin, V3 unit_dir);
// The wall a Shift+click would take now (drawn highlighted; 0: none) - set each frame while Shift is held.
void set_hover_wall(uint32_t checksum);
uint32_t hover_wall();
void select_wall(uint32_t checksum);
uint32_t selected_wall();
void clear_wall();
// For the Inspector: the wall's name, box, faces and their flags.
std::string describe_wall(uint32_t checksum);
// (level_editor_faces.h) The nearest drawn face under the ray (the collision shown): its sector's checksum (or the
// moved piece's, for a companion's face) and its index in that collision (`companion`: a moved piece's shared
// collision). False: none drawn there.
struct FaceHit {
    uint32_t checksum = 0;
    int face = -1;
    bool companion = false;
    float t = 0.0F;
};
bool pick_face(V3 origin, V3 unit_dir, FaceHit* hit);
// The cache made again (faces' flags were changed - level_editor_faces.h).
void stale_cache();

// Hiding while editing (by a sector's checksum / an object's id).
bool hidden_sector(uint32_t checksum);
bool hidden_object(uint32_t id);
// H: these hidden. Returns how many were hidden.
int conceal(const std::vector<uint32_t>& sectors, std::span<const uint32_t> objects);
// I or /: everything but these hidden (scenery and objects with a model). Returns how many were hidden.
int get_isolate(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects);
// Alt+H: everything shown again.
void show_all();
// These shown again (the Outliner's eye - level_editor_organize.h). Returns how many were hidden.
int get_show(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects);
int hidden_count();
// What is hidden while editing: the sectors' checksums and the objects' ids (the Outliner's markers).
std::vector<uint32_t> hidden_ids();

} // namespace frontend::level_editor::view
