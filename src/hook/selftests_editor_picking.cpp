// Self-tests for the level editor's picking rules (src/game/front/level_editor_picking.*, not original): structure
// (inside the box, wide, flat slabs) on pieces measured in TR / BO / Foundry / z_ma, what a click takes under the
// filter and with Alt, click-to-cycle's order, the ray against triangles and strips (TR's spider web decal: hit only
// where it has triangles), locks, and the layouts' lock records.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_picking.h"
#include "hook/selftest.h"

namespace {

namespace P = Front::LevelEditor::Picking;
using Front::LevelEditor::V3;

V3 Size(float x, float y, float z) { return {x, y, z}; }
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
// (relative: areas)
bool Close(float a, float b) { return std::fabs(a - b) <= 1e-4f * std::max(1.0f, std::fabs(b)); }
// A box of that size with its corner at `at` (far from the camera unless it holds it).
bool Structure(V3 size, V3 at = {10000.0f, 0.0f, 10000.0f}, V3 camera = {0.0f, 500.0f, 0.0f}, float share = 1.0f)
{
    return P::IsStructure(at, at + size, camera, share);
}

void TestEditorPickStructure()
{
    // Real pieces (x, y, z extents).
    Check(Structure(Size(2087, 0, 3105)), "editor pick: TR's floor (2087 x 0 x 3105) is structure");
    Check(Structure(Size(1080, 742, 0)), "editor pick: TR's wall panel (1080 x 742 x 0) is structure (a slab)");
    Check(Structure(Size(2880, 882, 4528)), "editor pick: TR's roof (2880 x 882 x 4528) is structure (wide)");
    Check(Structure(Size(800, 51, 699)), "editor pick: a floor tile (800 x 51 x 699) is structure (a slab)");
    Check(Structure(Size(3412, 160, 2601)), "editor pick: TR's spider web decal's box (3412 x 160 x 2601) is wide");
    Check(!Structure(Size(720, 147, 807)), "editor pick: TR's bowl (720 x 147 x 807) is pickable");
    Check(!Structure(Size(620, 864, 853)), "editor pick: a 620 x 864 x 853 block is pickable");
    Check(!Structure(Size(1477, 1080, 1036)), "editor pick: a BO building (1477 x 1080 x 1036) is pickable");
    Check(!Structure(Size(4000, 60, 40)), "editor pick: a long rail / ledge (4000 x 60 x 40) is pickable");
    Check(!Structure(Size(300, 2, 300)), "editor pick: a small flat sign (300 x 2 x 300) is pickable");
    Check(Structure(Size(400, 400, 40)) && !Structure(Size(399, 399, 0)) && !Structure(Size(400, 400, 41)),
          "editor pick: the slab thresholds (400 wide, a tenth thick)");
    Check(Structure(Size(360, 60, 924)), "editor pick: TR's low platform SkWare_Main_12 (360 x 60 x 924) is structure");
    Check(Structure(Size(350, 80, 350)) && !Structure(Size(349, 60, 924)) && !Structure(Size(360, 81, 924)) &&
              !Structure(Size(924, 60, 349)),
          "editor pick: the low-and-flat thresholds (80 high, 350 across both ways)");
    Check(Structure(Size(1500, 1500, 10)) && Structure(Size(10, 1500, 1500)) && !Structure(Size(1499, 1499, 1000)),
          "editor pick: the wide threshold (1500 in two dimensions)");
    // Inside: the camera within the box (a room), however small.
    Check(P::IsStructure({-100, 0, -100}, {100, 300, 100}, {0, 150, 0}), "editor pick: a box holding the camera");
    Check(!P::IsStructure({-100, 0, -100}, {100, 300, 100}, {0, 301, 0}), "editor pick: just outside it");
    Check(P::IsStructure({-100, 0, -100}, {100, 300, 100}, {100, 300, 100}), "editor pick: on its corner counts");
    Check(!P::IsStructure({1, 0, 0}, {0, 1, 1}, {0.5f, 0.5f, 0.5f}), "editor pick: not a box: not structure");
}
SELFTEST(Early, TestEditorPickStructure);

void TestEditorPickChoose()
{
    using C = P::Choice;
    P::Candidates c;
    Check(P::Choose(c, P::kAll, false) == C::kNone, "editor pick: nothing under the cursor");
    c.passed = true;
    c.passedT = 100.0f;
    Check(P::Choose(c, P::kAll, false) == C::kNone, "editor pick: only structure: nothing (a click deselects)");
    Check(P::Choose(c, P::kAll, true) == C::kPassed, "editor pick: Alt takes the structure piece");
    Check(P::Choose(c, P::kAll & ~P::kScenery, true) == C::kNone, "editor pick: Alt with scenery filtered: nothing");
    c.object = true;
    c.objectT = 300.0f;
    Check(P::Choose(c, P::kAll, false) == C::kObject, "editor pick: an object behind the floor is taken");
    Check(P::Choose(c, P::kAll, true) == C::kPassed, "editor pick: with Alt the nearer floor is taken");
    c.scenery = true;
    c.sceneryT = 300.0f;
    Check(P::Choose(c, P::kAll, false) == C::kScenery, "editor pick: scenery wins an equal distance (as before)");
    c.sceneryT = 301.0f;
    Check(P::Choose(c, P::kAll, false) == C::kObject, "editor pick: the nearer of an object and scenery");
    Check(P::Choose(c, P::kNodes | P::kScenery, false) == C::kNone,
          "editor pick: objects filtered - the nearer object still hides the scenery behind it");
    c.objectT = 302.0f;
    Check(P::Choose(c, P::kNodes | P::kScenery, false) == C::kScenery, "editor pick: objects filtered");
    c.objectT = 300.0f;
    Check(P::Choose(c, P::kNodes | P::kObjects, false) == C::kObject, "editor pick: scenery filtered");
    Check(P::Choose(c, P::kNodes, false) == C::kNone, "editor pick: only nodes: nothing here");
    c.node = 5;
    Check(P::Choose(c, P::kAll, true) == C::kNode, "editor pick: a gizmo first");
    Check(P::Choose(c, P::kObjects | P::kScenery, false) == C::kObject, "editor pick: nodes filtered: the object");
}
SELFTEST(Early, TestEditorPickChoose);

// What's nearest along the ray occludes what's behind it (P::Resolve, P::Choose with `blocked`).
void TestEditorPickOcclusion()
{
    using C = P::Choice;
    // The user's screenshot (thug2_2026-10-21_12-32-03, TR): the cursor over a hanging light's housing; along the ray
    // (measured offline from TR's files with tools/game/pick_census.ts): the light (opaque, its drawn housing above its
    // collision), the glossy floor SkWare_Main_9a (alpha-blended, no collision) and SkWare_Main_9a_water (alpha-blended,
    // collision: solid), then the mirrored door under the floor, g_WareNew_Main_door01_reflect (opaque, no collision).
    const uint32_t kLight = 0x11, kFloor = 0x9A, kWater = 0x9B, kDoorReflect = 0xD0;
    std::vector<P::Met> met = {{kDoorReflect, 2705.0f, false, true, false},
                               {kFloor, 2147.0f, true, false, false},
                               {kWater, 2151.0f, true, true, false}};
    P::Resolved r = P::Resolve(met);
    Check(r.take == 0 && r.blocked && Near(r.blockT, 2151.0f) && r.passed == kFloor && Near(r.passedT, 2147.0f),
          "editor occlusion: the reflection under TR's glossy floor isn't taken - the floor's collision is solid");
    met.push_back({kLight, 545.0f, false, true, false});
    r = P::Resolve(met);
    Check(r.take == kLight && Near(r.takeT, 545.0f) && r.blocked && Near(r.blockT, 545.0f),
          "editor occlusion: the light in front is taken");
    // A see-through decal without collision lets the ray on; an opaque structure piece doesn't.
    r = P::Resolve({{0x1, 300.0f, false, true, false}, {0x2, 100.0f, true, false, false}});
    Check(r.take == 0x1 && r.passed == 0x2 && Near(r.blockT, 300.0f), "editor occlusion: a web in front: the ramp behind");
    r = P::Resolve({{0x1, 300.0f, false, true, false}, {0x3, 100.0f, true, true, false}});
    Check(r.take == 0 && r.blocked && Near(r.blockT, 100.0f) && r.passed == 0x3,
          "editor occlusion: a wall in front: nothing behind it (Alt takes the wall)");
    // Locked: never taken, still solid; a locked see-through piece doesn't stop the ray.
    r = P::Resolve({{0x1, 300.0f, false, true, false}, {0x4, 100.0f, false, true, true}});
    Check(r.take == 0 && r.blocked && Near(r.blockT, 100.0f) && r.passed == 0, "editor occlusion: a locked piece in front");
    r = P::Resolve({{0x1, 300.0f, false, true, false}, {0x5, 100.0f, true, false, true}});
    Check(r.take == 0x1 && r.passed == 0, "editor occlusion: a locked decal in front: the ramp (the decal isn't for Alt)");
    // One piece twice (its meshes, then its collision): the first that occludes ends the search.
    r = P::Resolve({{0x6, 50.0f, true, false, false}, {0x6, 51.0f, true, true, false}, {0x1, 300.0f, false, true, false}});
    Check(r.take == 0 && Near(r.blockT, 51.0f) && r.passed == 0x6 && Near(r.passedT, 50.0f),
          "editor occlusion: a see-through floor's collision is solid where its glossy mesh isn't");
    Check(P::Resolve({}).take == 0 && !P::Resolve({}).blocked, "editor occlusion: nothing met");

    // Choose: objects and scenery against the first occluding surface.
    P::Candidates c;
    c.blocked = true;
    c.blockT = 100.0f;
    c.object = true;
    c.objectT = 300.0f;
    Check(P::Choose(c, P::kAll, false) == C::kNone, "editor occlusion: an object behind a wall isn't taken");
    c.passed = true;
    c.passedT = 100.0f;
    Check(P::Choose(c, P::kAll, true) == C::kPassed, "editor occlusion: Alt takes the wall");
    c.objectT = 60.0f;
    Check(P::Choose(c, P::kAll, false) == C::kObject, "editor occlusion: an object in front of the wall is");
    Check(P::Choose(c, P::kNodes | P::kScenery, false) == C::kNone, "editor occlusion: a filtered object still hides");
    c.objectT = 100.0f;
    Check(P::Choose(c, P::kAll, false) == C::kNone, "editor occlusion: an object level with the wall: the wall");
    c.scenery = true;
    c.sceneryT = 100.0f;
    Check(P::Choose(c, P::kAll, false) == C::kScenery, "editor occlusion: the scenery piece that is the surface");
    Check(P::Choose(c, P::kNodes | P::kObjects, false) == C::kNone,
          "editor occlusion: scenery filtered - its surface still hides the object level with it");
    c.objectT = 99.0f;
    Check(P::Choose(c, P::kNodes | P::kObjects, false) == C::kObject, "editor occlusion: scenery filtered, object in front");
    c.blocked = false;
    c.scenery = false;
    c.passed = false;
    c.objectT = 5000.0f;
    Check(P::Choose(c, P::kAll, false) == C::kObject, "editor occlusion: nothing solid: the object far away");

    // Gizmos: a gizmo further along the view than the surface under the cursor (and the slack) isn't taken.
    Check(P::GizmoInFront(400.0f, true, 400.0f, 0.0f) && P::GizmoInFront(411.0f, true, 400.0f, 12.0f) &&
              !P::GizmoInFront(413.0f, true, 400.0f, 12.0f) && P::GizmoInFront(9000.0f, false, 400.0f, 12.0f),
          "editor occlusion: a gizmo against the surface (with the slack)");
    Check(Near(P::GizmoSlack(1000.0f, 0.001f, 8.0f), P::kGizmoSlack + 8.0f) && Near(P::GizmoSlack(-5.0f, 1.0f, 8.0f), P::kGizmoSlack),
          "editor occlusion: the slack grows with the pick radius's span");
    namespace N = Front::LevelEditor::Nodes;
    std::vector<N::Pickable> p = {{1, 100.0f, 100.0f, 100.0f, 100.0f, false, 900.0f}};
    N::Occluder wall;
    wall.blocked = true;
    wall.depth = 500.0f;
    wall.unitsPerPixel = 0.001f;
    wall.pixels = 8.0f;
    Check(N::PickNearest(p, 100.0f, 100.0f, 8.0f) == 1 && N::PickNearest(p, 100.0f, 100.0f, 8.0f, wall) == -1,
          "editor occlusion: a spawn behind a wall isn't picked");
    wall.depth = 885.0f; // (slack 12 + 0.9 * 8 = 19.2)
    Check(N::PickNearest(p, 100.0f, 100.0f, 8.0f, wall) == 1, "editor occlusion: a spawn on the floor under the cursor is");
    // A rail from depth 100 (screen x 0) to 1000 (x 300): perspective-correct depth where the cursor is nearest.
    std::vector<N::Pickable> rail = {{2, 0.0f, 0.0f, 300.0f, 0.0f, true, 550.0f, 100.0f, 1000.0f}};
    wall.depth = 200.0f;
    Check(N::PickNearest(rail, 10.0f, 2.0f, 8.0f, wall) == 2 && N::PickNearest(rail, 290.0f, 2.0f, 8.0f, wall) == -1,
          "editor occlusion: a rail running behind a wall: its near end picks, its far end doesn't");
}
SELFTEST(Early, TestEditorPickOcclusion);

// Materials that are see-through (P::MaterialSeeThrough: the first pass's flags and blend mode, as TR's files have them).
void TestEditorPickSeeThrough()
{
    Check(!P::MaterialSeeThrough(0x1024, 0), "editor see-through: g_WareNew_Main_door01_reflect (diffuse) is solid");
    Check(P::MaterialSeeThrough(0x64, 5), "editor see-through: TR's glossy floor SkWare_Main_9a (blend, 0x40)");
    Check(P::MaterialSeeThrough(0x65, 1), "editor see-through: TR's water ripples (add, 0x40)");
    Check(!P::MaterialSeeThrough(0x24, 5) && !P::MaterialSeeThrough(0, 6),
          "editor see-through: blend / fixblend without 0x40 can be solid (BO's trash cans)");
    Check(P::MaterialSeeThrough(0, 1) && P::MaterialSeeThrough(0, 2) && P::MaterialSeeThrough(0, 3) &&
              P::MaterialSeeThrough(0, 4) && P::MaterialSeeThrough(0, 7) && P::MaterialSeeThrough(0, 8) &&
              P::MaterialSeeThrough(0, 9) && P::MaterialSeeThrough(0, 10),
          "editor see-through: added / subtracted / modulated / brightened without 0x40");
    Check(P::MaterialSeeThrough(0, 9 | 0x80000000u) && !P::MaterialSeeThrough(0, 0x80000000u) &&
              !P::MaterialSeeThrough(0, 11),
          "editor see-through: the blend mode's fixed alpha (high byte) aside");
}
SELFTEST(Early, TestEditorPickSeeThrough);

// The low-and-flat structure rule with a level top (P::LevelShare), on real pieces' sizes and shares.
void TestEditorPickLevelTop()
{
    // A wedge kicker 360 wide, rising 48 over 100 then flat for 580: the slope (26 degrees) isn't level.
    P::TopArea kicker;
    P::AddTopTriangle(&kicker, {0, 0, 0}, {360, 0, 0}, {360, 48, 100});
    P::AddTopTriangle(&kicker, {0, 0, 0}, {360, 48, 100}, {0, 48, 100});
    P::AddTopTriangle(&kicker, {0, 48, 100}, {360, 48, 100}, {360, 48, 680}); // (the other winding: either counts)
    P::AddTopTriangle(&kicker, {0, 48, 100}, {360, 48, 680}, {0, 48, 680});
    float slope = 360.0f * std::sqrt(100.0f * 100.0f + 48.0f * 48.0f), flat = 360.0f * 580.0f;
    Check(Close(kicker.up, slope + flat) && Close(kicker.level, flat) &&
              Close(P::LevelShare(kicker), flat / (slope + flat)),
          "editor level top: a kicker's slope and flat top");
    // Walls (vertical) don't count; a 4-degree slope is level enough; none at all: level (SkWare_Main_12's sides).
    P::TopArea walls;
    P::AddTopTriangle(&walls, {0, 0, 0}, {360, 0, 0}, {360, 60, 0});
    P::AddTopTriangle(&walls, {0, 0, 0}, {0, 60, 924}, {0, 0, 924});
    Check(walls.up == 0.0f && P::LevelShare(walls) == 1.0f, "editor level top: side walls only: level");
    P::TopArea gentle;
    P::AddTopTriangle(&gentle, {0, 0, 0}, {100, 7, 0}, {0, 0, 100}); // (atan 0.07: 4 degrees)
    P::AddTopTriangle(&gentle, {0, 0, 0}, {100, 10, 0}, {0, 0, 100}); // (5.7 degrees)
    Check(Close(gentle.level, 0.5f * std::sqrt(1.0e8f + 490000.0f)) &&
              Close(gentle.up, gentle.level + 0.5f * std::sqrt(1.0e8f + 1.0e6f)),
          "editor level top: within 5 degrees is level, 5.7 isn't");
    P::TopArea degenerate;
    P::AddTopTriangle(&degenerate, {1, 1, 1}, {1, 1, 1}, {2, 2, 2});
    Check(degenerate.up == 0.0f, "editor level top: a degenerate triangle adds nothing");
    // A strip: the kicker's flat quad (0 1 2 3) as the meshes are drawn.
    struct Vertex {
        float x, y, z;
        uint32_t colour;
    };
    Vertex v[4] = {{0, 48, 100, 0}, {0, 48, 680, 0}, {360, 48, 100, 0}, {360, 48, 680, 0}};
    const uint16_t strip[] = {0, 1, 2, 3, 3};
    P::TopArea quad;
    P::AddTopStrip(&quad, reinterpret_cast<const uint8_t*>(v), sizeof(Vertex), 4, strip, 5);
    Check(Close(quad.up, flat) && Close(quad.level, flat), "editor level top: a strip's quad");
    P::TopArea cut;
    P::AddTopStrip(&cut, reinterpret_cast<const uint8_t*>(v), sizeof(Vertex), 3, strip, 5);
    Check(Close(cut.up, flat * 0.5f), "editor level top: indices past the vertex count skipped");

    // The rule, on pieces measured by tools/game/pick_census.ts (size; level share of the upward-facing surface).
    auto structure = [](V3 size, float share) { return Structure(size, {10000.0f, 0.0f, 10000.0f}, {0.0f, 500.0f, 0.0f}, share); };
    Check(!structure(Size(360, 48, 680), 0.43f), "editor level top: Foundry's 360 x 48 x 680 kicker (43% level) is pickable");
    Check(!structure(Size(353, 48, 915), 0.0f), "editor level top: z_ma's 353 x 48 x 915 bank (0%) is pickable");
    Check(!structure(Size(554, 37, 374), 0.44f), "editor level top: BO's planter 554 x 37 x 374 (44%) is pickable");
    Check(!structure(Size(609, 76, 577), 0.0f), "editor level top: TR's NJ_bFloors_FarEast01 (0%) is pickable");
    Check(!structure(Size(693, 71, 1636), 0.66f), "editor level top: TR's NJ_Floors_FarEast22 (66%) is pickable");
    Check(structure(Size(360, 60, 924), 1.0f), "editor level top: TR's SkWare_Main_12 (sides only) is still structure");
    Check(structure(Size(426, 49, 829), 1.0f), "editor level top: TR's houseporch08 (level) is structure");
    Check(structure(Size(536, 39, 1260), 0.92f), "editor level top: TR's NJ_Floors_FarEast21 (92%) is structure");
    Check(structure(Size(400, 80, 400), P::kLevelTopShare) && !structure(Size(400, 80, 400), P::kLevelTopShare - 0.01f),
          "editor level top: the share's threshold");
    Check(structure(Size(2087, 0, 3105), 0.0f) && structure(Size(800, 51, 699), 0.0f),
          "editor level top: wide and slab structure doesn't depend on the top");
}
SELFTEST(Early, TestEditorPickLevelTop);

void TestEditorPickCycle()
{
    using C = P::Choice;
    std::vector<P::Layered> objects = {{0x100, 50.0f, false}, {0x200, 400.0f, false}};
    std::vector<P::Layered> scenery = {{0xA, 10.0f, true}, {0xB, 120.0f, false}, {0xC, 900.0f, true}};
    std::vector<P::Entry> order = P::CycleOrder(3, objects, scenery, P::kAll);
    Check(order.size() == 6 && order[0].kind == C::kNode && order[0].id == 3 && order[1].id == 0x100 &&
              order[2].id == 0x200 && order[3].kind == C::kScenery && order[3].id == 0xB &&
              order[4].kind == C::kPassed && order[4].id == 0xA && order[5].id == 0xC,
          "editor pick: the cycle's order - gizmo, objects, scenery, then structure (each nearest first)");
    Check(P::IndexOf(order, C::kObject, 0x200) == 2 && P::IndexOf(order, C::kScenery, 0xA) == 4 &&
              P::IndexOf(order, C::kPassed, 0xB) == 3 && P::IndexOf(order, C::kObject, 0xB) == -1 &&
              P::IndexOf(order, C::kNode, 4) == -1,
          "editor pick: an entry found in the cycle (scenery taken by Alt is the same piece)");
    std::vector<P::Entry> some = P::CycleOrder(3, objects, scenery, P::kScenery);
    Check(some.size() == 3 && some[0].id == 0xB && some[2].id == 0xC, "editor pick: the cycle as filtered");
    Check(P::CycleOrder(-1, {}, {}, P::kAll).empty(), "editor pick: nothing under the cursor: no cycle");
    Check(P::SameSpot(100, 100, 104, 96, 4) && !P::SameSpot(100, 100, 105, 100, 4) && !P::SameSpot(100, 100, 100, 95, 4),
          "editor pick: the same spot (4 pixels)");
}
SELFTEST(Early, TestEditorPickCycle);

void TestEditorPickTriangles()
{
    float t = 0.0f;
    V3 a = {0, 0, 0}, b = {100, 0, 0}, c = {0, 0, 100};
    Check(P::RayTriangle({10, 50, 10}, {0, -1, 0}, a, b, c, &t) && Near(t, 50.0f), "editor pick: a triangle hit from above");
    Check(P::RayTriangle({10, -50, 10}, {0, 1, 0}, a, b, c, &t) && Near(t, 50.0f), "editor pick: and from below (either side)");
    Check(!P::RayTriangle({60, 50, 60}, {0, -1, 0}, a, b, c, &t), "editor pick: missing it (past the long edge)");
    Check(!P::RayTriangle({10, 50, 10}, {0, 1, 0}, a, b, c, &t), "editor pick: behind the ray");
    Check(!P::RayTriangle({10, 50, 10}, {1, 0, 0}, a, b, c, &t), "editor pick: parallel");
    // A strip as the scene meshes are drawn: a quad (0 1 2 3), a join (3 3 4 4) - degenerate, skipped - and a second
    // quad far off; with a stride of 16 bytes (x y z and a colour).
    struct Vertex {
        float x, y, z;
        uint32_t colour;
    };
    Vertex v[8] = {{0, 0, 0, 0}, {0, 0, 100, 0}, {100, 0, 0, 0}, {100, 0, 100, 0},
                   {500, 20, 500, 0}, {500, 20, 600, 0}, {600, 20, 500, 0}, {600, 20, 600, 0}};
    const uint16_t strip[] = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    auto* bytes = reinterpret_cast<const uint8_t*>(v);
    Check(P::RayStrip({90, 100, 90}, {0, -1, 0}, bytes, sizeof(Vertex), 8, strip, 10, 1e9f, &t) && Near(t, 100.0f),
          "editor pick: a strip's second triangle");
    Check(P::RayStrip({550, 100, 550}, {0, -1, 0}, bytes, sizeof(Vertex), 8, strip, 10, 1e9f, &t) && Near(t, 80.0f),
          "editor pick: a strip's quad after a join");
    Check(!P::RayStrip({300, 100, 300}, {0, -1, 0}, bytes, sizeof(Vertex), 8, strip, 10, 1e9f, &t),
          "editor pick: between them nothing - the join's degenerate triangles span no area (a web's box isn't hit)");
    Check(!P::RayStrip({550, 100, 550}, {0, -1, 0}, bytes, sizeof(Vertex), 8, strip, 10, 50.0f, &t),
          "editor pick: beyond the limit");
    Check(!P::RayStrip({550, 100, 550}, {0, -1, 0}, bytes, sizeof(Vertex), 6, strip, 10, 1e9f, &t),
          "editor pick: indices past the vertex count skipped");
}
SELFTEST(Early, TestEditorPickTriangles);

void TestEditorPickLocks()
{
    P::ClearLocks();
    Check(!P::Locked(P::kScenery, 0x1234), "editor locks: none");
    Check(P::SetLocked(P::kScenery, 0x1234, true) && !P::SetLocked(P::kScenery, 0x1234, true) &&
              P::Locked(P::kScenery, 0x1234) && !P::Locked(P::kObjects, 0x1234),
          "editor locks: a piece locked once, by kind");
    Check(P::SetLocked(P::kObjects, 0x55, true) && P::Locks().size() == 2, "editor locks: an object locked");
    namespace Layout = Front::LevelEditor::Layout;
    Layout::File file;
    file.scene = 0x4B7E41E2;
    file.locks = P::Locks();
    std::string text = Layout::Write(file);
    Layout::File back;
    int line = 0;
    Check(Layout::Read(text, &back, &line) && back.locks.size() == 2 && back.locks[0].kind == P::kScenery &&
              back.locks[0].id == 0x1234 && back.locks[1].kind == P::kObjects && back.locks[1].id == 0x55,
          "editor locks: lock records round-trip in a layout");
    Check(text.find("lock sector 00001234") != std::string::npos && text.find("lock object 00000055") != std::string::npos,
          "editor locks: the records' text");
    Check(Layout::Read("thug2-decomp layout 2\nscene 4B7E41E2\nlock wall 00000001\nhide 00000002\n", &back, &line) &&
              back.locks.empty() && back.hidden.size() == 1,
          "editor locks: an unknown lock kind is skipped");
    Check(P::SetLocked(P::kScenery, 0x1234, false) && P::Locks().size() == 1 && !P::Locked(P::kScenery, 0x1234),
          "editor locks: unlocked");
    P::ClearLocks();
    Check(P::Locks().empty(), "editor locks: cleared");
}
SELFTEST(Early, TestEditorPickLocks, [] { Front::LevelEditor::Picking::ClearLocks(); });

} // namespace
