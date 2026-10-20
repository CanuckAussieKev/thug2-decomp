#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_ui.h"

// NOT ORIGINAL - the level editor's view options (ours; see level_editor.h): what the editor shows, never what the game
// does. Nothing here is saved in a layout or changes play; everything is put back when the editor closes.
//
//   - Show collision (C, the toolbar's Collision; [editor] collision=0/1 in thug2-decomp.ini): the level's collision
//     near the camera drawn over the view as see-through triangles (ImGui's background draw list, as the gizmos - not
//     the renderer), each face coloured by what it is for the skater (Classify below). Collision with nothing to draw
//     (a sector whose geometry is null - invisible walls, blockers) in a strong magenta, with its edges. Read from each
//     sector's collision (CCollStatic, vtable 0x00645C18: +0x08 flags - 4 off -, +0x0C its CCollObjTriData, whose
//     vertices are where the sector is now: moving collision moves them), cached per sector and made again when its
//     box changes (the editor moved it). The nearest sectors first, up to kMaxTriangles drawn, within kRadius (the
//     collision-only ones within kWallRadius).
//   - Invisible walls: with the collision shown, Shift+click on a magenta face selects that collision-only sector -
//     its name, box, faces and their flags in the Inspector, its faces outlined. Delete switches its collision off
//     (Delete / Home / the Inspector's Restore on again), and it moves and turns like scenery (level_editor_walls.h).
//     Collision switched off (a wall, or a drawn piece's by the Inspector's Collision) stays drawn faded, a wall's edges
//     dashed, so it can be found and switched on again.
//   - Hide (H) / Isolate (I or /) / Show all (Alt+H): the selected scenery and objects hidden from the view and from
//     clicks, or everything else hidden; while editing only. Scenery: the sector's visibility mask (CSector::
//     SetVisibility 0x0049E680 / GetVisibility 0x0049E690 -> the geometry's +0x44 and each mesh's viewport bits +0x1B,
//     which the scene render tests - what the LevelGeometry Visible / Invisible node commands set, 0x005A3B80) - not
//     its active flag (CSector::SetActive also turns its collision off, and is what the editor's Delete and the layouts
//     use). Objects: their model's geometries turned off (the geometry's SetActive, vfunc +0x34 - for an instance its
//     +0x4C; the model's suspend pass 0x00493C60 turns them on and off when its decision changes, so Tick turns hidden
//     ones off again every frame) - their collision and logic untouched.
namespace Front::LevelEditor::View {

// --- the pieces (pure, self-tested: src/hook/selftests_editor_view.cpp) -------------------------------------------

// What a collision face is for the skater, from its face flags (CCollObjTriData face +0: u16) - in this order (the
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
enum class Surface { kInvisibleWall, kTrigger, kNoCollide, kWallride, kVert, kNotSkatable, kSkatable, kPlain, kCount };
Surface Classify(uint32_t faceFlags, bool collisionOnly);
// The colour drawn (ARGB, see-through) and the legend's name.
uint32_t SurfaceColour(Surface surface);
const char* SurfaceName(Surface surface);
// Face flags other passes read (the legend's notes, not coloured): 0x80 the camera's feeler ignores (0x004E4425:
// SetIgnore(0x90) - the camera goes through), 0x1000 / 0x2000 the graffiti feelers ignore (0x0051DF71: 0x3000 - not
// taggable), 0x800 one skater feeler ignores (0x004F8698: 0x810), 0x20 three feelers ask for (0x00422F51 / 0x004F8341
// / 0x0052716D: only 0x20 faces).

// A triangle clipped to the camera's near plane (depth along the view >= nearZ): `out` gets 0 (all behind), 3 or 4
// points (a polygon, in order).
int ClipTriangleToNear(V3 cameraPos, const Frame& frame, float nearZ, const V3 in[3], V3 out[4]);
// The distance from a point to an axis-aligned box (0 inside).
float DistanceToBox(V3 point, V3 min, V3 max);

// The editor-only hidden set: ids hidden; Isolate keeps `keep` shown and hides `all` but those.
struct HiddenSet {
    std::vector<uint32_t> ids;
    bool Has(uint32_t id) const;
    // Added (false: already there).
    bool Add(uint32_t id);
};
std::vector<uint32_t> IsolateOthers(const std::vector<uint32_t>& all, const std::vector<uint32_t>& keep);
// A line drawn as dashes (dash long, gap between, from its start; at most 256 an edge) - collision switched off.
void AddDashed(std::vector<Ui::Line>* lines, const Ui::Line& line, float dash, float gap);
// A colour faded (its alpha at most 0x30) - collision switched off.
uint32_t Faded(uint32_t argb);

// --- the game side --------------------------------------------------------------------------------------------------

constexpr int kMaxTriangles = 12000;
constexpr float kRadius = 3000.0f;
constexpr float kWallRadius = 6000.0f;

// The editor starting / stopping (Stop shows everything hidden and drops the caches).
void Start();
void Stop();
// Once a frame (Update): hidden objects kept off.
void Tick();

// Show collision: the setting ([editor] collision, read at Start) and switching it (written to the ini).
bool ShowCollision();
void SetShowCollision(bool on);
// The collision near the camera, as triangles over the view (back buffer pixels, painted far to near) and the
// selected wall's edges as lines.
void AddCollision(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                  std::vector<Ui::Overlay::Triangle>* triangles, std::vector<Ui::Line>* lines);
struct Stats {
    int triangles = 0, sectors = 0, walls = 0, off = 0; // (off: pieces whose collision is switched off)
    bool capped = false;
};
Stats LastStats();

// Shift+click with the collision shown: the nearest drawn face under the ray, if it is an invisible wall's (a
// collision-only sector) - its checksum, else 0.
uint32_t PickWall(V3 origin, V3 unitDir);
void SelectWall(uint32_t checksum);
uint32_t SelectedWall();
void ClearWall();
// For the Inspector: the wall's name, box, faces and their flags.
std::string DescribeWall(uint32_t checksum);

// Hiding while editing (by a sector's checksum / an object's id).
bool HiddenSector(uint32_t checksum);
bool HiddenObject(uint32_t id);
// H: these hidden. Returns how many were hidden.
int Hide(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects);
// I or /: everything but these hidden (scenery and objects with a model). Returns how many were hidden.
int Isolate(const std::vector<uint32_t>& sectors, const std::vector<uint32_t>& objects);
// Alt+H: everything shown again.
void ShowAll();
int HiddenCount();

} // namespace Front::LevelEditor::View
