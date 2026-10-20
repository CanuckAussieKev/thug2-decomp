// Self-tests for the level editor's scaling of scenery (src/game/front/level_editor_scale.*, not original): placements
// with a scale (scaling about a point, composed with turns and moves, the engine's state and boxes), the limits, the
// vertices written again from their copy (no drift), the layout records' scale= and a collision geometry put back and
// scaled / turned / moved with the game's own CCollObjTriData code (vertices, box and BSP tree agreeing).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_scale.h"
#include "game/nx/coll_tri.h"
#include "hook/selftest.h"

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

const V3 kPoints[] = {{0, 0, 0}, {3, 1, -2}, {-50, 7, 12}, {120.5f, -3, 64}};

// --- placements -----------------------------------------------------------------------------------------------------

void TestEditorScalePlacement()
{
    // Scaling about a point: every point's distance from it times the factor.
    LE::Placement p = {1, {10, 0, 5}};
    LE::Placement before = p;
    V3 pivot = {4, -2, 9};
    Scale::ScaleAbout(&p, pivot, 1.5f);
    bool about = Near(p.scale, 1.5f, 0) && p.quarters == 1;
    for (V3 x : kPoints)
        about &= Near(LE::Apply(p, x), (LE::Apply(before, x) - pivot) * 1.5f + pivot, 1e-3f);
    Check(about, "editor scale: scaled about a point (after a turn and a move)");

    // Then turned and moved: still one placement for the lot; back by the inverse factor about the same point.
    LE::Placement q = p;
    LE::TurnAbout(&q, {-6, 0, 2}, 3);
    LE::MoveBy(&q, {1, 2, 3});
    bool combined = true;
    for (V3 x : kPoints) {
        V3 expect = LE::QuarterTurn(LE::Apply(p, x) - V3{-6, 0, 2}, 3) + V3{-6, 0, 2} + V3{1, 2, 3};
        combined &= Near(LE::Apply(q, x), expect, 1e-3f);
    }
    Scale::ScaleAbout(&p, pivot, 1.0f / 1.5f);
    bool undone = Near(p.scale, 1.0f, 1e-6f);
    for (V3 x : kPoints)
        undone &= Near(LE::Apply(p, x), LE::Apply(before, x), 1e-3f);
    Check(combined && undone, "editor scale: with turns and moves; the inverse factor undoes it");

    // Composed (a copy of a scaled piece, moved and scaled since).
    LE::Placement inner = {1, {10, 0, 5}, 2.0f}, outer = {3, {-4, 2, 7}, 0.75f};
    LE::Placement both = LE::Compose(outer, inner);
    bool composed = Near(both.scale, 1.5f, 1e-6f);
    for (V3 x : kPoints)
        composed &= Near(LE::Apply(both, x), LE::Apply(outer, LE::Apply(inner, x)), 1e-3f);
    Check(composed, "editor scale: composed placements multiply their scales");

    // The engine's state: the vertices as loaded at Q0 v + p0 are now at s Q v + position.
    LE::Placement e = {2, {30, 4, -8}, 1.25f};
    int quarters;
    V3 position;
    LE::EngineState(1, {100, 0, 50}, e, &quarters, &position);
    bool engine = quarters == 3;
    for (V3 v : kPoints) {
        V3 first = LE::QuarterTurn(v, 1) + V3{100, 0, 50};
        engine &= Near(LE::QuarterTurn(v, quarters) * 1.25f + position, LE::Apply(e, first), 1e-3f);
    }
    Check(engine, "editor scale: the engine's quarters and position for a scaled placement");

    // A placed box: its corners where the placement puts the loaded box's corners.
    V3 min = {0, 0, 0}, max = {10, 4, 6};
    LE::OrientedBox box = LE::PlacedBox(min, max, e);
    V3 corners[8];
    LE::BoxCorners(box.origin, box.axes, box.min, box.max, corners);
    bool placed = true;
    for (int i = 0; i < 8; ++i) {
        V3 loaded = {i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z};
        placed &= Near(corners[i], LE::Apply(e, loaded), 1e-3f);
    }
    float t = 0.0f;
    // (placed at x 17.5..30, y 4..9, z -15.5..-8: half a turn, 1.25x)
    bool hit = LE::RayBox({1000, 6, -10}, {-1, 0, 0}, box.origin, box.axes, box.min, box.max, &t) && Near(t, 970.0f, 1e-2f);
    Check(placed && hit, "editor scale: a scaled sector's box (corners placed, rays still meet it)");
}
SELFTEST(Early, TestEditorScalePlacement);

// --- limits ---------------------------------------------------------------------------------------------------------

void TestEditorScaleLimits()
{
    Check(Near(Scale::AllowedFactor(2.0f, {1.0f}, {4.0f}), 2.0f, 0) &&
              Near(Scale::AllowedFactor(8.0f, {1.0f}, {4.0f}), 4.0f, 1e-6f) &&
              Near(Scale::AllowedFactor(2.0f, {3.0f}, {4.0f}), 4.0f / 3.0f, 1e-6f) &&
              Near(Scale::AllowedFactor(0.1f, {1.0f}, {4.0f}), 0.25f, 1e-6f),
          "editor scale: a factor kept within 0.25x..4x");
    Check(Scale::AllowedFactor(2.0f, {4.0f}, {4.0f}) == 1.0f && Scale::AllowedFactor(0.5f, {0.25f}, {4.0f}) == 1.0f &&
              Scale::AllowedFactor(1.1f, {4.0f}, {4.0f}) == 1.0f,
          "editor scale: at a limit, no further that way");
    Check(Near(Scale::AllowedFactor(0.5f, {4.0f}, {4.0f}), 0.5f, 0),
          "editor scale: at the top, down still works");
    Check(Near(Scale::AllowedFactor(2.0f, {1.0f}, {1.5f}), 1.5f, 1e-6f) &&
              Near(Scale::AllowedFactor(2.0f, {1.0f, 1.5f}, {4.0f, 4.0f}), 2.0f, 0) &&
              Near(Scale::AllowedFactor(4.0f, {1.0f, 2.0f}, {4.0f, 4.0f}), 2.0f, 1e-6f),
          "editor scale: a group limited by its most limited piece (compressed collision too)");
    Check(Scale::AllowedFactor(std::nanf(""), {1.0f}, {4.0f}) == 1.0f && Scale::AllowedFactor(-2.0f, {1.0f}, {4.0f}) == 1.0f &&
              Scale::AllowedFactor(0.0f, {1.0f}, {4.0f}) == 1.0f,
          "editor scale: a factor that isn't a positive number does nothing");
    Check(Near(Scale::CompressedLimit(2000.0f), 2.0f, 1e-6f) && Scale::CompressedLimit(500.0f) == Scale::kMax &&
              Scale::CompressedLimit(0.0f) == Scale::kMax,
          "editor scale: compressed collision's limit (4000 units across)");
    Check(Scale::Clamp(10.0f) == Scale::kMax && Scale::Clamp(0.01f) == Scale::kMin && Scale::Clamp(1.5f) == 1.5f &&
              Scale::Clamp(std::nanf("")) == 1.0f,
          "editor scale: the Inspector's value clamped");
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
    V3 from = {11, 0, 22}, to = {40, 3, -7};
    Scale::RewritePositions(buffer, 24, 3, original, from, to, 1, 2.0f);
    bool written = true, kept = true;
    for (int v = 0; v < 3; ++v) {
        const float* p = reinterpret_cast<const float*>(buffer + 24 * v);
        V3 o = {original[3 * v], original[3 * v + 1], original[3 * v + 2]};
        V3 expect = to + LE::QuarterTurn((o - from) * 2.0f, 1);
        written &= Near({p[0], p[1], p[2]}, expect, 1e-4f);
        for (int k = 12; k < 24; ++k)
            kept &= buffer[24 * v + k] == static_cast<uint8_t>(0xA0 + v * 16 + k);
    }
    Check(written && kept, "editor scale: vertex positions written from their copy (turned, scaled, moved), the rest kept");

    // Many changes, then back to where it was copied: exactly the copy's numbers (nothing accumulates).
    for (int i = 0; i < 200; ++i)
        Scale::RewritePositions(buffer, 24, 3, original, from, {from.x + i * 0.37f, 2.0f, -i * 1.3f}, i & 3,
                                0.25f + 0.0173f * i);
    Scale::RewritePositions(buffer, 24, 3, original, from, from, 0, 1.0f);
    bool exact = true;
    for (int v = 0; v < 3; ++v)
        exact &= std::memcmp(buffer + 24 * v, original + 3 * v, 12) == 0;
    Check(exact, "editor scale: back to 1x where it was gives the copy's own numbers (no drift)");

    // As the placement says: a sector's vertex v (as loaded) for any placement's engine state.
    LE::Placement e = {3, {5, -1, 2}, 0.6f};
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
    Check(Scale::Token(1.0f).empty() && Scale::Token(1.5f) == " scale=1.5", "editor scale: scale= written only when not 1");
    float value = 0.0f, odd = 1.1f * 1.1f * 1.1f * 1.1f * 1.1f;
    Check(Scale::ReadToken(Scale::Token(odd).substr(1), &value) && value == odd,
          "editor scale: scale= reads back exactly");
    float untouched = 7.0f;
    Check(!Scale::ReadToken("scale=", &untouched) && !Scale::ReadToken("scale=-1", &untouched) &&
              !Scale::ReadToken("scale=0", &untouched) && !Scale::ReadToken("scale=abc", &untouched) &&
              !Scale::ReadToken("scale=nan", &untouched) && !Scale::ReadToken("scale=2x", &untouched) &&
              !Scale::ReadToken("size=2", &untouched) && untouched == 7.0f,
          "editor scale: bad scale= fields refused");

    Layout::File file;
    file.scene = 0x4B7E41E2;
    file.sectors.push_back({0xBF49DBEE, {1, {-188.25f, 0.0f, -13.5f}, 1.5f}, false});
    file.sectors.push_back({0x01234567, {0, {5.0f, 0.0f, 0.0f}}, false});
    file.rails.push_back({0x835E979B, {1, {-188.25f, 0.0f, -13.5f}, 1.5f}, false});
    file.clones.push_back({0xBF49DBEE, {2, {40.0f, 1.0f, 2.0f}, 0.5f}});
    std::string text = Layout::Write(file);
    Check(text.find("\nsector BF49DBEE 1 -188.25 0 -13.5 0 scale=1.5") != std::string::npos &&
              text.find("\nsector 01234567 0 5 0 0 0") != std::string::npos &&
              text.find("\nsector 01234567 0 5 0 0 0 scale") == std::string::npos &&
              text.find("\nrail 835E979B 1 -188.25 0 -13.5 0 scale=1.5") != std::string::npos &&
              text.find("\nclone BF49DBEE 2 40 1 2 scale=0.5") != std::string::npos,
          "editor scale: sector / rail / clone records carry scale= after their numbers (none when 1)");
    Layout::File back;
    int line = -1;
    bool read = Layout::Read(text, &back, &line) && back.sectors.size() == 2 && back.rails.size() == 1 &&
                back.clones.size() == 1;
    Check(read && back.sectors[0].placement.scale == 1.5f && back.sectors[1].placement.scale == 1.0f &&
              back.rails[0].placement.scale == 1.5f && back.clones[0].placement.scale == 0.5f &&
              back.sectors[0].placement.quarters == 1 && back.sectors[0].placement.offset.x == -188.25f,
          "editor scale: scales read back (1 where none is written)");

    // Older files (no scale=) read as 1x; a later version's key=value field is skipped; a bad scale refuses the file.
    const char kOld[] = "thug2-decomp layout 2\nscene 4B7E41E2\nsector BF49DBEE 0 -188.212769 0 -13.4348145 0  # x\n"
                        "clone BF49DBEE 1 5 0 5\n";
    Check(Layout::Read(kOld, &back, &line) && back.sectors.size() == 1 && back.sectors[0].placement.scale == 1.0f &&
              back.clones.size() == 1 && back.clones[0].placement.scale == 1.0f,
          "editor scale: a layout without scales reads as before (1x)");
    const char kLater[] = "thug2-decomp layout 2\nscene 4B7E41E2\nsector BF49DBEE 0 1 2 3 0 tint=1 scale=2\n";
    Check(Layout::Read(kLater, &back, &line) && back.sectors.size() == 1 && back.sectors[0].placement.scale == 2.0f,
          "editor scale: other key=value fields after a record are skipped");
    const char kBad[] = "thug2-decomp layout 2\nscene 4B7E41E2\nsector BF49DBEE 0 1 2 3 0 scale=-2\n";
    Check(!Layout::Read(kBad, &back, &line) && line == 3, "editor scale: a bad scale= refuses the file (its line)");
    // (an older reader reads a record's fields up to `hidden` and ignores the rest: the records above put scale= after
    // all of them, so an older DLL reads the file - the pieces placed unscaled)
}
SELFTEST(Early, TestEditorScaleRecords);

// --- collision ------------------------------------------------------------------------------------------------------

// A small uncompressed geometry: four vertices, its box, a BSP tree of one split (x at 12) over two leaves.
struct TestGeometry {
    float vertices[12] = {10, 0, 20, 14, 2, 20, 10, 0, 26, 13, 4, 25};
    alignas(8) Nx::CCollBSPNode nodes[3] = {};
    Nx::CCollObjTriData data = {};

    TestGeometry()
    {
        data.m_numVertices = 4;
        data.m_compressed = false;
        data.m_vertices = vertices;
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

    V3 from = {11, 0, 22}, to = {40, 3, -7};
    Scale::RebuildCollision(copy, 1, from, to, 2.0f);
    bool vertices = true;
    const TestGeometry loaded;
    for (int v = 0; v < 4; ++v) {
        V3 o = {loaded.vertices[3 * v], loaded.vertices[3 * v + 1], loaded.vertices[3 * v + 2]};
        V3 expect = Scale::Rebuilt(o, from, to, 1, 2.0f);
        vertices &= Near({g.vertices[3 * v], g.vertices[3 * v + 1], g.vertices[3 * v + 2]}, expect, 1e-3f);
    }
    // The box holds every vertex (sides doubled); the split on x at 12 is now on z (a quarter turn takes x to -z) at
    // to.z - 2 (12 - from.x), its sides swapped.
    bool box = Near(g.data.m_max.x - g.data.m_min.x, 2 * 6.0f, 1e-3f) && Near(g.data.m_max.z - g.data.m_min.z, 2 * 4.0f, 1e-3f) &&
               Near(g.data.m_max.y - g.data.m_min.y, 2 * 4.0f, 1e-3f);
    for (int v = 0; v < 4; ++v)
        box &= g.vertices[3 * v] >= g.data.m_min.x - 1e-3f && g.vertices[3 * v] <= g.data.m_max.x + 1e-3f &&
               g.vertices[3 * v + 2] >= g.data.m_min.z - 1e-3f && g.vertices[3 * v + 2] <= g.data.m_max.z + 1e-3f;
    float split = g.nodes[0].Split() / 16.0f;
    bool tree = g.nodes[0].Axis() == 2 && Near(split, to.z - 2.0f * (12.0f - from.x), 0.13f) && (g.nodes[0].m_link & 1);
    Check(vertices && box && tree, "editor scale: collision scaled, turned and moved - vertices, box and tree agree");
    Check(g.data.RejectionScale() == 0.0f, "editor scale: scaled collision rejects no faces by distance");

    // Many rebuilds, then back as copied: the copy's exact bytes, box, tree and rejection scale.
    for (int i = 0; i < 100; ++i)
        Scale::RebuildCollision(copy, i & 3, from, {from.x + i * 3.1f, i * 0.5f, -i * 2.7f}, 0.25f + 0.037f * i);
    Scale::RebuildCollision(copy, 0, from, from, 1.0f);
    TestGeometry fresh;
    Check(std::memcmp(g.vertices, fresh.vertices, sizeof(g.vertices)) == 0 && g.data.m_min == fresh.data.m_min &&
              g.data.m_max == fresh.data.m_max && g.nodes[0].m_split == fresh.nodes[0].m_split &&
              (g.nodes[0].m_link & 3) == (fresh.nodes[0].m_link & 3) && g.data.m_unknown3C == fresh.data.m_unknown3C,
          "editor scale: collision back to 1x as copied is exact (no drift in vertices, box or tree)");

    // Another geometry in its place (another level): not the one copied.
    float other[12] = {};
    g.data.m_vertices = other;
    Check(!Scale::SameCollision(copy), "editor scale: a geometry with other arrays isn't the one copied");

    // Compressed collision: its limit from its size now and its scale.
    Nx::CCollObjTriData c = {};
    c.m_compressed = true;
    c.m_min = {0, 0, 0, 1};
    c.m_max = {1000, 200, 300, 1};
    Check(Scale::CollisionLimit(&c, 1.0f) == Scale::kMax, "editor scale: compressed, 1000 across: up to 4x");
    c.m_max.x = 2000;
    Check(Near(Scale::CollisionLimit(&c, 1.0f), 2.0f, 1e-5f) && Scale::CollisionLimit(&c, 2.0f) == Scale::kMax,
          "editor scale: compressed, 2000 across: 2x (already 2x: 4x of its own size)");
    c.m_compressed = false;
    Check(Scale::CollisionLimit(&c, 1.0f) == Scale::kMax && Scale::CollisionLimit(nullptr, 1.0f) == Scale::kMax,
          "editor scale: float collision (or none): 4x");
}
SELFTEST(Early, TestEditorScaleCollision);

} // namespace
