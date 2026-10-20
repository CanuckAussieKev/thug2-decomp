#pragma once

#include <cstdint>
#include <vector>

#include "game/front/level_editor.h"

// NOT ORIGINAL - the level editor's picking rules (ours; see level_editor.h): what a plain click in the view may take.
//
// A click used to take whatever the ray met first - nearly always the floor, a ceiling or a wall, or a see-through
// decal (TR's spider webs span the whole warehouse) - so every click selected something. Now:
//   - Structure: the big surfaces you're inside or standing on - a piece whose box holds the camera, or that is large
//     in two dimensions (a floor, a ceiling, a roof, a big wall, the ground; IsStructure) - and see-through pieces
//     (every mesh alpha-blended: webs, shadows, grime, glows) are passed by a plain click and by the hover: the ray
//     goes on to what's behind them or on them, and a click that meets only those deselects as a click on nothing does.
//   - Alt+click (or a middle click) - already "one piece of scenery" - takes the piece under the cursor whatever it is,
//     structure and see-through pieces included (holding Alt shows what it would take). The Outliner and the browser
//     still select anything.
//   - The pick filter (the toolbar's Pick: Nodes / Objects / Scenery; [editor] pick=<bits> in thug2-decomp.ini): what
//     a click in the view may take at all.
//   - Locks (L, the Inspector's Lock): a locked scenery piece or object is passed by every click, Alt+click too; the
//     Inspector lists them (Unlock), layouts keep them (`lock` records).
namespace Front::LevelEditor::Picking {

// The filter's bits.
constexpr uint32_t kNodes = 1, kObjects = 2, kScenery = 4, kAll = 7;

// The thresholds, from the sizes of the pieces of TR (the warehouse), BO, THPS3's Foundry and THAW's z_ma
// (docs/pending/editor-picking.md has the numbers):
//   - large in two dimensions: the second-largest extent at least kWideSpan (TR's floor 3105 x 2087, its roof, BO's
//     streets; a ramp or a rail is long in one dimension only);
//   - flat: a slab at least kSlabSpan wide in two dimensions and no thicker than kSlabThickness of that (a wall
//     panel 1080 x 742 x 0, a floor tile 800 x 51 x 699 - but not TR's bowl, 720 x 147 x 807).
constexpr float kWideSpan = 1500.0f;
constexpr float kSlabSpan = 400.0f;
constexpr float kSlabThickness = 0.1f;
//   - low and flat: no taller than kLowHeight (y is up) and at least kLowSpan across both ways on the ground - a floor
//     section or the low platform ramps stand on (TR's SkWare_Main_12, 360 x 60 x 924: from above it looks like the
//     floor, and the user saw clicks on it as clicking nothing). A manual pad that size is passed too (Alt+click).
constexpr float kLowHeight = 80.0f;
constexpr float kLowSpan = 350.0f;
// The camera counts as inside a box within this margin (units).
constexpr float kInsideMargin = 0.0f;

// Whether a scenery piece (its axis-aligned bounds, where it is now) is structure for a camera at `camera`.
bool IsStructure(V3 min, V3 max, V3 camera);

// What the ray met (the nearest of each kind; t: the distance along the ray) and what a click takes.
struct Candidates {
    int node = -1;          // a node's gizmo under the cursor (screen distance; first)
    bool object = false;    // an object's box
    float objectT = 0.0f;
    bool scenery = false;   // a scenery piece a plain click takes
    float sceneryT = 0.0f;
    bool passed = false;    // the nearest piece a plain click passes (structure, see-through)
    float passedT = 0.0f;
};
enum class Choice { kNone, kNode, kObject, kScenery, kPassed };
// The node first; then the nearest of the object, the scenery and (`everything`: Alt) the passed piece - each only
// when the filter lets its kind be clicked (the passed piece is scenery). Equal distances: scenery before the object
// (as before: the object is taken only when it's nearer).
Choice Choose(const Candidates& candidates, uint32_t filter, bool everything);

// Click-to-cycle: a click on (about) the same spot as the last one, with nothing moved since (the camera, the
// selection), takes the next thing under the cursor after what that click took - the gizmo, then the objects (nearest
// first), then the scenery, then the passed pieces (structure, see-through); wrapping round. The status bar says
// "n of m under the cursor".
struct Entry {
    Choice kind;
    uint32_t id; // a node's index, an object's id, a sector's checksum
    float t;
};
struct Layered {
    uint32_t id;
    float t;
    bool passed; // (scenery: a piece a plain click passes)
};
// Everything under the cursor in the cycle's order, as the filter lets it be clicked (`objects` and `scenery` each
// nearest first).
std::vector<Entry> CycleOrder(int node, const std::vector<Layered>& objects, const std::vector<Layered>& scenery,
                              uint32_t filter);
// Where `kind` / `id` is in the order (-1: not in it).
int IndexOf(const std::vector<Entry>& order, Choice kind, uint32_t id);
// Within `tolerance` pixels of the last click.
bool SameSpot(float x0, float y0, float x1, float y1, float tolerance);

// Ray against a triangle (either side): the distance along `dir` (in units of dir), false if none in front.
bool RayTriangle(V3 origin, V3 dir, V3 a, V3 b, V3 c, float* t);
// Ray against an indexed triangle strip (as the PC scene meshes are drawn: each index with the two before it a
// triangle, repeated indices - the strips' joins - none): `positions` the vertices' x, y, z, `stride` bytes apart,
// `vertexCount` of them (indices past it skipped). The nearest hit below `maxT`.
bool RayStrip(V3 origin, V3 dir, const uint8_t* positions, uint32_t stride, uint32_t vertexCount,
              const uint16_t* indices, uint32_t indexCount, float maxT, float* t);

// Locks: by kind (kObjects: an object's id; kScenery: a sector's checksum).
struct Lock {
    uint32_t kind;
    uint32_t id;
};
bool Locked(uint32_t kind, uint32_t id);
// Locked / unlocked (false: nothing changed).
bool SetLocked(uint32_t kind, uint32_t id, bool locked);
const std::vector<Lock>& Locks();
// A new level (or a layout put back): every lock dropped / these locks in place.
void ClearLocks();
void SetLocks(const std::vector<Lock>& locks);

} // namespace Front::LevelEditor::Picking
