// Self-tests for the level editor's picking rules (src/game/front/level_editor_picking.*, not original): structure
// (inside the box, wide, flat slabs) on pieces measured in TR / BO / Foundry / z_ma, what a click takes under the
// filter and with Alt, click-to-cycle's order, the ray against triangles and strips (TR's spider web decal: hit only
// where it has triangles), locks, and the layouts' lock records.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_picking.h"
#include "hook/selftest.h"

namespace {

namespace P = Front::LevelEditor::Picking;
using Front::LevelEditor::V3;

V3 Size(float x, float y, float z) { return {x, y, z}; }
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
// A box of that size with its corner at `at` (far from the camera unless it holds it).
bool Structure(V3 size, V3 at = {10000.0f, 0.0f, 10000.0f}, V3 camera = {0.0f, 500.0f, 0.0f})
{
    return P::IsStructure(at, at + size, camera);
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
    Check(Structure(Size(400, 40, 400)) && !Structure(Size(399, 0, 399)) && !Structure(Size(400, 41, 400)),
          "editor pick: the slab thresholds (400 wide, a tenth thick)");
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
    Check(P::Choose(c, P::kNodes | P::kScenery, false) == C::kScenery, "editor pick: objects filtered");
    Check(P::Choose(c, P::kNodes | P::kObjects, false) == C::kObject, "editor pick: scenery filtered");
    Check(P::Choose(c, P::kNodes, false) == C::kNone, "editor pick: only nodes: nothing here");
    c.node = 5;
    Check(P::Choose(c, P::kAll, true) == C::kNode, "editor pick: a gizmo first");
    Check(P::Choose(c, P::kObjects | P::kScenery, false) == C::kObject, "editor pick: nodes filtered: the object");
}
SELFTEST(Early, TestEditorPickChoose);

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
