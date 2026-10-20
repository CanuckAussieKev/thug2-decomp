// Self-tests for the level editor's collision switches and invisible walls (src/game/front/level_editor_walls.*, not
// original): the step from one placement to another as the collision geometry's own methods take it (a turn about a
// centre, then a move), moves and turns kept on sixteenths so a geometry moved about and back is exactly as loaded
// (CCollObjTriData::RotateY / Translate on a small geometry with a BSP split), the Inspector's combined checkbox, the
// layouts' `wall` records, and the collision view's dashes.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_view.h"
#include "game/front/level_editor_walls.h"
#include "game/nx/coll_tri.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace Walls = Front::LevelEditor::Walls;
namespace Layout = Front::LevelEditor::Layout;
namespace View = Front::LevelEditor::View;
using LE::V3;

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}
bool OnSixteenths(V3 v)
{
    auto one = [](float f) { return f * 16.0f == std::floor(f * 16.0f); };
    return one(v.x) && one(v.y) && one(v.z);
}

const V3 kPoints[] = {{0, 0, 0}, {3, 1, -2}, {-50, 7, 12}, {120.5f, -3, 64}};

// A placement's points after a Step, as RotateY then Translate do it.
V3 Stepped(const Walls::Step& step, V3 y) { return LE::QuarterTurn(y - step.centre, step.turns) + step.centre + step.delta; }

void TestEditorWallsSteps()
{
    Check(Walls::ToSixteenth(0.03f) == 0.0f && Walls::ToSixteenth(0.04f) == 0.0625f && Walls::ToSixteenth(-1.97f) == -2.0f &&
              Walls::ToSixteenth(100.5f) == 100.5f,
          "editor walls: values put on sixteenths (the BSP tree's grid)");
    // The step between any two placements does to a point what the placements say.
    const LE::Placement placements[] = {{0, {0, 0, 0}}, {1, {16, 0, -8}}, {2, {-3.5f, 2, 40}}, {3, {100, -6, 0.25f}}};
    bool all = true;
    for (const LE::Placement& from : placements)
        for (const LE::Placement& to : placements) {
            Walls::Step step = Walls::StepBetween(from, to);
            all = all && step.turns >= 0 && step.turns <= 3;
            for (V3 x : kPoints)
                all = all && Near(Stepped(step, LE::Apply(from, x)), LE::Apply(to, x), 1e-4f);
        }
    Check(all, "editor walls: a step (turn about a centre, then a move) takes each placement to each other");
    Walls::Step none = Walls::StepBetween(placements[2], placements[2]);
    Check(none.turns == 0 && none.delta.x == 0 && none.delta.y == 0 && none.delta.z == 0,
          "editor walls: no step between a placement and itself");

    // Moving: the pivot goes where asked (to a sixteenth); the placement stays on sixteenths.
    LE::Placement p = {1, {16, 0, -8}};
    V3 x0 = {5, 2, 7};
    V3 pivot = LE::Apply(p, x0);
    LE::Placement moved = Walls::MovedTo(p, pivot, {33.3f, -7.71f, 1000.02f});
    Check(Near(LE::Apply(moved, x0), {33.3f, -7.71f, 1000.02f}, 1.0f / 32.0f) && OnSixteenths(moved.offset) &&
              moved.quarters == 1 && LE::IsUnitScale(moved.scale),
          "editor walls: moved - the pivot where asked, on sixteenths");
    // Turning about the pivot keeps the pivot (put on sixteenths) where it is.
    LE::Placement turned = Walls::TurnedAbout(moved, LE::Apply(moved, x0), 1);
    Check(turned.quarters == 2 && Near(LE::Apply(turned, x0), LE::Apply(moved, x0), 1.0f / 32.0f) &&
              OnSixteenths(turned.offset),
          "editor walls: a quarter turn about its middle keeps it in place");
    LE::Placement back = Walls::TurnedAbout(turned, LE::Apply(turned, x0), -1);
    Check(back.quarters == 1, "editor walls: turning back (Z after X)");
    Check(Walls::IsHome({0, {0, 0, 0}}) && !Walls::IsHome({1, {0, 0, 0}}) && !Walls::IsHome({0, {0, 0.0625f, 0}}),
          "editor walls: where the level has it");

    Check(Walls::Combined({}) == -1 && Walls::Combined({false, false}) == 1 && Walls::Combined({true}) == 0 &&
              Walls::Combined({false, true}) == 2,
          "editor walls: the Inspector's Collision over the selection (none / on / off / mixed)");
}
SELFTEST(Early, TestEditorWallsSteps);

// A small uncompressed geometry: four vertices, its box, a BSP tree of one split (x at 12) over two leaves.
struct WallGeometry {
    float vertices[12] = {10, 0, 20, 14, 2, 20, 10, 0, 26, 13, 4, 25};
    alignas(8) Nx::CCollBSPNode nodes[3] = {};
    Nx::CCollObjTriData data = {};

    WallGeometry()
    {
        data.m_numVertices = 4;
        data.m_compressed = false;
        data.m_vertices = vertices;
        data.m_min = {10, 0, 20, 1};
        data.m_max = {14, 4, 26, 1};
        nodes[0].m_split = (12 * 16) << 2; // (x at 12, in sixteenths)
        nodes[0].m_link = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&nodes[1]));
        nodes[1].m_split = nodes[2].m_split = 3; // (leaves without faces)
        data.m_bsp = &nodes[0];
    }
};

void TestEditorWallsCollision()
{
    WallGeometry g;
    const WallGeometry loaded;
    // One move and turn: the vertices where the placement says, the box around them, the split turned with them.
    LE::Placement home = {0, {0, 0, 0}};
    LE::Placement p = Walls::TurnedAbout(Walls::MovedTo(home, {12, 2, 23}, {40.2f, 3.1f, -7.3f}), {40.2f, 3.1f, -7.3f}, 1);
    Walls::StepCollision(&g.data, home, p);
    bool vertices = true, box = true;
    for (int v = 0; v < 4; ++v) {
        V3 o = {loaded.vertices[3 * v], loaded.vertices[3 * v + 1], loaded.vertices[3 * v + 2]};
        V3 now = {g.vertices[3 * v], g.vertices[3 * v + 1], g.vertices[3 * v + 2]};
        vertices = vertices && Near(now, LE::Apply(p, o), 1e-4f);
        box = box && now.x >= g.data.m_min.x && now.x <= g.data.m_max.x && now.z >= g.data.m_min.z &&
              now.z <= g.data.m_max.z && now.y >= g.data.m_min.y && now.y <= g.data.m_max.y;
    }
    // (x = 12 as loaded is now on z: QuarterTurn takes (dx, dz) to (dz, -dx) - the split's z is Apply(p, (12, 0, 0)).z)
    float split = g.nodes[0].Split() / 16.0f;
    bool tree = g.nodes[0].Axis() == 2 && Near(split, LE::Apply(p, {12, 0, 0}).z, 1e-4f);
    Check(vertices && box && tree, "editor walls: collision moved and turned - vertices, box and BSP split agree");

    // Dragged about (fractional targets every step, quarter turns between), then back where the level has it: exactly as
    // loaded - every step on sixteenths, so the tree's splits (whole sixteenths, Translate rounding towards zero) never
    // drift from the vertices.
    LE::Placement at = p;
    for (int i = 0; i < 300; ++i) {
        V3 pivot = (V3{g.data.m_min.x, g.data.m_min.y, g.data.m_min.z} + V3{g.data.m_max.x, g.data.m_max.y, g.data.m_max.z}) * 0.5f;
        LE::Placement next = (i % 7 == 3) ? Walls::TurnedAbout(at, pivot, (i & 8) ? -1 : 1)
                                          : Walls::MovedTo(at, pivot, pivot + V3{0.37f * (i % 5) - 0.8f, 0.013f * i, -0.29f});
        Walls::StepCollision(&g.data, at, next);
        at = next;
    }
    bool still = true;
    for (int v = 0; v < 4; ++v) {
        V3 o = {loaded.vertices[3 * v], loaded.vertices[3 * v + 1], loaded.vertices[3 * v + 2]};
        still = still && Near({g.vertices[3 * v], g.vertices[3 * v + 1], g.vertices[3 * v + 2]}, LE::Apply(at, o), 1e-3f);
    }
    float splitNow = g.nodes[0].Split() / 16.0f;
    V3 splitPoint = LE::Apply(at, {12, 0, 0});
    float wanted = g.nodes[0].Axis() == 0 ? splitPoint.x : splitPoint.z;
    Check(still && Near(splitNow, wanted, 1e-4f), "editor walls: after 300 drags and turns the tree still matches the vertices");
    Walls::StepCollision(&g.data, at, home);
    Check(std::memcmp(g.vertices, loaded.vertices, sizeof(g.vertices)) == 0 && g.data.m_min == loaded.data.m_min &&
              g.data.m_max == loaded.data.m_max && g.nodes[0].m_split == loaded.nodes[0].m_split &&
              (g.nodes[0].m_link & 3) == (loaded.nodes[0].m_link & 3),
          "editor walls: put back where the level has it - exactly as loaded (vertices, box, tree)");
}
SELFTEST(Early, TestEditorWallsCollision);

void TestEditorWallsRecords()
{
    Layout::File file;
    file.scene = 0x4B7E41E2;
    file.walls.push_back({0x1234ABCD, {1, {-188.25f, 0.0625f, 13.5f}}, true});
    file.walls.push_back({0x0BADF00D, {0, {0, 0, 0}}, true});
    file.walls.push_back({0x00C0FFEE, {3, {4, 0, -2}}, false});
    std::string text = Layout::Write(file);
    Check(text.find("\nwall 1234ABCD 1 -188.25 0.0625 13.5 1") != std::string::npos &&
              text.find("\nwall 0BADF00D 0 0 0 0 1") != std::string::npos &&
              text.find("\nwall 00C0FFEE 3 4 0 -2 0") != std::string::npos,
          "editor walls: wall records written (checksum, quarters, offset, off)");
    Layout::File back;
    int line = -1;
    Check(Layout::Read(text, &back, &line) && back.walls.size() == 3 && back.walls[0].checksum == 0x1234ABCD &&
              back.walls[0].placement.quarters == 1 && back.walls[0].placement.offset.x == -188.25f &&
              back.walls[0].placement.offset.y == 0.0625f && back.walls[0].off && back.walls[1].off &&
              !back.walls[2].off && back.walls[2].placement.quarters == 3 && LE::IsUnitScale(back.walls[2].placement.scale),
          "editor walls: wall records read back");
    // A layout from before (no wall records) reads with none; a wall record with a bad switch refuses the file.
    const char kOld[] = "thug2-decomp layout 2\nscene 4B7E41E2\nsector BF49DBEE 0 1 2 3 0\n";
    Check(Layout::Read(kOld, &back, &line) && back.walls.empty() && back.sectors.size() == 1,
          "editor walls: a layout without wall records reads as before");
    const char kBad[] = "thug2-decomp layout 2\nscene 4B7E41E2\nwall 1234ABCD 0 0 0 0 2\n";
    Check(!Layout::Read(kBad, &back, &line) && line == 3, "editor walls: a wall record with a bad switch refuses the file");
    const char kShort[] = "thug2-decomp layout 2\nscene 4B7E41E2\nwall 1234ABCD 0 0 0\n";
    Check(!Layout::Read(kShort, &back, &line), "editor walls: a wall record cut short refuses the file");
    // (an older reader skips the unknown kind `wall` - Read's "a later version's record": the walls as the level has them)
}
SELFTEST(Early, TestEditorWallsRecords);

void TestEditorWallsDashes()
{
    std::vector<Front::LevelEditor::Ui::Line> lines;
    View::AddDashed(&lines, {0, 0, 30, 0, 0xFFFFFFFF, 1.0f}, 6.0f, 5.0f);
    Check(lines.size() == 3 && lines[0].ax == 0 && lines[0].bx == 6 && Near(lines[1].ax, 11, 1e-4f) &&
              Near(lines[1].bx, 17, 1e-4f) && Near(lines[2].ax, 22, 1e-4f) && Near(lines[2].bx, 28, 1e-4f) &&
              lines[2].argb == 0xFFFFFFFF && lines[2].ay == 0 && lines[2].by == 0,
          "editor walls: a dashed edge - dashes and gaps along it, colour kept");
    lines.clear();
    View::AddDashed(&lines, {0, 0, 0, 25, 0xFF00FF00, 1.0f}, 6.0f, 5.0f);
    Check(lines.size() == 3 && Near(lines[2].ay, 22, 1e-4f) && Near(lines[2].by, 25, 1e-4f) && lines[2].bx == 0,
          "editor walls: the last dash cut at the edge's end");
    lines.clear();
    View::AddDashed(&lines, {5, 5, 5, 5, 0xFFFFFFFF, 1.0f}, 6.0f, 5.0f);
    View::AddDashed(&lines, {0, 0, 100000, 0, 0xFFFFFFFF, 1.0f}, 6.0f, 5.0f);
    Check(lines.size() <= 256 && !lines.empty(), "editor walls: no dashes for a point; at most 256 for a huge edge");
    Check(View::Faded(0xB0FF20E0) == 0x30FF20E0 && View::Faded(0x20102030) == 0x20102030,
          "editor walls: faded (alpha at most 0x30, colour kept)");
}
SELFTEST(Early, TestEditorWallsDashes);

} // namespace
