// Self-tests for the level editor's scaling of scenery and objects (src/game/front/level_editor_scale.*, not original):
// placements with a per-axis scale (scaling about a point, stretched along an axis, composed with turns and moves, the
// engine's state and boxes), the copy's frame (a stretched piece rebuilt from a copy taken after turns), normals through
// the inverse transpose, the limits, the vertices written again from their copy (no drift), the layout records' scale=
// (uniform and per axis, on objects too), a collision geometry put back and scaled per axis / turned / moved with the
// game's own CCollObjTriData code (vertices, box and BSP tree agreeing), and an object's scale (its model and its
// collision component's triangle object or box, from a copy of its own).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_scale.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "game/obj/collision_component.h"
#include "game/obj/object.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

namespace LE = Front::LevelEditor;
namespace Scale = Front::LevelEditor::Scale;
namespace Layout = Front::LevelEditor::Layout;
using LE::V3;

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}
bool Same(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

const V3 kPoints[] = {{0, 0, 0}, {3, 1, -2}, {-50, 7, 12}, {120.5f, -3, 64}};
const V3 kStretches[] = {{1.5f, 1.5f, 1.5f}, {2.0f, 1.0f, 1.0f}, {1.0f, 0.5f, 1.0f}, {1.0f, 1.0f, 3.0f}, {0.5f, 2.0f, 1.25f}};

// --- placements -----------------------------------------------------------------------------------------------------

void TestEditorScalePlacement()
{
    // Scaling about a point: every point's offset from it times the factor, per axis (after a turn and a move).
    bool about = true;
    for (int q = 0; q < 4; ++q) {
        for (V3 f : kStretches) {
            LE::Placement p = {q, {10, 0, 5}, {1.0f, 2.0f, 0.5f}};
            LE::Placement before = p;
            V3 pivot = {4, -2, 9};
            Scale::ScaleAbout(&p, pivot, f);
            about &= p.quarters == q;
            for (V3 x : kPoints)
                about &= Near(LE::Apply(p, x), LE::PerAxis(LE::Apply(before, x) - pivot, f) + pivot, 1e-3f);
        }
    }
    Check(about, "editor scale: scaled about a point along the world's axes (stretches too, after any turn)");

    // Then turned and moved: still one placement for the lot; back by the inverse factor about the same point.
    LE::Placement p = {1, {10, 0, 5}};
    LE::Placement before = p;
    V3 pivot = {4, -2, 9};
    Scale::ScaleAbout(&p, pivot, V3{1.5f, 1.0f, 0.5f});
    LE::Placement q = p;
    LE::TurnAbout(&q, {-6, 0, 2}, 3);
    LE::MoveBy(&q, {1, 2, 3});
    bool combined = true;
    for (V3 x : kPoints) {
        V3 expect = LE::QuarterTurn(LE::Apply(p, x) - V3{-6, 0, 2}, 3) + V3{-6, 0, 2} + V3{1, 2, 3};
        combined &= Near(LE::Apply(q, x), expect, 1e-3f);
    }
    Scale::ScaleAbout(&p, pivot, V3{1.0f / 1.5f, 1.0f, 2.0f});
    bool undone = Near(p.scale, {1.0f, 1.0f, 1.0f}, 1e-6f);
    for (V3 x : kPoints)
        undone &= Near(LE::Apply(p, x), LE::Apply(before, x), 1e-3f);
    Check(combined && undone, "editor scale: with turns and moves; the inverse factor undoes it");
    // (a stretch along world x after a quarter turn is the piece's own z)
    LE::Placement turned = {1, {0, 0, 0}};
    Scale::ScaleAbout(&turned, {0, 0, 0}, V3{2.0f, 1.0f, 1.0f});
    Check(Same(turned.scale, {1.0f, 1.0f, 2.0f}), "editor scale: a world-x stretch of a quarter-turned piece is its z");

    // Composed (a copy of a scaled piece, moved and scaled since): any turns, stretches.
    bool composed = true;
    for (int qi = 0; qi < 4; ++qi) {
        for (int qo = 0; qo < 4; ++qo) {
            LE::Placement inner = {qi, {10, 0, 5}, {2.0f, 1.0f, 0.5f}}, outer = {qo, {-4, 2, 7}, {0.75f, 3.0f, 1.25f}};
            LE::Placement both = LE::Compose(outer, inner);
            for (V3 x : kPoints)
                composed &= Near(LE::Apply(both, x), LE::Apply(outer, LE::Apply(inner, x)), 1e-3f);
        }
    }
    LE::Placement u1 = {1, {10, 0, 5}, Scale::Uniform(2.0f)}, u2 = {3, {-4, 2, 7}, Scale::Uniform(0.75f)};
    composed &= Near(LE::Compose(u2, u1).scale, Scale::Uniform(1.5f), 1e-6f);
    Check(composed, "editor scale: composed placements (stretched, turned either way) as one");

    // The engine's state: the vertices as loaded at Q0 v + p0 are now where the placement puts them.
    LE::Placement e = {2, {30, 4, -8}, {1.25f, 0.5f, 2.0f}};
    int quarters;
    V3 position;
    LE::EngineState(1, {100, 0, 50}, e, &quarters, &position);
    bool engine = quarters == 3;
    for (V3 v : kPoints) {
        V3 first = LE::QuarterTurn(v, 1) + V3{100, 0, 50};
        engine &= Near(LE::Apply(e, first) - position, LE::QuarterTurn(LE::PerAxis(first - V3{100, 0, 50}, e.scale), 2), 1e-3f);
    }
    Check(engine, "editor scale: the engine's quarters and position for a stretched placement");

    // A placed box: its corners where the placement puts the loaded box's corners; its size per axis.
    V3 min = {0, 0, 0}, max = {10, 4, 6};
    LE::OrientedBox box = LE::PlacedBox(min, max, e);
    V3 corners[8];
    LE::BoxCorners(box.origin, box.axes, box.min, box.max, corners);
    bool placed = true;
    for (int i = 0; i < 8; ++i) {
        V3 loaded = {i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z};
        placed &= Near(corners[i], LE::Apply(e, loaded), 1e-3f);
    }
    placed &= Near(LE::BoxSize(box), {12.5f, 2.0f, 12.0f}, 1e-4f);
    float t = 0.0f;
    // (placed at x 17.5..30, y 4..6, z -20..-8: half a turn, stretched)
    bool hit = LE::RayBox({1000, 5, -10}, {-1, 0, 0}, box.origin, box.axes, box.min, box.max, &t) && Near(t, 970.0f, 1e-2f);
    bool miss = !LE::RayBox({1000, 7, -10}, {-1, 0, 0}, box.origin, box.axes, box.min, box.max, &t);
    Check(placed && hit && miss, "editor scale: a stretched sector's box (corners placed, its size, rays meet it)");
}
SELFTEST(Early, TestEditorScalePlacement);

// --- the copy's frame and normals -----------------------------------------------------------------------------------

void TestEditorScaleFrame()
{
    // A copy taken after the piece was turned / moved (scale 1), rebuilt for any later placement: where the placement
    // puts every point.
    const V3 p0 = {100, 0, 50};
    bool frame = true;
    for (int q0 = 0; q0 < 4; ++q0) {
        for (int qc = 0; qc < 4; ++qc) {
            for (int q = 0; q < 4; ++q) {
                for (V3 s : kStretches) {
                    LE::Placement pc = {qc, {7, -2, 3}}, p = {q, {-30, 5, 12}, s};
                    int cq, nq;
                    V3 cpos, npos;
                    LE::EngineState(q0, p0, pc, &cq, &cpos);
                    LE::EngineState(q0, p0, p, &nq, &npos);
                    Scale::RebuildFrame f = Scale::FrameFor(cq, cpos, q0, nq, npos, s);
                    for (V3 x : kPoints)
                        frame &= Near(Scale::Rebuilt(LE::Apply(pc, x), f.from, f.to, f.turns, f.copyScale),
                                      LE::Apply(p, x), 2e-3f);
                    frame &= f.normalTurns == q;
                }
            }
        }
    }
    Check(frame, "editor scale: a copy taken after turns rebuilt for a stretched, turned placement (its frame)");

    // Normals: through the inverse transpose - a face's normal stays perpendicular to its stretched edges, unit long,
    // facing the same way; unscaled only turned (not normalised again); zero stays zero.
    bool normals = true;
    const V3 tri[3] = {{0, 0, 0}, {4, 1, 0}, {1, 3, 2}};
    V3 n = LE::Normalize(LE::Cross(tri[1] - tri[0], tri[2] - tri[0]));
    for (int q = 0; q < 4; ++q) {
        for (V3 s : kStretches) {
            V3 a = LE::QuarterTurn(LE::PerAxis(tri[1] - tri[0], s), q), b = LE::QuarterTurn(LE::PerAxis(tri[2] - tri[0], s), q);
            V3 expect = LE::Normalize(LE::Cross(a, b));
            V3 got = Scale::ScaledNormal(n, s, q);
            normals &= Near(got, expect, 1e-4f) && Near(LE::Dot(got, got), 1.0f, 1e-4f);
        }
    }
    V3 odd = {0.3f, 0.4f, 0.0f}; // (not unit: unscaled it stays as it is, turned)
    normals &= Same(Scale::ScaledNormal(odd, Scale::Uniform(1.0f), 1), LE::QuarterTurn(odd, 1)) &&
               Same(Scale::ScaledNormal({0, 0, 0}, {2, 1, 1}, 0), {0, 0, 0});
    Check(normals, "editor scale: normals through the inverse transpose, normalised and turned");

    // Normals written into a vertex buffer at their offset, the rest kept; the vertex formats that have them.
    const float original[6] = {0, 1, 0, 0.6f, 0, 0.8f};
    uint8_t buffer[64];
    for (int i = 0; i < 64; ++i)
        buffer[i] = static_cast<uint8_t>(0x40 + i);
    uint8_t untouched[64];
    std::memcpy(untouched, buffer, 64);
    Scale::RewriteNormals(buffer, 32, 2, 16, original, {2, 1, 1}, 1);
    bool written = true, kept = true;
    for (int v = 0; v < 2; ++v) {
        const float* got = reinterpret_cast<const float*>(buffer + 32 * v + 16);
        written &= Near({got[0], got[1], got[2]},
                        Scale::ScaledNormal({original[3 * v], original[3 * v + 1], original[3 * v + 2]}, {2, 1, 1}, 1), 0);
        for (int k = 0; k < 32; ++k)
            kept &= (k >= 16 && k < 28) || buffer[32 * v + k] == untouched[32 * v + k];
    }
    Check(written && kept, "editor scale: normals written at their offset, the rest of each vertex kept");
    Check(Scale::NormalOffset(0x152, 32) == 12 && Scale::NormalOffset(0x52, 28) == 12 && Scale::NormalOffset(0x142, 24) == -1 &&
              Scale::NormalOffset(0x152, 20) == -1 && Scale::NormalOffset(0x154, 36) == -1 &&
              Scale::NormalOffset(0x00781B52, 32) == -1 && Scale::NormalOffset(0, 32) == -1,
          "editor scale: which vertex formats have a normal (an FVF with XYZ and NORMAL; not a shader handle)");
}
SELFTEST(Early, TestEditorScaleFrame);

// --- limits ---------------------------------------------------------------------------------------------------------

void TestEditorScaleLimits()
{
    auto u = [](float f) { return Scale::Uniform(f); };
    auto allowed = [&u](float f, std::vector<float> scales, std::vector<float> limits) {
        std::vector<V3> s, l;
        for (float v : scales)
            s.push_back(u(v));
        for (float v : limits)
            l.push_back(u(v));
        return Scale::AllowedFactor(u(f), s, l, true).x;
    };
    Check(Near(allowed(2.0f, {1.0f}, {4.0f}), 2.0f, 0) && Near(allowed(8.0f, {1.0f}, {4.0f}), 4.0f, 1e-6f) &&
              Near(allowed(2.0f, {3.0f}, {4.0f}), 4.0f / 3.0f, 1e-6f) && Near(allowed(0.1f, {1.0f}, {4.0f}), 0.25f, 1e-6f),
          "editor scale: a factor kept within 0.25x..4x");
    Check(allowed(2.0f, {4.0f}, {4.0f}) == 1.0f && allowed(0.5f, {0.25f}, {4.0f}) == 1.0f &&
              allowed(1.1f, {4.0f}, {4.0f}) == 1.0f,
          "editor scale: at a limit, no further that way");
    Check(Near(allowed(0.5f, {4.0f}, {4.0f}), 0.5f, 0), "editor scale: at the top, down still works");
    Check(Near(allowed(2.0f, {1.0f}, {1.5f}), 1.5f, 1e-6f) && Near(allowed(2.0f, {1.0f, 1.5f}, {4.0f, 4.0f}), 2.0f, 0) &&
              Near(allowed(4.0f, {1.0f, 2.0f}, {4.0f, 4.0f}), 2.0f, 1e-6f),
          "editor scale: a group limited by its most limited piece (compressed collision too)");
    Check(allowed(std::nanf(""), {1.0f}, {4.0f}) == 1.0f && allowed(-2.0f, {1.0f}, {4.0f}) == 1.0f &&
              allowed(0.0f, {1.0f}, {4.0f}) == 1.0f,
          "editor scale: a factor that isn't a positive number does nothing");
    // Per axis: each by itself; uniform: what every axis allows (the most limited one, on all three).
    V3 a = Scale::AllowedFactor({2.0f, 1.0f, 0.5f}, {{1.0f, 1.0f, 0.4f}}, {{3.0f, 4.0f, 4.0f}}, false);
    V3 b = Scale::AllowedFactor({2.0f, 2.0f, 2.0f}, {{1.0f, 3.0f, 1.0f}}, {{4.0f, 4.0f, 4.0f}}, true);
    V3 c = Scale::AllowedFactor({0.5f, 0.5f, 0.5f}, {{1.0f, 0.4f, 1.0f}}, {{4.0f, 4.0f, 4.0f}}, true);
    V3 d = Scale::AllowedFactor({2.0f, 2.0f, 2.0f}, {{1.0f, 4.0f, 1.0f}}, {{4.0f, 4.0f, 4.0f}}, true);
    V3 e = Scale::AllowedFactor({2.0f, 1.0f, 1.0f}, {{1.0f, 4.0f, 1.0f}}, {{1.6f, 4.0f, 4.0f}}, false);
    Check(Near(a, {2.0f, 1.0f, 0.625f}, 1e-6f) && Near(b, Scale::Uniform(4.0f / 3.0f), 1e-6f) &&
              Near(c, Scale::Uniform(0.625f), 1e-6f) && Same(d, Scale::Uniform(1.0f)) && Near(e, {1.6f, 1.0f, 1.0f}, 1e-6f),
          "editor scale: limits per axis; uniform takes the most limited axis (none at all: 1)");
    Check(Near(Scale::CompressedLimit(2000.0f), 2.0f, 1e-6f) && Scale::CompressedLimit(500.0f) == Scale::kMax &&
              Scale::CompressedLimit(0.0f) == Scale::kMax,
          "editor scale: compressed collision's limit (4000 units across)");
    Check(Scale::Clamp(10.0f) == Scale::kMax && Scale::Clamp(0.01f) == Scale::kMin && Scale::Clamp(1.5f) == 1.5f &&
              Scale::Clamp(std::nanf("")) == 1.0f && Same(Scale::Clamp(V3{10.0f, 0.5f, 0.01f}), {4.0f, 0.5f, 0.25f}),
          "editor scale: the Inspector's value clamped");
    // The axis the camera faces most, a factor along it, an object's axis nearest a world axis.
    const V3 rows[3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0.1f}}; // (an object turned a quarter)
    Check(Scale::MainAxis({0.2f, -0.9f, 0.3f}) == 1 && Scale::MainAxis({-0.8f, 0.1f, 0.5f}) == 0 &&
              Scale::MainAxis({0.1f, 0.2f, -0.9f}) == 2 && Same(Scale::AlongAxis(2, 1.1f), {1.0f, 1.0f, 1.1f}) &&
              Same(Scale::AlongAxis(0, 0.5f), {0.5f, 1.0f, 1.0f}) && Scale::NearestAxis(rows, 0) == 2 &&
              Scale::NearestAxis(rows, 2) == 0 && Scale::NearestAxis(rows, 1) == 1,
          "editor scale: the camera's main axis, a factor along it, an object's nearest axis");
    Check(Scale::Describe(Scale::Uniform(1.5f)) == "1.50x" && Scale::Describe({2, 1, 0.5f}) == "2.00 x 1.00 x 0.50",
          "editor scale: described uniform / per axis");
}
SELFTEST(Early, TestEditorScaleLimits);

// --- vertices written again from their copy -------------------------------------------------------------------------

void TestEditorScaleVertices()
{
    // Three vertices of 24 bytes (position, then 12 bytes the scale leaves alone).
    const float original[9] = {10, 0, 20, 12, 5, 20, 10, 0, 26};
    uint8_t buffer[72];
    for (int v = 0; v < 3; ++v) {
        std::memcpy(buffer + 24 * v, original + 3 * v, 12);
        for (int k = 12; k < 24; ++k)
            buffer[24 * v + k] = static_cast<uint8_t>(0xA0 + v * 16 + k);
    }
    V3 from = {11, 0, 22}, to = {40, 3, -7}, s = {2.0f, 0.5f, 3.0f};
    Scale::RewritePositions(buffer, 24, 3, original, from, to, 1, s);
    bool written = true, kept = true;
    for (int v = 0; v < 3; ++v) {
        const float* p = reinterpret_cast<const float*>(buffer + 24 * v);
        V3 o = {original[3 * v], original[3 * v + 1], original[3 * v + 2]};
        V3 expect = to + LE::QuarterTurn(LE::PerAxis(o - from, s), 1);
        written &= Near({p[0], p[1], p[2]}, expect, 1e-4f);
        for (int k = 12; k < 24; ++k)
            kept &= buffer[24 * v + k] == static_cast<uint8_t>(0xA0 + v * 16 + k);
    }
    Check(written && kept, "editor scale: vertex positions written from their copy (turned, stretched, moved), the rest kept");

    // Many changes, then back to where it was copied: exactly the copy's numbers (nothing accumulates).
    for (int i = 0; i < 200; ++i)
        Scale::RewritePositions(buffer, 24, 3, original, from, {from.x + i * 0.37f, 2.0f, -i * 1.3f}, i & 3,
                                {0.25f + 0.0173f * i, 1.0f + 0.01f * i, 3.0f - 0.01f * i});
    Scale::RewritePositions(buffer, 24, 3, original, from, from, 0, Scale::Uniform(1.0f));
    bool exact = true;
    for (int v = 0; v < 3; ++v)
        exact &= std::memcmp(buffer + 24 * v, original + 3 * v, 12) == 0;
    Check(exact, "editor scale: back to 1x where it was gives the copy's own numbers (no drift)");

    // As the placement says: a sector's vertex v (as loaded) for any placement's engine state.
    LE::Placement e = {3, {5, -1, 2}, {0.6f, 1.5f, 2.0f}};
    int quarters;
    V3 position;
    V3 p0 = {100, 0, 50};
    LE::EngineState(0, p0, e, &quarters, &position);
    V3 loaded = {107, 3, 41};
    Check(Near(Scale::Rebuilt(loaded, p0, position, quarters, e.scale), LE::Apply(e, loaded), 1e-3f),
          "editor scale: a vertex rebuilt for the engine's state is where the placement puts it");
}
SELFTEST(Early, TestEditorScaleVertices);

// --- layout records -------------------------------------------------------------------------------------------------

void TestEditorScaleRecords()
{
    Check(Scale::Token(Scale::Uniform(1.0f)).empty() && Scale::Token(Scale::Uniform(1.5f)) == " scale=1.5" &&
              Scale::Token({2.0f, 1.0f, 0.5f}) == " scale=2,1,0.5",
          "editor scale: scale= written only when not 1 (one number when uniform)");
    V3 value = {}, odd = {1.1f * 1.1f * 1.1f * 1.1f * 1.1f, 0.3f, 1.0f / 3.0f};
    Check(Scale::ReadToken(Scale::Token(odd).substr(1), &value) && Same(value, odd) &&
              Scale::ReadToken(Scale::Token(Scale::Uniform(odd.x)).substr(1), &value) && Same(value, Scale::Uniform(odd.x)),
          "editor scale: scale= reads back exactly (three numbers, or one for all)");
    V3 untouched = {7.0f, 7.0f, 7.0f};
    Check(!Scale::ReadToken("scale=", &untouched) && !Scale::ReadToken("scale=-1", &untouched) &&
              !Scale::ReadToken("scale=0", &untouched) && !Scale::ReadToken("scale=abc", &untouched) &&
              !Scale::ReadToken("scale=nan", &untouched) && !Scale::ReadToken("scale=2x", &untouched) &&
              !Scale::ReadToken("size=2", &untouched) && !Scale::ReadToken("scale=1,2", &untouched) &&
              !Scale::ReadToken("scale=1,2,3,4", &untouched) && !Scale::ReadToken("scale=1,,2", &untouched) &&
              !Scale::ReadToken("scale=1,-2,3", &untouched) && !Scale::ReadToken("scale=1,2,", &untouched) &&
              !Scale::ReadToken("scale=1;2;3", &untouched) && Same(untouched, {7.0f, 7.0f, 7.0f}),
          "editor scale: bad scale= fields refused");

    Layout::File file;
    file.scene = 0x4B7E41E2;
    file.sectors.push_back({0xBF49DBEE, {1, {-188.25f, 0.0f, -13.5f}, Scale::Uniform(1.5f)}, false});
    file.sectors.push_back({0x01234567, {0, {5.0f, 0.0f, 0.0f}}, false});
    file.sectors.push_back({0x89ABCDEF, {2, {1.0f, 2.0f, 3.0f}, {2.0f, 1.0f, 0.75f}}, true});
    file.rails.push_back({0x835E979B, {1, {-188.25f, 0.0f, -13.5f}, Scale::Uniform(1.5f)}, false});
    file.clones.push_back({0xBF49DBEE, {2, {40.0f, 1.0f, 2.0f}, {0.5f, 0.5f, 3.0f}}});
    Layout::ObjectRecord object = {0x0B1EC700, {1, 2, 3}, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    object.scale = {2.0f, 1.0f, 1.5f};
    file.objects.push_back(object);
    file.objects.push_back({0x0B1EC701, {4, 5, 6}, {1, 0, 0, 0, 1, 0, 0, 0, 1}});
    Layout::CopyRecord copy = {0x0C0B1000, 0x0B1EC700, {7, 8, 9}, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    copy.scale = Scale::Uniform(0.5f);
    file.copies.push_back(copy);
    std::string text = Layout::Write(file);
    Check(text.find("\nsector BF49DBEE 1 -188.25 0 -13.5 0 scale=1.5") != std::string::npos &&
              text.find("\nsector 01234567 0 5 0 0 0") != std::string::npos &&
              text.find("\nsector 01234567 0 5 0 0 0 scale") == std::string::npos &&
              text.find("\nsector 89ABCDEF 2 1 2 3 1 scale=2,1,0.75") != std::string::npos &&
              text.find("\nrail 835E979B 1 -188.25 0 -13.5 0 scale=1.5") != std::string::npos &&
              text.find("\nclone BF49DBEE 2 40 1 2 scale=0.5,0.5,3") != std::string::npos &&
              text.find("\nobject 0B1EC700 1 2 3 1 0 0 0 1 0 0 0 1 scale=2,1,1.5") != std::string::npos &&
              text.find("\nobject 0B1EC701 4 5 6 1 0 0 0 1 0 0 0 1") != std::string::npos &&
              text.find("\nobject 0B1EC701 4 5 6 1 0 0 0 1 0 0 0 1 scale") == std::string::npos &&
              text.find("\ncopy 0C0B1000 0B1EC700 7 8 9 1 0 0 0 1 0 0 0 1 scale=0.5") != std::string::npos,
          "editor scale: sector / rail / clone / object / copy records carry scale= after their numbers (none when 1)");
    Layout::File back;
    int line = -1;
    bool read = Layout::Read(text, &back, &line) && back.sectors.size() == 3 && back.rails.size() == 1 &&
                back.clones.size() == 1 && back.objects.size() == 2 && back.copies.size() == 1;
    Check(read && Same(back.sectors[0].placement.scale, Scale::Uniform(1.5f)) &&
              Same(back.sectors[1].placement.scale, Scale::Uniform(1.0f)) &&
              Same(back.sectors[2].placement.scale, {2.0f, 1.0f, 0.75f}) && back.sectors[2].hidden &&
              Same(back.rails[0].placement.scale, Scale::Uniform(1.5f)) &&
              Same(back.clones[0].placement.scale, {0.5f, 0.5f, 3.0f}) && Same(back.objects[0].scale, {2.0f, 1.0f, 1.5f}) &&
              Same(back.objects[1].scale, Scale::Uniform(1.0f)) && Same(back.copies[0].scale, Scale::Uniform(0.5f)) &&
              back.sectors[0].placement.quarters == 1 && back.sectors[0].placement.offset.x == -188.25f &&
              back.objects[0].pos[2] == 3.0f && back.copies[0].source == 0x0B1EC700,
          "editor scale: scales read back (1 where none is written)");

    // Older files (no scale=) read as 1x; a later version's key=value field is skipped; a bad scale refuses the file.
    const char kOld[] = "thug2-decomp layout 2\nscene 4B7E41E2\nsector BF49DBEE 0 -188.212769 0 -13.4348145 0  # x\n"
                        "clone BF49DBEE 1 5 0 5\nobject 0B1EC700 1 2 3 1 0 0 0 1 0 0 0 1  # y\n";
    Check(Layout::Read(kOld, &back, &line) && back.sectors.size() == 1 &&
              Same(back.sectors[0].placement.scale, Scale::Uniform(1.0f)) && back.clones.size() == 1 &&
              Same(back.clones[0].placement.scale, Scale::Uniform(1.0f)) && back.objects.size() == 1 &&
              Same(back.objects[0].scale, Scale::Uniform(1.0f)),
          "editor scale: a layout without scales reads as before (1x)");
    const char kLater[] = "thug2-decomp layout 2\nscene 4B7E41E2\nsector BF49DBEE 0 1 2 3 0 tint=1 scale=2\n"
                          "object 0B1EC700 1 2 3 1 0 0 0 1 0 0 0 1 tint=2 scale=1,3,1\n";
    Check(Layout::Read(kLater, &back, &line) && back.sectors.size() == 1 &&
              Same(back.sectors[0].placement.scale, Scale::Uniform(2.0f)) && back.objects.size() == 1 &&
              Same(back.objects[0].scale, {1.0f, 3.0f, 1.0f}),
          "editor scale: other key=value fields after a record are skipped");
    const char kBad[] = "thug2-decomp layout 2\nscene 4B7E41E2\nsector BF49DBEE 0 1 2 3 0 scale=-2\n";
    const char kBadObject[] = "thug2-decomp layout 2\nscene 4B7E41E2\n\nobject 0B1EC700 1 2 3 1 0 0 0 1 0 0 0 1 scale=1,0,1\n";
    Check(!Layout::Read(kBad, &back, &line) && line == 3 && !Layout::Read(kBadObject, &back, &line) && line == 4,
          "editor scale: a bad scale= refuses the file (its line)");
}
SELFTEST(Early, TestEditorScaleRecords);

// --- collision ------------------------------------------------------------------------------------------------------

// A small uncompressed geometry: four vertices, its box, a BSP tree of one split (x at 12) over two leaves.
struct TestGeometry {
    float vertices[12] = {10, 0, 20, 14, 2, 20, 10, 0, 26, 13, 4, 25};
    alignas(8) Nx::CCollBSPNode nodes[3] = {};
    uint8_t faces[10] = {};
    Nx::CCollObjTriData data = {};

    TestGeometry()
    {
        data.m_numVertices = 4;
        data.m_compressed = false;
        data.m_vertices = vertices;
        data.m_faces = faces;
        data.m_min = {10, 0, 20, 1};
        data.m_max = {14, 4, 26, 1};
        std::memcpy(&data.m_unknown3C, "\x00\x00\x80\x3F", 4); // (a rejection scale of 1)
        nodes[0].m_split = (12 * 16) << 2;                       // (x at 12, in sixteenths)
        nodes[0].m_link = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&nodes[1]));
        nodes[1].m_split = nodes[2].m_split = 3; // (leaves without faces)
        data.m_bsp = &nodes[0];
    }
};

void TestEditorScaleCollision()
{
    TestGeometry g;
    Scale::CollisionCopy copy = Scale::CopyCollision(&g.data);
    Check(copy.nodes.size() == 1 && copy.vertices.size() == 48 && Scale::SameCollision(copy),
          "editor scale: a collision geometry copied (vertices, one split node)");

    V3 from = {11, 0, 22}, to = {40, 3, -7}, s = {2.0f, 0.5f, 3.0f};
    Scale::RebuildCollision(copy, 1, from, to, s);
    bool vertices = true;
    const TestGeometry loaded;
    for (int v = 0; v < 4; ++v) {
        V3 o = {loaded.vertices[3 * v], loaded.vertices[3 * v + 1], loaded.vertices[3 * v + 2]};
        V3 expect = Scale::Rebuilt(o, from, to, 1, s);
        vertices &= Near({g.vertices[3 * v], g.vertices[3 * v + 1], g.vertices[3 * v + 2]}, expect, 1e-3f);
    }
    // The box holds every vertex (x 4 * 2 long, turned onto z; z 6 * 3 onto x; y 4 * 0.5); the split on x at 12 is now
    // on z (a quarter turn takes x to -z) at to.z - 2 * (12 - from.x), its sides swapped.
    bool box = Near(g.data.m_max.x - g.data.m_min.x, 3 * 6.0f, 1e-3f) && Near(g.data.m_max.z - g.data.m_min.z, 2 * 4.0f, 1e-3f) &&
               Near(g.data.m_max.y - g.data.m_min.y, 0.5f * 4.0f, 1e-3f);
    for (int v = 0; v < 4; ++v)
        box &= g.vertices[3 * v] >= g.data.m_min.x - 1e-3f && g.vertices[3 * v] <= g.data.m_max.x + 1e-3f &&
               g.vertices[3 * v + 2] >= g.data.m_min.z - 1e-3f && g.vertices[3 * v + 2] <= g.data.m_max.z + 1e-3f;
    float split = g.nodes[0].Split() / 16.0f;
    bool tree = g.nodes[0].Axis() == 2 && Near(split, to.z - 2.0f * (12.0f - from.x), 0.13f) && (g.nodes[0].m_link & 1);
    Check(vertices && box && tree, "editor scale: collision stretched, turned and moved - vertices, box and tree agree");
    Check(g.data.RejectionScale() == 0.0f, "editor scale: scaled collision rejects no faces by distance");

    // Many rebuilds, then back as copied: the copy's exact bytes, box, tree and rejection scale.
    for (int i = 0; i < 100; ++i)
        Scale::RebuildCollision(copy, i & 3, from, {from.x + i * 3.1f, i * 0.5f, -i * 2.7f},
                                {0.25f + 0.037f * i, 1.0f + 0.02f * i, 4.0f - 0.03f * i});
    Scale::RebuildCollision(copy, 0, from, from, Scale::Uniform(1.0f));
    TestGeometry fresh;
    Check(std::memcmp(g.vertices, fresh.vertices, sizeof(g.vertices)) == 0 && g.data.m_min == fresh.data.m_min &&
              g.data.m_max == fresh.data.m_max && g.nodes[0].m_split == fresh.nodes[0].m_split &&
              (g.nodes[0].m_link & 3) == (fresh.nodes[0].m_link & 3) && g.data.m_unknown3C == fresh.data.m_unknown3C,
          "editor scale: collision back to 1x as copied is exact (no drift in vertices, box or tree)");

    // Another geometry in its place (another level): not the one copied.
    float other[12] = {};
    g.data.m_vertices = other;
    Check(!Scale::SameCollision(copy), "editor scale: a geometry with other arrays isn't the one copied");

    // Compressed collision: its limit on each axis from its size now and its scale.
    Nx::CCollObjTriData c = {};
    c.m_compressed = true;
    c.m_min = {0, 0, 0, 1};
    c.m_max = {1000, 200, 300, 1};
    Check(Same(Scale::CollisionLimit(&c, Scale::Uniform(1.0f)), Scale::Uniform(Scale::kMax)),
          "editor scale: compressed, 1000 across: up to 4x");
    c.m_max.x = 2000;
    c.m_max.z = 3200;
    Check(Near(Scale::CollisionLimit(&c, Scale::Uniform(1.0f)), {2.0f, 4.0f, 1.25f}, 1e-5f) &&
              Near(Scale::CollisionLimit(&c, {2.0f, 1.0f, 1.25f}), {4.0f, 4.0f, 1.5625f}, 1e-5f),
          "editor scale: compressed: each axis limited by its own side (already scaled: of its own size)");
    c.m_compressed = false;
    Check(Same(Scale::CollisionLimit(&c, Scale::Uniform(1.0f)), Scale::Uniform(Scale::kMax)) &&
              Same(Scale::CollisionLimit(nullptr, Scale::Uniform(1.0f)), Scale::Uniform(Scale::kMax)),
          "editor scale: float collision (or none): 4x");
}
SELFTEST(Early, TestEditorScaleCollision);

// --- objects --------------------------------------------------------------------------------------------------------

using namespace TestStandIns;

// A stand-in heap for the geometry's copy (Mem::New / Calloc / free): a bump allocator.
alignas(16) uint8_t g_heap[0x4000];
uint32_t g_heapUsed;
uint32_t Allocate(uint32_t size)
{
    uint32_t at = (g_heapUsed + 15) & ~15u;
    if (at + size > sizeof(g_heap))
        return 0;
    g_heapUsed = at + size;
    std::memset(g_heap + at, 0, size);
    return reinterpret_cast<uint32_t>(g_heap + at);
}

// An object with a model component (its model) and a collision component (a triangle object over a test geometry, or
// a box).
struct TestObject {
    uint8_t object[sizeof(Obj::CCompositeObject)] = {};
    uint8_t model[0x240] = {};
    uint8_t modelData[0x60] = {};
    uint8_t collision[0x40] = {};
    uint8_t tri[sizeof(Nx::CCollMovTri)] = {};
    uint8_t box[sizeof(Nx::CCollMovBBox)] = {};
    TestGeometry geometry;
    void* vtable[16] = {};

    Obj::CCompositeObject* Object() { return reinterpret_cast<Obj::CCompositeObject*>(object); }
    Nx::CCollMovTri* Tri() { return reinterpret_cast<Nx::CCollMovTri*>(tri); }
    Nx::CCollMovBBox* Box() { return reinterpret_cast<Nx::CCollMovBBox*>(box); }

    TestObject(uint32_t id, bool withBox, void* teleport)
    {
        for (void*& slot : vtable)
            slot = teleport;
        auto* o = Object();
        o->m_id = id;
        auto* m = reinterpret_cast<Obj::CBaseComponent*>(model);
        m->m_vtable = vtable;
        m->m_type = 0x286A8D26;
        *reinterpret_cast<uint8_t**>(model + 0x208) = modelData;
        auto* c = reinterpret_cast<Obj::CCollisionComponent*>(collision);
        c->m_vtable = vtable;
        c->m_type = Obj::CCollisionComponent::kType;
        m->mp_next = c;
        o->mp_components = m;
        if (withBox) {
            Box()->Construct();
            c->mp_collision = Box();
        } else {
            Tri()->Construct(&geometry.data);
            c->mp_collision = Tri();
        }
    }
};

void TestEditorScaleObjects()
{
    StandIns& s = *new StandIns();
    g_heapUsed = 0;
    s.At({AddressOf(Mem::New)}, 0, [](Regs& r) { return Allocate(r.args[0]); });
    s.At({AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) { return Allocate(r.args[0]); });
    void* teleport = s.Thunk(0, [](Regs&) { return 0u; });

    // A triangle object: its own copy of the geometry, stretched; the model's scale; the shared geometry untouched.
    auto* t = new TestObject(0x0B1EC700, false, teleport);
    Nx::CCollObjTriData* shared = &t->geometry.data;
    const TestGeometry loaded;
    Check(Scale::ObjectScalable(t->Object()) && Same(Scale::ObjectScale(t->Object()), Scale::Uniform(1.0f)) &&
              Scale::SetObjectScale(t->Object(), Scale::Uniform(1.0f)) && t->Tri()->m_geometry == shared,
          "editor scale: an object unscaled: nothing copied");
    V3 stretch = {2.0f, 1.0f, 0.5f};
    bool set = Scale::SetObjectScale(t->Object(), stretch);
    Nx::CCollObjTriData* own = t->Tri()->m_geometry;
    const float* scaledModel = reinterpret_cast<const float*>(t->modelData + 0x1C);
    bool model = scaledModel[0] == 2.0f && scaledModel[1] == 1.0f && scaledModel[2] == 0.5f && (t->modelData[0x44] & 8);
    bool geometry = own != shared && (t->Tri()->m_flags & Nx::CCollObj::kOwnsGeometry) &&
                    std::memcmp(t->geometry.vertices, loaded.vertices, sizeof(loaded.vertices)) == 0;
    float radius = 0.0f;
    for (int v = 0; v < 4 && own; ++v) {
        Mth::Vector p = own->GetRawVertexPos(v);
        geometry &= Near({p.x, p.y, p.z}, LE::PerAxis({loaded.vertices[3 * v], loaded.vertices[3 * v + 1], loaded.vertices[3 * v + 2]}, stretch), 1e-4f);
        radius = std::max(radius, std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
    }
    geometry &= own && Near(t->Tri()->m_radius, radius, 1e-3f) && t->Tri()->m_unknownD0 == 0 &&
                 Near(own->m_max.x, 28.0f, 1e-4f) && Near(own->m_max.z, 13.0f, 1e-4f) &&
                 (t->Object()->m_compositeFlags & Obj::COMPOSITE_FLAG_TELEPORTED);
    Check(set && model && geometry && Same(Scale::ObjectScale(t->Object()), stretch),
          "editor scale: an object stretched - its model, its own copy of the collision (vertices, box, radius)");

    // Limits: each axis within 0.25..4.
    Scale::SetObjectScale(t->Object(), {10.0f, 0.1f, 1.0f});
    Check(Same(Scale::ObjectScale(t->Object()), {4.0f, 0.25f, 1.0f}) && t->Tri()->m_geometry == own,
          "editor scale: an object's scale kept within 0.25x..4x (the same copy)");

    // Back to 1: the copy's exact numbers, the model unscaled, the rejection scale back.
    for (int i = 0; i < 30; ++i)
        Scale::SetObjectScale(t->Object(), {0.3f + 0.1f * i, 1.0f + 0.05f * i, 2.0f - 0.03f * i});
    Scale::SetObjectScale(t->Object(), Scale::Uniform(1.0f));
    bool back = !(t->modelData[0x44] & 8) && t->Tri()->m_unknownD0 == loaded.data.m_unknown3C &&
                own->m_min == loaded.data.m_min && own->m_max == loaded.data.m_max;
    for (int v = 0; v < 4; ++v) {
        Mth::Vector p = own->GetRawVertexPos(v);
        back &= p.x == loaded.vertices[3 * v] && p.y == loaded.vertices[3 * v + 1] && p.z == loaded.vertices[3 * v + 2];
    }
    Check(back, "editor scale: an object back to 1x - exactly its collision as copied, the model unscaled");

    // Its collision replaced (made again): the record is forgotten.
    Scale::SetObjectScale(t->Object(), stretch);
    reinterpret_cast<Obj::CCollisionComponent*>(t->collision)->mp_collision = t->Box();
    Check(Same(Scale::ObjectScale(t->Object()), Scale::Uniform(1.0f)), "editor scale: an object whose collision changed starts afresh");

    // A box: scaled per axis from its first box, back exactly.
    auto* b = new TestObject(0x0B1EC701, true, teleport);
    Mth::BBox first = b->Box()->m_box;
    Scale::SetObjectScale(b->Object(), {2.0f, 3.0f, 0.5f});
    bool boxed = b->Box()->m_box.min.x == first.min.x * 2.0f && b->Box()->m_box.max.y == first.max.y * 3.0f &&
                 b->Box()->m_box.min.z == first.min.z * 0.5f && b->Box()->m_box.min.y == first.min.y * 3.0f;
    Scale::SetObjectScale(b->Object(), {1.5f, 1.0f, 1.0f});
    boxed &= b->Box()->m_box.max.x == first.max.x * 1.5f && b->Box()->m_box.max.y == first.max.y;
    Scale::SetObjectScale(b->Object(), Scale::Uniform(1.0f));
    boxed &= b->Box()->m_box.min == first.min && b->Box()->m_box.max == first.max;
    Check(boxed, "editor scale: an object's collision box scaled per axis (from its first box)");

    // Compressed collision limits an object's axes; no model: not scalable; dead: not scalable.
    auto* c = new TestObject(0x0B1EC702, false, teleport);
    c->geometry.data.m_compressed = true;
    c->geometry.data.m_max = {2010, 4, 1010, 1};
    V3 limit = Scale::ObjectLimit(c->Object());
    c->geometry.data.m_compressed = false;
    *reinterpret_cast<uint8_t**>(c->model + 0x208) = nullptr;
    bool noModel = !Scale::ObjectScalable(c->Object()) && !Scale::SetObjectScale(c->Object(), stretch);
    *reinterpret_cast<uint8_t**>(c->model + 0x208) = c->modelData;
    c->Object()->m_objectFlags |= Obj::OBJECT_FLAG_DEAD;
    bool dead = !Scale::ObjectScalable(c->Object());
    Check(Near(limit, {2.0f, 4.0f, 4.0f}, 1e-5f) && noModel && dead,
          "editor scale: an object's limits (compressed collision), not scalable without a model or dead");

    Scale::ForgetObjects();
    delete t;
    delete b;
    delete c;
    delete &s;
}
SELFTEST(Early, TestEditorScaleObjects, RemoveStandIns);

} // namespace
