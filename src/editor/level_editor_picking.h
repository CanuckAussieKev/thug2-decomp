#pragma once

#include <cstdint>
#include <vector>

#include "editor/level_editor.h"
#include <span>

// NOT ORIGINAL - the level editor's picking rules (ours; see level_editor.h): what a plain click in the view may take.
//
// A click used to take whatever the ray met first - nearly always the floor, a ceiling or a wall, or a see-through
// decal (TR's spider webs span the whole warehouse) - so every click selected something. Then passed pieces let the
// ray through to what's behind them - under TR's glossy floor too, to its mirrored "_reflect" pieces (the user's
// screenshot thug2_2026-10-21_12-32-03: g_WareNew_Main_door01_reflect taken through the floor) - so now what's
// nearest along the ray occludes what's behind it (Resolve, below). Now:
//   - Structure: the big surfaces you're inside or standing on - a piece whose box holds the camera, or that is large
//     in two dimensions (a floor, a ceiling, a roof, a big wall, the ground; is_structure) - and see-through pieces
//     (every mesh alpha-blended or additive: webs, shadows, grime, glows) are never taken by a plain click or the hover.
//     The ray goes on through see-through pieces it meets only by their meshes; a solid one - structure, a locked
//     piece, collision met (a glossy floor) - ends it: nothing behind it is taken, and the click deselects as a click
//     on nothing does. Gizmos and trigger volumes behind it aren't taken either; objects are by their boxes.
//   - Alt+click (or a middle click) - already "one piece of scenery" - takes the piece under the cursor whatever it is,
//     structure and see-through pieces included (holding Alt shows what it would take). The Outliner and the browser
//     still select anything.
//   - The pick filter (the toolbar's Pick: Nodes / Objects / Scenery; [editor] pick=<bits> in thug2-decomp.ini): what
//     a click in the view may take at all.
//   - Locks (L, the Inspector's Lock): a locked scenery piece or object is never taken by a click, Alt+click too (a
//     locked piece still hides what's behind it from a plain click - click again or Alt reach past); the Inspector
//     lists them (Unlock), layouts keep them (`lock` records).
namespace frontend::level_editor::picking {

// The filter's bits.
constexpr uint32_t kNodes = 1, kObjects = 2, kScenery = 4, kAll = 7;

// The thresholds, from the sizes of the pieces of TR (the warehouse), BO, THPS3's Foundry and THAW's z_ma:
//   - large in two dimensions: the second-largest extent at least kWideSpan (TR's floor 3105 x 2087, its roof, BO's
//     streets; a ramp or a rail is long in one dimension only);
//   - flat: a slab at least kSlabSpan wide in two dimensions and no thicker than kSlabThickness of that (a wall
//     panel 1080 x 742 x 0, a floor tile 800 x 51 x 699 - but not TR's bowl, 720 x 147 x 807).
constexpr float kWideSpan = 1500.0F;
constexpr float kSlabSpan = 400.0F;
constexpr float kSlabThickness = 0.1F;
//   - low and flat: no taller than kLowHeight (y is up), at least kLowSpan across both ways on the ground, and with a
//     level top - at least kLevelTopShare of its upward-facing surface level (get_level_share) - a floor section or the
//     edge of a sunken floor (TR's SkWare_Main_12, 360 x 60 x 924, only side walls: from above it looks like the floor,
//     and the user saw clicks on it as clicking nothing). Kickers, banks and launch ramps that low and wide have sloped
//     tops and stay pickable: Foundry's 360 x 48 x 680 kickers (43% level), z_ma's 353 x 48 x 915 bank (0%), BO's
//     554 x 37 x 374 planter (44%), TR's NJ_bFloors_FarEast01 609 x 76 x 577 (0%) - measured by
//     tools/game/pick_census.ts from the levels' own files.
constexpr float kLowHeight = 80.0F;
constexpr float kLowSpan = 350.0F;
constexpr float kLevelTopShare = 0.9F;
// The camera counts as inside a box within this margin (units).
constexpr float kInsideMargin = 0.0F;

// Whether a scenery piece (its axis-aligned bounds, where it is now) is structure for a camera at `camera`.
// `levelShare`: how much of its upward-facing surface is level (get_level_share; 1 when unknown or it has none).
bool is_structure(V3 min, V3 max, V3 camera, float level_share = 1.0F);

// A piece's upward-facing surface from its triangles (either winding - strips alternate): faces turned up more than
// kUpNormalY (about 75 degrees from level) count, those within about 5 degrees of level (kLevelNormalY) are level.
constexpr float kUpNormalY = 0.25F;
constexpr float kLevelNormalY = 0.996F;
struct TopArea {
    float up = 0.0F;
    float level = 0.0F;
};
void add_top_triangle(TopArea* area, V3 a, V3 b, V3 corner_c);
// The triangles of an indexed strip (as ray_strip reads them) added.
void add_top_strip(TopArea* area, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
                   const uint16_t* indices, uint32_t index_count);
// The level part of the upward-facing surface (1 when there's none: a piece of side walls only).
float get_level_share(const TopArea& area);

// A material pass's flags and its blend mode.
struct PassBlend {
    uint32_t pass_flags_{};
    uint32_t blend_mode_{};
};

// See-through materials: the first pass drawn transparent (the scene's sort flag 0x40 in the material's +0x20 - the
// pass's file flags) or one that adds, subtracts, modulates or brightens (its +0x80 blend mode, low 24 bits: 1 add,
// 2 fixadd, 3 / 4 subtract, 7 / 8 modulate, 9 / 10 brighten) - light put on or taken off what's behind, never a solid
// surface. 0 diffuse, 5 blend and 6 fixblend can be solid (BO's trash cans are blend without 0x40). In the shipped and
// ported levels every additive piece also has 0x40 (pick_census.ts); the modes are a safeguard.
bool material_see_through(PassBlend pass_blend);

// --- occlusion: what's nearest along the ray blocks what's behind it ---
//
// Every piece the ray meets is an entry (its nearest distance). A piece occludes when the ray met its collision (a
// surface the skater can stand on - TR's glossy warehouse floor is alpha-blended over mirrored "_reflect" pieces, and
// it's solid) or an opaque mesh of it (not see-through). Webs, glows, shadows, grime and other see-through pieces
// without collision under the ray don't: the ray goes on. A plain click takes the first entry that is pickable (not
// passed, not locked) if nothing occluding is in front of it; a structure piece, a locked piece or an occluding
// see-through piece first ends the search - nothing behind it is taken (cycle and Alt+click still reach past).
struct Met {
    uint32_t id;
    float t;
    bool passed;   // structure / see-through (Alt takes it)
    bool occludes; // collision met, or an opaque mesh
    bool locked;   // passed by every click, Alt too - but it still occludes a plain click
};
struct Resolved {
    uint32_t take = 0; // what a plain click may take (before the objects and gizmos: Choose)
    float take_t_ = 0.0F;
    uint32_t passed = 0; // the nearest passed piece not locked (Alt)
    float passed_t_ = 0.0F;
    bool blocked = false; // the first occluding surface along the ray (anything: pickable, passed or locked)
    float block_t_ = 0.0F;
};
// `met` in any order, a piece any number of times (each entry's own distance and kind; one piece is one kind).
Resolved settle_up(const std::vector<Met>& met);

// Gizmos (nodes' icons, rails, trigger volumes) are drawn over everything, so a gizmo behind a wall is still under the
// cursor: a plain click takes it only when it's no further along the view than the first occluding surface under the
// cursor, with `slack` to spare (a spawn on the floor, a rail on a ledge's edge: gizmo_slack).
bool gizmo_in_front(float gizmo_depth, bool blocked, float block_depth, float slack);
// The slack at `depth` along the view: kGizmoSlack units plus what `pixels` pixels span there (`unitsPerPixel` at
// depth 1).
constexpr float kGizmoSlack = 12.0F;
float gizmo_slack(float depth, float units_per_pixel, float pixels);

// What the ray met (the nearest of each kind; t: the distance along the ray) and what a click takes.
struct Candidates {
    int node = -1;       // a node's gizmo under the cursor (screen distance; first)
    bool object = false; // an object's box
    float object_t_ = 0.0F;
    bool scenery = false; // a scenery piece a plain click takes
    float scenery_t_ = 0.0F;
    bool passed = false; // the nearest piece a plain click passes (structure, see-through)
    float passed_t_ = 0.0F;
    bool blocked = false; // the first occluding scenery surface (Resolved::block_t_)
    float block_t_ = 0.0F;
};
enum class Choice : uint8_t { kNone, kNode, kObject, kScenery, kPassed };
// The node first (the caller leaves out gizmos behind the first occluding surface: gizmo_in_front); then, for a plain
// click, the nearest of the object, the scenery and the occluding surface - an object or a scenery piece taken only
// when nothing occluding is nearer and the filter lets its kind be clicked: a kind filtered out still occludes, and so
// does a passed / locked surface (nothing behind it). With `everything` (Alt) the nearest of the object, the scenery
// and the passed piece, occlusion aside. Equal distances: scenery before the object (as before: the object is taken
// only when it's nearer), the scenery piece itself before the surface (it is that surface).
Choice get_choose(const Candidates& candidates, uint32_t filter, bool everything);

// Click-to-cycle: a click on (about) the same spot as the last one, with nothing moved since (the camera, the
// selection), takes the next thing under the cursor after what that click took - the gizmo, then the objects (nearest
// first), then the scenery, then the passed pieces (structure, see-through); wrapping round. The status bar says
// "n of m under the cursor".
struct TableRow {
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
std::vector<TableRow> cycle_order(int node, const std::vector<Layered>& objects, std::span<const Layered> scenery,
                                  uint32_t filter);
// Where `kind` / `id` is in the order (-1: not in it).
int get_index_of(const std::vector<TableRow>& order, Choice kind, uint32_t id);
// Within `tolerance` pixels of the last click.
bool same_spot(float x0, float y0, float x1, float y1, float tolerance);

// Ray against a triangle (either side): the distance along `dir` (in units of dir), false if none in front.
bool ray_triangle(Ray ray, V3 a, V3 b, V3 corner_c, float* hit_distance);
// A strip's index count and the furthest hit wanted.
struct StripLimit {
    uint32_t index_count_{};
    float max_t_{};
};

// Ray against an indexed triangle strip (as the PC scene meshes are drawn: each index with the two before it a
// triangle, repeated indices - the strips' joins - none): `positions` the vertices' x, y, z, `stride` bytes apart,
// `vertexCount` of them (indices past it skipped). The nearest hit below `max_t`.
bool ray_strip(V3 origin, V3 dir, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
               const uint16_t* indices, StripLimit strip_limit, float* hit_distance);

// Locks: by kind (kObjects: an object's id; kScenery: a sector's checksum).
struct Lock {
    uint32_t kind;
    uint32_t id;
};
bool latched(uint32_t kind, uint32_t id);
// Locked / unlocked (false: nothing changed).
bool set_locked(uint32_t kind, uint32_t id, bool locked);
const std::vector<Lock>& get_locks();
// A new level (or a layout put back): every lock dropped / these locks in place.
void clear_locks();
void set_locks(const std::vector<Lock>& locks);

} // namespace frontend::level_editor::picking
