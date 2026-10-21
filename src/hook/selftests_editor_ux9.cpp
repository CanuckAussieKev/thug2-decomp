// Self-tests for the level editor's UX9 tools (src/game/front/level_editor_ground.*, _gap_wizard.*, _skate_view.*, not
// original): the generated ground pieces (shapes, windings, flags, ramps, pits, the strip, the vertex layouts, the
// record), the gap wizard's spots, gates and types, the skateability classes, and the tools' keys.

#include <SDL2/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_gap_wizard.h"
#include "game/front/level_editor_ground.h"
#include "game/front/level_editor_skate_view.h"
#include "game/front/level_editor_triggers.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace G = Front::LevelEditor::Ground;
namespace W = Front::LevelEditor::GapWizard;
namespace S = Front::LevelEditor::SkateView;
using LE::V3;

bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

G::Spec Box(float h)
{
    G::Spec s;
    s.name = "ed_ground01";
    s.x0 = 0, s.z0 = 0, s.x1 = 400, s.z1 = 300;
    s.base = 10;
    s.height = h;
    s.material = 0x12345678;
    return s;
}

V3 FaceNormal(const G::Mesh& m, const G::Face& f)
{
    V3 a = m.vertices[f.a].pos, b = m.vertices[f.b].pos, c = m.vertices[f.c].pos;
    return LE::Normalize(LE::Cross(b - a, c - a));
}
V3 FaceCentre(const G::Mesh& m, const G::Face& f)
{
    return (m.vertices[f.a].pos + m.vertices[f.b].pos + m.vertices[f.c].pos) * (1.0f / 3.0f);
}
int CountFlags(const G::Mesh& m, uint16_t flags)
{
    return static_cast<int>(std::count_if(m.faces.begin(), m.faces.end(), [&](const G::Face& f) { return f.flags == flags; }));
}

void TestEditorGroundShapes()
{
    // A plain platform: a deck and four walls, every face wound out of the box (the collision's one-sided test).
    G::Mesh plain = G::Build(Box(200));
    bool outward = true, stored = true;
    V3 centre = {200, 110, 150};
    for (const G::Face& f : plain.faces) {
        V3 n = FaceNormal(plain, f);
        outward = outward && LE::Dot(n, FaceCentre(plain, f) - centre) > 0.0f;
        stored = stored && Near(plain.vertices[f.a].normal.x, n.x) && Near(plain.vertices[f.a].normal.y, n.y);
    }
    Check(plain.faces.size() == 10 && plain.vertices.size() == 30 && outward && stored,
          "editor ground: a platform's deck and walls, wound outwards, normals stored");
    Check(CountFlags(plain, G::kFlagDeck) == 2 && CountFlags(plain, G::kFlagWall) == 8 && Near(plain.max.y, 210.0f) &&
              Near(plain.min.y, 10.0f - G::kSink),
          "editor ground: the deck skatable, the walls wall-ridable, sunk below the ground");

    // A quarter pipe on +X: 8 curve faces (16 triangles), 2 x 8 cheek triangles; the steeper half vert; out by its radius.
    G::Spec qp = Box(200);
    qp.ramps[G::kPlusX] = G::Ramp::Quarter;
    G::Mesh q = G::Build(qp);
    Check(q.faces.size() == 2 + 6 + 16 + 16 && CountFlags(q, G::kFlagVert) == 8 && Near(q.max.x, 600.0f) &&
              Near(q.max.y, 210.0f),
          "editor ground: a quarter pipe - its curve, cheeks, vert faces, radius out from the edge");
    bool rampFacesOut = true;
    for (const G::Face& f : q.faces) {
        if (f.flags == G::kFlagVert || (f.flags == G::kFlagRamp && FaceCentre(q, f).x > 400.0f)) {
            V3 n = FaceNormal(q, f);
            rampFacesOut = rampFacesOut && n.x > -1e-4f && n.y > -1e-4f;
        }
    }
    Check(rampFacesOut, "editor ground: the ramp's faces face out and up");
    // The profile: the foot at the radius out, the top at the edge; taller than the radius - a vertical extension;
    // lower - a truncated arc whose foot is nearer.
    auto full = G::QuarterProfile(200, 200, 8), tall = G::QuarterProfile(100, 250, 8), low = G::QuarterProfile(200, 50, 8);
    Check(full.size() == 9 && Near(full[0].first, 200) && Near(full[0].second, 0) && full[8].first == 0.0f &&
              full[8].second == 200.0f && tall.size() == 10 && tall[9].second == 250.0f && tall[8].second == 100.0f &&
              Near(low[0].first, 200.0f * std::sin(std::acos(0.75f)), 0.01f) && low[8].second == 50.0f,
          "editor ground: the quarter pipe's profile (full, extended, truncated)");

    // Kickers: twice the height long by default, a set length otherwise.
    G::Spec k = Box(100);
    k.ramps = {G::Ramp::Kicker, G::Ramp::Kicker, G::Ramp::None, G::Ramp::None};
    G::Mesh km = G::Build(k);
    k.length = 50;
    Check(Near(km.min.x, -200.0f) && Near(km.max.x, 600.0f) && Near(G::Build(k).max.x, 450.0f) &&
              G::RampLength(k, G::kPlusX) == 50.0f && G::RampLength(k, G::kMinusZ) == 0.0f,
          "editor ground: kickers' lengths");

    // A pit: its floor below, its walls facing in; opposite ramps kept from meeting.
    G::Spec pit = Box(150);
    pit.lower = true;
    G::Mesh p = G::Build(pit);
    bool inward = true;
    V3 middle = {200, 10 - 75, 150};
    for (const G::Face& f : p.faces)
        inward = inward && LE::Dot(FaceNormal(p, f), middle - FaceCentre(p, f)) > 0.0f;
    Check(p.faces.size() == 10 && inward && Near(p.min.y, -140.0f) && Near(p.max.y, 10.0f),
          "editor ground: a pit - floor and walls facing in");
    pit.ramps[G::kMinusX] = pit.ramps[G::kPlusX] = G::Ramp::Quarter;
    pit.height = 300;
    Check(G::RampLength(pit, G::kMinusX) == 190.0f && G::RampLength(pit, G::kPlusX) == 190.0f &&
              G::Build(pit).faces.size() == 2 + 4 + 2 * 18,
          "editor ground: a pit's opposite ramps share its width (no cheeks inside)");

    // Normalise: corners in order, limits.
    G::Spec n = Box(5000);
    std::swap(n.x0, n.x1);
    n.z1 = n.z0 + 10;
    Check(G::Normalise(&n) && n.x0 == 0 && n.x1 == 400 && n.z1 == G::kMinSide && n.height == G::kMaxHeight,
          "editor ground: the rectangle put in order and kept within the limits");
    n.base = std::nanf("");
    Check(!G::Normalise(&n), "editor ground: not a number refused");
}
SELFTEST(Early, TestEditorGroundShapes);

void TestEditorGroundStripAndVertices()
{
    G::Spec s = Box(200);
    s.ramps[G::kMinusZ] = G::Ramp::Quarter;
    G::Mesh m = G::Build(s);
    std::vector<uint16_t> strip = G::Strip(m.faces, true);
    auto tris = G::StripTriangles(strip);
    // (every face drawn once each way; nothing else)
    std::multiset<std::vector<uint16_t>> want, got;
    auto key = [](uint16_t a, uint16_t b, uint16_t c) {
        // (rotated so the smallest is first: the same triangle with the same winding)
        std::vector<uint16_t> v = {a, b, c};
        std::rotate(v.begin(), std::min_element(v.begin(), v.end()), v.end());
        return v;
    };
    for (const G::Face& f : m.faces) {
        want.insert(key(f.a, f.b, f.c));
        want.insert(key(f.a, f.c, f.b));
    }
    for (const auto& t : tris)
        got.insert(key(t[0], t[1], t[2]));
    Check(want == got && tris.size() == 2 * m.faces.size(), "editor ground: the strip draws every face both ways");
    auto oneWay = G::StripTriangles(G::Strip(m.faces, false));
    bool asWound = oneWay.size() == m.faces.size();
    for (size_t i = 0; asWound && i < oneWay.size(); ++i)
        asWound = key(oneWay[i][0], oneWay[i][1], oneWay[i][2]) == key(m.faces[i].a, m.faces[i].b, m.faces[i].c);
    Check(asWound, "editor ground: a one-sided strip keeps each face's winding (even starts)");

    G::Layout l;
    Check(G::LayoutFor(0x152, 36, &l) && l.normal == 12 && l.diffuse == 24 && l.tex == 28 && l.texCount == 1 &&
              l.size < 0 && l.specular < 0,
          "editor ground: XYZ | NORMAL | DIFFUSE | TEX1");
    Check(G::LayoutFor(0x242, 32, &l) && l.normal < 0 && l.diffuse == 12 && l.tex == 16 && l.texCount == 2,
          "editor ground: XYZ | DIFFUSE | TEX2");
    Check(!G::LayoutFor(0x152, 40, &l) && !G::LayoutFor(2, 12, &l) && !G::LayoutFor(0x10152, 36, &l) &&
              !G::LayoutFor(0x146, 32, &l),
          "editor ground: other strides, built-in shaders, handles and XYZRHW refused");
    G::LayoutFor(0x152, 36, &l);
    std::vector<uint8_t> bytes(m.vertices.size() * 36);
    G::WriteVertices(m, l, 36, 0xFF7F6E5D, bytes.data());
    const G::Vertex& v = m.vertices[5];
    float pos[3], nrm[3], uv[2];
    uint32_t colour;
    std::memcpy(pos, &bytes[5 * 36], 12);
    std::memcpy(nrm, &bytes[5 * 36 + 12], 12);
    std::memcpy(&colour, &bytes[5 * 36 + 24], 4);
    std::memcpy(uv, &bytes[5 * 36 + 28], 8);
    Check(pos[0] == v.pos.x && pos[2] == v.pos.z && nrm[1] == v.normal.y && colour == 0xFF7F6E5D && uv[0] == v.u &&
              uv[1] == v.v,
          "editor ground: the vertices written in the donor's layout");
    // (texture coordinates: world units over the uv scale, by the face's main axis)
    G::Spec t = Box(100);
    t.uv = 100;
    G::Mesh tm = G::Build(t);
    Check(Near(tm.vertices[0].u, tm.vertices[0].pos.x / 100.0f) && Near(tm.vertices[0].v, -tm.vertices[0].pos.z / 100.0f),
          "editor ground: the deck's texture coordinates from x / z");
}
SELFTEST(Early, TestEditorGroundStripAndVertices);

void TestEditorGroundRecords()
{
    G::Spec s = Box(120);
    s.ramps = {G::Ramp::Quarter, G::Ramp::None, G::Ramp::Kicker, G::Ramp::None};
    s.length = 75.5f;
    s.uv = 128;
    s.colour = 0x80706050;
    s.terrain = 7;
    s.lower = true;
    s.hides = {0xAABBCCDD, 0x01020304};
    std::string text = G::Record(s, nullptr);
    G::Spec back;
    Check(text ==
              "ed_ground01 lower rect=0,0,400,300 base=10 height=120 ramps=q,-,k,- length=75.5 material=0x12345678 "
              "uv=128 color=80706050 terrain=7 hides=0xAABBCCDD,0x01020304" &&
              G::ReadRecord(text, &back) && G::SameSpec(back, s),
          "editor ground: the record and back");
    // (a material known by name is written by name - its checksum must be the name's)
    G::Spec named = Box(50);
    named.material = 0x12345678;
    Check(G::Record(named, "not_its_name").find("material=0x12345678") != std::string::npos,
          "editor ground: a name that isn't the material's checksum not written");
    Check(!G::ReadRecord("ed_ground01 raise rect=0,0,1,1 base=0 height=10", &back) &&
              !G::ReadRecord("ed_ground01 raise rect=0,0,100,100 base=0 base=1 height=10 material=0x00000001", &back) &&
              !G::ReadRecord("ed_ground01 raise rect=0,0,100,100 base=0 height=10 ramps=q,-,x,- material=0x00000001",
                             &back) &&
              !G::ReadRecord("ed_ground01 sideways rect=0,0,100,100 base=0 height=10 material=0x00000001", &back) &&
              !G::ReadRecord("ed_ground01 raise rect=0,0,100 base=0 height=10 material=0x00000001", &back) &&
              G::ReadRecord("ed_ground01 raise rect=0,0,100,100 base=0 height=10 material=0x00000001 later=1", &back),
          "editor ground: records refused (no material, a key twice, a bad ramp, kind, rect) - a later key skipped");
    std::vector<G::Spec> specs = {Box(1)};
    Check(G::FreshName({}) == "ed_ground01" && G::FreshName(specs) == "ed_ground02", "editor ground: fresh names");
    // Pit tiles: inside the rectangle (a margin) or reaching out; ground faces under it.
    G::Spec r = Box(100);
    Check(G::TileInside({-20, 0, 0}, {400, 10, 320}, r, 25) && !G::TileInside({-30, 0, 0}, {400, 10, 300}, r, 25),
          "editor ground: a tile inside the rectangle (with the margin) or not");
    V3 up[3] = {{100, 10, 100}, {100, 10, 200}, {200, 10, 100}};
    V3 high[3] = {{100, 60, 100}, {100, 60, 200}, {200, 60, 100}};
    V3 wall[3] = {{100, 0, 100}, {100, 100, 100}, {200, 0, 100}};
    V3 away[3] = {{500, 10, 100}, {500, 10, 200}, {600, 10, 100}};
    Check(G::GroundFaceUnder(up, r, 30) && !G::GroundFaceUnder(high, r, 30) && !G::GroundFaceUnder(wall, r, 30) &&
              !G::GroundFaceUnder(away, r, 30),
          "editor ground: ground faces under the rectangle");
    Check(G::SnapTo(37, 25) == 25 && G::SnapTo(38, 25) == 50 && G::SnapTo(-13, 25) == -25 && G::SnapTo(7, 0) == 7,
          "editor ground: snapping to the grid");
}
SELFTEST(Early, TestEditorGroundRecords);

void TestEditorGapWizard()
{
    Check(W::KindOfFace(1.0f, 0) == W::SpotKind::Area && W::KindOfFace(0.05f, 0) == W::SpotKind::Wall &&
              W::KindOfFace(0.5f, 0) == W::SpotKind::Lip && W::KindOfFace(1.0f, 0x08) == W::SpotKind::Lip &&
              W::KindOfFace(0.0f, 0x08) == W::SpotKind::Lip,
          "editor gap wizard: what a clicked face is");
    Check(Near(W::HeadingOf({1, 0, 0}), 1.5707963f) && Near(W::HeadingOf({0, 5, 1}), 0.0f) && W::HeadingOf({0, 1, 0}) == 0.0f,
          "editor gap wizard: headings (0 along +z, +x a quarter turn)");
    W::Spot area;
    area.point = {0, 0, 0};
    W::Gate g = W::GateFor(area, {300, 50, 0});
    Check(Near(g.pos.y, 150.0f) && Near(g.yaw, 1.5707963f) && g.size.x == 300.0f && g.size.z == 60.0f,
          "editor gap wizard: an area's gate stands on the ground, facing the other end");
    W::Spot rail;
    rail.kind = W::SpotKind::Rail;
    rail.point = {0, 100, 0};
    rail.along = {0, 0, 1};
    W::Gate rg = W::GateFor(rail, {50, 100, -400});
    Check(Near(std::fabs(rg.yaw), 3.1415927f) && rg.size.x == 120.0f && Near(rg.pos.y, 130.0f),
          "editor gap wizard: a rail's gate lies across the rail, along it towards the other end");
    W::Spot wall;
    wall.kind = W::SpotKind::Wall;
    wall.point = {0, 50, 0};
    wall.normal = {-1, 0, 0};
    Check(Near(W::GateFor(wall, {0, 0, 500}).pos.x, -80.0f), "editor gap wizard: a wall's gate stands off the wall");
    W::Spot lip = area;
    lip.kind = W::SpotKind::Lip;
    Check(Near(W::GateFor(lip, {0, 0, 1}).pos.y, 200.0f) && W::GateFor(lip, {0, 0, 1}).size.y == 400.0f,
          "editor gap wizard: a lip's gate rises above it");
    Check(W::Suggest(rail, rail) == W::Type::Grind && W::Suggest(area, wall) == W::Type::Wallride &&
              W::Suggest(area, lip) == W::Type::Air,
          "editor gap wizard: the type suggested");
    // (every flag a type gives is one of THUG2's StartGap flags)
    const auto& known = LE::Triggers::GapFlags();
    bool allKnown = true;
    for (int t = 0; t < W::kTypes; ++t) {
        for (const std::string& f : W::FlagsFor(static_cast<W::Type>(t)))
            allKnown = allKnown && std::any_of(known.begin(), known.end(), [&](const char* k) { return f == k; });
    }
    Check(allKnown && W::FlagsFor(W::Type::Air) == std::vector<std::string>{"CANCEL_GROUND"},
          "editor gap wizard: the types' flags are the game's");
    Check(W::ClampScore(10) == 50 && W::ClampScore(9000) == 5000 && W::ClampScore(250) == 250,
          "editor gap wizard: scores 50 - 5000");
}
SELFTEST(Early, TestEditorGapWizard);

void TestEditorSkateability()
{
    float t = 0.7f;
    Check(S::Classify(0x03, 0.0f, t, 0) == S::Kind::Bank && S::Classify(0x02, 1.0f, t, 0) == S::Kind::NotSkatable &&
              S::Classify(0x0A, 0.0f, t, 0) == S::Kind::NotSkatable && S::Classify(0x08, 0.0f, t, 0) == S::Kind::Vert &&
              S::Classify(0x0C, 0.0f, t, 0) == S::Kind::Vert && S::Classify(0x04, 0.0f, t, 0) == S::Kind::Wallride,
          "editor skateability: the flags in the skater's own order (0x01, 0x02, 0x08, 0x04)");
    Check(S::Classify(0, 1.0f, t, 0) == S::Kind::Floor && S::Classify(0, 0.8f, t, 0) == S::Kind::Bank &&
              S::Classify(0, 0.6f, t, 0) == S::Kind::NotSkatable && S::Classify(0x01, 1.0f, t, 0) == S::Kind::Floor,
          "editor skateability: plain faces by their slope");
    Check(S::Classify(0x10, 1.0f, t, 0) == S::Kind::None && S::Classify(0x50, 1.0f, t, 1) == S::Kind::Kill &&
              S::Classify(0x50, 1.0f, t, 2) == S::Kind::Water,
          "editor skateability: pass-through faces left out, kill / water triggers shown");
    Check(Near(S::Threshold(45.0f, 3.14159265f / 180.0f), 0.70710678f) &&
              Near(S::Threshold(std::nanf(""), 1.0f), 0.70710678f) && S::Threshold(200.0f, 1.0f) >= 0.0f,
          "editor skateability: the slope threshold");
    for (int k = 1; k < S::kKinds; ++k)
        Check(S::Colour(static_cast<S::Kind>(k)) != 0 && *S::Name(static_cast<S::Kind>(k)),
              "editor skateability: every kind has a colour and a name");
}
SELFTEST(Early, TestEditorSkateability);

void TestEditorUx9Keys()
{
    // The tools' default keys don't clash with the built-in actions' (Alt+J, Alt+K, Alt+U).
    namespace A = Front::LevelEditor::Actions;
    const auto& all = A::All();
    bool clean = true;
    for (int code : {SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_U}) {
        A::Chord mine = {code, A::kAlt};
        for (int i = 0; i < A::kBuiltinCount && i < static_cast<int>(all.size()); ++i) {
            if (all[i].kind != A::Kind::Press)
                continue;
            for (const A::Chord& d : all[i].defaults)
                clean = clean && !A::SameBinding(mine, d);
        }
    }
    Check(clean, "editor ux9: Alt+J / Alt+K / Alt+U clash with no built-in key");
}
SELFTEST(Early, TestEditorUx9Keys);

} // namespace
