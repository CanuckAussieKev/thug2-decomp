// Self-tests for the level editor's view menus and measuring tools (not original): the Unlit view's shader twins
// (game/gfx/view_mode.h), the overlays' grid and setting (level_editor_overlays.h), the jump arcs' lips, physics and
// flights (level_editor_arcs.h), the grind preview's rail walk (level_editor_grind_preview.h), the ruler
// (level_editor_ruler.h) and the Add palette's thumbnail cells and lists (level_editor_thumbs.h).

#include <cmath>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_arcs.h"
#include "game/front/level_editor_grind_preview.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_ruler.h"
#include "game/front/level_editor_thumbs.h"
#include "game/gfx/view_mode.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace A = Front::LevelEditor::Arcs;
namespace G = Front::LevelEditor::GrindPreview;
namespace O = Front::LevelEditor::Overlays;
namespace R = Front::LevelEditor::Ruler;
namespace T = Front::LevelEditor::Thumbs;
using LE::V3;

bool Near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance = 1e-3f)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

void TestEditorUnlitShaders()
{
    namespace VM = Nx::ViewMode;
    // The game's texture x vertex colour shader (0x0067EBF0): v0 whole -> c7; v0.a kept; the def after the version.
    std::string in = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4\tr0.rgb,v0,r0\n";
    std::string want = "ps.1.1\ndef c7, 0.5, 0.5, 0.5, 0.5\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\n"
                       "mul_x4\tr0.rgb,c7,r0\n";
    Check(VM::UnlitShaderSource(in) == want, "editor unlit: the texture x vertex colour shader's twin");
    // The generated shaders' last line, v0 at the end of a line.
    Check(VM::UnlitShaderSource("ps.1.1\ntex t0\nmov r0, t0\nmul_x2 r0.rgb, r0, v0\n") ==
              "ps.1.1\ndef c7, 0.5, 0.5, 0.5, 0.5\ntex t0\nmov r0, t0\nmul_x2 r0.rgb, r0, c7\n",
          "editor unlit: a generated shader's final vertex colour");
    // Only alpha from the vertex colour: unchanged (no twin needed).
    std::string alphaOnly = "ps.1.1\ntex t0\nmul_x2\tr0,v0.a,t0.a\n";
    Check(VM::UnlitShaderSource(alphaOnly) == alphaOnly, "editor unlit: vertex alpha only - the same text");
    // A shader already using c7, or without a version line: no twin.
    Check(VM::UnlitShaderSource("ps.1.1\nmov r0, c7\nmul r0, r0, v0\n").empty(), "editor unlit: c7 taken - none");
    Check(VM::UnlitShaderSource("tex t0\nmul r0, t0, v0\n").empty(), "editor unlit: no version line - none");
    // Comments and other registers (v1, rv0) left alone.
    Check(VM::UnlitShaderSource("ps.1.1\nmul v1.rgb,v1,r0 ; v0 here\nmul r0, rv0, v0\n") ==
              "ps.1.1\ndef c7, 0.5, 0.5, 0.5, 0.5\nmul v1.rgb,v1,r0 ; v0 here\nmul r0, rv0, c7\n",
          "editor unlit: comments, v1 and longer names untouched");
}
SELFTEST(Early, TestEditorUnlitShaders);

// A ramp rising along `heading` (x or z, sign) from y 0 to `height` over `length`, `width` wide, as two triangles
// wound so their normal points up (or down when `flip`), plus a back wall (facing away) and its deck when asked.
void AddRamp(std::vector<A::Face>* faces, float length, float height, float width, bool flip, bool backWall,
             uint16_t flags = 0x01)
{
    V3 a = {0, 0, -width / 2}, b = {0, 0, width / 2}, c = {length, height, width / 2}, d = {length, height, -width / 2};
    auto add = [&](V3 p, V3 q, V3 r, uint16_t f) {
        if (flip)
            faces->push_back({p, r, q, f});
        else
            faces->push_back({p, q, r, f});
    };
    // (b - a) x (c - a) for a, b, c: (0,0,w) x (L,h,w/2... ) - check: normal's y must be positive unflipped
    add(a, b, c, flags);
    add(a, c, d, flags);
    if (backWall) {
        V3 e = {length, 0, -width / 2}, f = {length, 0, width / 2};
        // (outward: +x)
        add(d, c, f, 0x01);
        add(d, f, e, 0x01);
    }
}

V3 FaceNormal(const A::Face& f) { return LE::Normalize(LE::Cross(f.b - f.a, f.c - f.a)); }

void TestEditorJumpLips()
{
    // A kicker rising along +x: 30 degrees (height / length = tan 30), 64 high, 100 wide, with a back wall.
    std::vector<A::Face> kicker;
    float height = 64.0f, length = height / std::tan(30.0f * 3.14159265f / 180.0f);
    AddRamp(&kicker, length, height, 100.0f, false, true);
    // (the test's winding: the slope's normal up, the back wall's outward)
    bool up = FaceNormal(kicker[0]).y > 0.0f;
    if (!up) {
        kicker.clear();
        AddRamp(&kicker, length, height, 100.0f, true, true);
    }
    std::vector<A::Lip> lips = A::FindLips(kicker, 4);
    Check(lips.size() == 1, "editor arcs: a kicker has one lip");
    if (lips.size() == 1) {
        const A::Lip& lip = lips[0];
        Check(Near(lip.heading, {1, 0, 0}), "editor arcs: the kicker's lip faces up its slope");
        Check(Near(lip.tangent, {std::cos(0.5235988f), std::sin(0.5235988f), 0}), "editor arcs: its tangent is the slope's");
        Check(Near(lip.point, {length, height, 0}, 0.01f) && Near(lip.width, 100.0f, 0.01f) && Near(lip.height, height),
              "editor arcs: the lip at the top edge's middle, its width and height");
        Check(!lip.vert, "editor arcs: a 30 degree kicker isn't vert");
    }
    // The same with the opposite winding everywhere: the same lip (the piece's convention found from its slope).
    std::vector<A::Face> reversed;
    AddRamp(&reversed, length, height, 100.0f, up, true);
    std::vector<A::Lip> again = A::FindLips(reversed, 4);
    Check(again.size() == 1 && !lips.empty() && Near(again[0].point, lips[0].point, 0.01f) && !again[0].vert,
          "editor arcs: the winding's convention doesn't change the lip (the back wall stays a back wall)");
    // A quarter pipe: facets steepening to vertical along -z, then a vertical top flagged vert.
    std::vector<A::Face> qp;
    float r = 120.0f;
    int steps = 6;
    for (int i = 0; i < steps; ++i) {
        float t0 = 1.5707963f * i / steps, t1 = 1.5707963f * (i + 1) / steps;
        // (profile: z from 0 to -r, y from 0 to r: the curve's centre at (z -r? ) - z = -r sin t, y = r - r cos t)
        float z0 = -r * std::sin(t0), y0 = r - r * std::cos(t0), z1 = -r * std::sin(t1), y1 = r - r * std::cos(t1);
        V3 a = {-60, y0, z0}, b = {60, y0, z0}, c = {60, y1, z1}, d = {-60, y1, z1};
        qp.push_back({a, b, c, static_cast<uint16_t>(i == steps - 1 ? 0x09 : 0x01)});
        qp.push_back({a, c, d, static_cast<uint16_t>(i == steps - 1 ? 0x09 : 0x01)});
    }
    if (LE::Normalize(LE::Cross(qp[0].b - qp[0].a, qp[0].c - qp[0].a)).y < 0.0f)
        for (A::Face& f : qp)
            std::swap(f.b, f.c);
    std::vector<A::Lip> qpLips = A::FindLips(qp, 4);
    Check(qpLips.size() == 1 && qpLips[0].vert && Near(qpLips[0].heading, {0, 0, -1}, 0.02f) &&
              Near(qpLips[0].point.y, r, 0.01f),
          "editor arcs: a quarter pipe's lip is vert, at its top, facing up its curve");
    // A flat deck and a wall alone: no lips; a ramp lower than the minimum: none.
    std::vector<A::Face> flat = {{{0, 10, 0}, {0, 10, 100}, {100, 10, 100}, 0x01}};
    Check(A::FindLips(flat, 4).empty(), "editor arcs: flat ground has no lip");
    // (steep enough to count as sloped - 4 up over 5 - but only 4 high)
    std::vector<A::Face> low;
    AddRamp(&low, 5.0f, 4.0f, 50.0f, !up, false);
    Check(A::FindLips(low, 4).empty(), "editor arcs: a ramp under the minimum height isn't a lip");
    std::vector<A::Face> step;
    AddRamp(&step, 8.0f, 7.0f, 50.0f, !up, false);
    Check(A::FindLips(step, 4).size() == 1, "editor arcs: one just over it is");
    // Not skatable / not collidable faces are ignored.
    std::vector<A::Face> noCollide;
    AddRamp(&noCollide, length, height, 100.0f, !up, false, 0x10);
    Check(A::FindLips(noCollide, 4).empty(), "editor arcs: not collidable faces are no lip");
    // Two kickers back to back (a funbox's): two lips, facing apart.
    std::vector<A::Face> two;
    AddRamp(&two, length, height, 100.0f, !up, false);
    std::vector<A::Face> other;
    AddRamp(&other, length, height, 100.0f, !up, false);
    for (A::Face& f : other) {
        for (V3* p : {&f.a, &f.b, &f.c})
            *p = {-p->x - 200.0f, p->y, -p->z}; // (turned half round: a half turn keeps the winding)
        two.push_back(f);
    }
    std::vector<A::Lip> both = A::FindLips(two, 4);
    Check(both.size() == 2 && LE::Dot(both[0].heading, both[1].heading) < -0.99f, "editor arcs: back-to-back kickers");
    Check(A::FindLips(two, 1).size() == 1, "editor arcs: at most maxLips");
}
SELFTEST(Early, TestEditorJumpLips);

void TestEditorJumpPhysics()
{
    // skater_physics's pairs at stat 5 / 10, the limits as GetScaledValue applies them.
    Check(Near(A::ScaledStat(757.0f, 900.0f, 5.0f, false, 0.0f), 828.5f), "editor arcs: Skater_Max_Speed at stat 5");
    Check(Near(A::ScaledStat(957.0f, 1100.0f, 10.0f, false, 0.0f), 1100.0f), "editor arcs: max max speed at stat 10");
    Check(Near(A::ScaledStat(532.0f, 675.0f, 15.0f, true, 700.0f), 700.0f), "editor arcs: a rising pair's limit");
    Check(Near(A::ScaledStat(10.0f, 2.0f, 10.0f, true, 5.0f), 5.0f), "editor arcs: a falling pair's limit is a minimum");
    A::Physics p = A::ArcsFallback();
    Check(Near(p.airGravity, -1350.0f) && Near(p.vertGravity, -1350.0f / 1.1f) && Near(p.jump, 432.0f) &&
              Near(p.speeds[0], 603.5f) && Near(p.speeds[1], 828.5f) && Near(p.speeds[2], 1028.5f),
          "editor arcs: the defaults are skater_physics at stat 5");
    // The launch: along the tangent, a falling y clamped to 0, then the jump.
    A::Lip lip;
    lip.tangent = {0.8f, 0.6f, 0.0f};
    Check(Near(A::LaunchVelocity(lip, 100.0f, 50.0f), {80.0f, 110.0f, 0.0f}), "editor arcs: speed along the lip + the ollie");
    lip.tangent = {0.8f, -0.6f, 0.0f};
    Check(Near(A::LaunchVelocity(lip, 100.0f, 50.0f), {80.0f, 50.0f, 0.0f}), "editor arcs: a downward lip's y clamped first");
    // A flight over flat ground (y = 0) from 64 up: lands where the formula says.
    float g = -1350.0f;
    V3 v = {800.0f, 400.0f, 0.0f};
    auto ground = [](V3 from, V3 to, V3* hit, V3* normal) {
        if (from.y >= 0.0f && to.y < 0.0f) {
            float t = from.y / (from.y - to.y);
            *hit = from + (to - from) * t;
            *normal = {0.0f, 1.0f, 0.0f};
            return true;
        }
        return false;
    };
    A::Flight f = A::Fly({0, 64, 0}, v, g, 1.0f / 60.0f, 6.0f, 0.0f, ground);
    float t = (v.y + std::sqrt(v.y * v.y + 2.0f * -g * 64.0f)) / -g;
    Check(f.landed && Near(f.landing.x, v.x * t, 1.0f) && Near(f.landing.y, 0.0f, 0.01f),
          "editor arcs: a flight lands where the projectile formula says");
    Check(Near(f.apex.y, 64.0f + v.y * v.y / (-2.0f * g), 2.0f), "editor arcs: its apex");
    // Nothing below: no landing, flown for the whole time; the skip passes an early hit.
    A::Flight never = A::Fly({0, 64, 0}, v, g, 1.0f / 60.0f, 1.0f, 0.0f, nullptr);
    Check(!never.landed && Near(never.time, 1.0f, 0.02f), "editor arcs: no ground - no landing");
    auto wallAtStart = [](V3 from, V3, V3* hit, V3* normal) {
        *hit = from;
        *normal = {1, 0, 0};
        return true;
    };
    A::Flight skipped = A::Fly({0, 64, 0}, v, g, 0.1f, 1.0f, 0.25f, wallAtStart);
    Check(skipped.landed && skipped.time > 0.25f, "editor arcs: the first moments aren't cast (the lip itself)");
    Check(Near(A::RiseHeight(600.0f, -1200.0f), 150.0f) && A::RiseHeight(-5.0f, -1200.0f) == 0.0f,
          "editor arcs: how high a vert launch rises");
    Check(A::Distance(120.0f) == "120 (10.0 ft)", "editor arcs: distances in units and feet");
}
SELFTEST(Early, TestEditorJumpPhysics);

void TestEditorOverlaysRulerGrind()
{
    // The grid: lines on multiples of the spacing within the square, every fifth major.
    std::vector<O::GridLine> grid = O::GridLines({0, 0, 0}, 5.0f, 250.0f, 100.0f);
    int major = 0;
    for (const O::GridLine& l : grid)
        major += l.major ? 1 : 0;
    Check(grid.size() == 10 && major == 2 && Near(grid[0].a.y, 5.0f), "editor overlays: a grid's lines and majors");
    Check(O::GridLines({0, 0, 0}, 0.0f, 100000.0f, 1.0f).empty(), "editor overlays: too many lines - none");
    Check(O::ParseBits(0, false) == O::kDefaultBits && O::ParseBits(0, true) == 0 &&
              O::ParseBits(0x7FFFFFFF, true) == O::kAllBits,
          "editor overlays: the ini's bits read back");
    // The ruler.
    R::Measure m = R::MeasureOf({0, 10, 0}, {30, 50, 40});
    Check(Near(m.distance, std::sqrt(900.0f + 1600.0f + 1600.0f)) && Near(m.horizontal, 50.0f) && Near(m.height, 40.0f),
          "editor ruler: distance, across and up");
    Check(R::Describe(R::MeasureOf({0, 0, 0}, {0, -24, 0})) == "24 (2.0 ft) - across 0 (0.0 ft), down 24 (2.0 ft)",
          "editor ruler: its text");
    std::vector<std::pair<float, float>> pixels = {{100.0f, 100.0f}, {108.0f, 100.0f}, {300.0f, 300.0f}};
    Check(R::NearestWithin(pixels, 107, 101, 12.0f) == 1 && R::NearestWithin(pixels, 200, 200, 12.0f) == -1,
          "editor ruler: the nearest corner within the snap");
    // The grind preview: a rail 0 -> 1 -> 2 with a branch 1 -> 3, and another rail 4 -> 5.
    G::Graph graph;
    graph.count = 6;
    graph.links = {{0, 1}, {1, 2}, {1, 3}, {4, 5}};
    G::Rail rail = G::RailOf(graph, 2);
    Check((rail.points == std::vector<int>{0, 1, 2, 3}) && rail.links.size() == 3, "editor grind: the rail reached both ways");
    Check((G::Ends(rail) == std::vector<int>{0, 2, 3}) && (G::Branches(rail) == std::vector<int>{1}),
          "editor grind: its ends and branch");
    Check(G::RailOf(graph, 5).points == (std::vector<int>{4, 5}), "editor grind: the other rail alone");
    std::vector<V3> positions = {{0, 0, 0}, {100, 0, 0}, {100, 0, 50}, {100, 30, 0}, {}, {}};
    Check(Near(G::Length(rail, positions), 180.0f), "editor grind: its length");
    std::vector<V3> tips = G::ChevronTips({0, 0, 0}, {120, 0, 0}, 60.0f);
    Check(tips.size() == 2 && Near(tips[0], {30, 0, 0}) && Near(tips[1], {90, 0, 0}), "editor grind: chevrons spaced");
    Check(G::ChevronTips({0, 0, 0}, {10, 0, 0}, 60.0f).size() == 1, "editor grind: a short segment gets one");
}
SELFTEST(Early, TestEditorOverlaysRulerGrind);

void TestEditorPaletteThumbs()
{
    // Cells: found, given, the least recently shown taken when full - never one shown this frame.
    T::CellCache cells(3);
    Check(cells.Take(T::Key(0, 1), 1) == 0 && cells.Take(T::Key(0, 2), 1) == 1 && cells.Take(T::Key(1, 1), 2) == 2,
          "editor thumbs: free cells first");
    Check(cells.Find(T::Key(0, 2)) == 1 && cells.Find(T::Key(1, 2)) == -1, "editor thumbs: keys by tab and id");
    cells.drawn[0] = 1;
    Check(cells.Take(T::Key(0, 1), 3) == 0 && cells.drawn[0] == 1, "editor thumbs: shown again keeps its picture");
    Check(cells.Take(T::Key(0, 9), 3) == 1 && cells.drawn[1] == 0, "editor thumbs: the oldest given up");
    Check(cells.Take(T::Key(0, 10), 3) == 2, "editor thumbs: then the next oldest");
    Check(cells.Take(T::Key(0, 11), 3) == -1, "editor thumbs: none while all are on screen");
    float u0, v0, u1, v1;
    T::CellUv(T::kAtlasPixels / T::kCellPixels + 1, &u0, &v0, &u1, &v1);
    Check(Near(u0, 1.0f / 16.0f) && Near(v0, 1.0f / 16.0f) && Near(u1, 2.0f / 16.0f) && Near(v1, 2.0f / 16.0f),
          "editor thumbs: a cell's uv rectangle");
    // The lists.
    int tab = -1;
    uint32_t id = 0;
    Check(T::EntryName(0, 0xAB) == "s:000000AB" && T::ParseEntry("o:0000ABCD", &tab, &id) && tab == 1 && id == 0xABCD,
          "editor thumbs: entries named and read");
    Check(!T::ParseEntry("x:0000ABCD", &tab, &id) && !T::ParseEntry("s:ABC", &tab, &id) &&
              !T::ParseEntry("s:0000ABCG", &tab, &id),
          "editor thumbs: malformed entries refused");
    std::vector<std::string> recent;
    for (int i = 0; i < 25; ++i)
        T::PushRecent(&recent, T::EntryName(0, i), 20);
    T::PushRecent(&recent, T::EntryName(0, 10), 20);
    Check(recent.size() == 20 && recent[0] == T::EntryName(0, 10) && recent[1] == T::EntryName(0, 24) &&
              recent.back() == T::EntryName(0, 5),
          "editor thumbs: recently used - newest first, once, twenty kept");
    std::vector<std::string> fav;
    T::ToggleIn(&fav, "s:00000001");
    T::ToggleIn(&fav, "o:00000002");
    T::ToggleIn(&fav, "s:00000001");
    Check(fav == std::vector<std::string>{"o:00000002"}, "editor thumbs: favourites in and out");
    Check(T::JoinList({"s:00000001", "o:00000002"}) == "s:00000001,o:00000002", "editor thumbs: joined for the ini");
    Check(T::SplitList("s:00000001,,bad,o:00000002,s:00000001") == (std::vector<std::string>{"s:00000001", "o:00000002"}),
          "editor thumbs: split back - malformed and duplicates dropped");
    Check(T::SplitList("").empty() && T::KeyOf("TR (EDITED)") == "TR__EDITED_" && T::KeyOf("") == "_",
          "editor thumbs: an empty list; a scene's key");
}
SELFTEST(Early, TestEditorPaletteThumbs);

} // namespace
