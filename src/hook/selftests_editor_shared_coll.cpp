// Self-tests for the level editor's shared collision (src/game/front/level_editor_shared_coll.*, not original): the
// distances that decide which collision-only faces go with a piece, faces read from a geometry (float / compressed,
// 10- / 8-byte faces), the companion object built from them and placed with the piece (moved, turned, scaled), and the
// collision tree kept exact when moves go to it in whole sixteenths (level_editor_scenery.cpp's ApplyTrack).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_shared_coll.h"
#include "game/front/level_editor_walls.h"
#include "game/nx/coll_tri.h"
#include "hook/selftest.h"

namespace {

namespace LE = Front::LevelEditor;
namespace Shared = Front::LevelEditor::SharedCollision;
using LE::V3;

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool Near(V3 a, V3 b, float tolerance)
{
    return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

void TestEditorSharedCollDistances()
{
    V3 a = {0, 0, 0}, b = {10, 0, 0}, c = {0, 0, 10};
    bool d = Near(Shared::PointTriangleDistance({2, 5, 2}, a, b, c), 5.0f, 1e-5f) &&          // above the inside
             Near(Shared::PointTriangleDistance({2, -3, 2}, a, b, c), 3.0f, 1e-5f) &&         // below
             Near(Shared::PointTriangleDistance({-4, 0, -3}, a, b, c), 5.0f, 1e-5f) &&        // past corner a
             Near(Shared::PointTriangleDistance({5, 0, -2}, a, b, c), 2.0f, 1e-5f) &&         // past edge ab
             Near(Shared::PointTriangleDistance({6, 0, 6}, a, b, c), std::sqrt(2.0f), 1e-4f); // past the long edge
    Check(d, "editor shared collision: point to triangle (inside, above / below, past a corner and edges)");
    Check(Near(Shared::PointTriangleDistance({5, 3, 4}, a, b, {20, 0, 0}), 5.0f, 1e-5f) &&
              Near(Shared::PointTriangleDistance({1, 1, 1}, a, a, a), std::sqrt(3.0f), 1e-5f),
          "editor shared collision: degenerate triangles (a line, a point) by their edges");

    // A strip of a 10 x 10 quad at y = 0 (positions 16 bytes apart), an index repeated (skipped), one out of range.
    float vertices[4][4] = {{0, 0, 0, 9}, {10, 0, 0, 9}, {0, 0, 10, 9}, {10, 0, 10, 9}};
    const uint16_t indices[] = {0, 1, 2, 3, 3, 7};
    const auto* positions = reinterpret_cast<const uint8_t*>(vertices);
    float above = Shared::StripDistance({9, 4, 9}, positions, 16, 4, indices, 6, 1e30f);
    float beside = Shared::StripDistance({13, 0, 14}, positions, 16, 4, indices, 6, 1e30f);
    float capped = Shared::StripDistance({9, 4, 9}, positions, 16, 4, indices, 6, 2.0f);
    float none = Shared::StripDistance({9, 4, 9}, positions, 16, 3, indices + 3, 3, 7.0f);
    Check(Near(above, 4.0f, 1e-5f) && Near(beside, 5.0f, 1e-5f) && capped == 2.0f && none == 7.0f,
          "editor shared collision: strip distance (both triangles, the best kept, bad triangles skipped)");
    // Two quads 90 units apart in one strip, joined by repeated indices: the join is no triangle.
    float two[8][3] = {{0, 0, 0}, {10, 0, 0}, {0, 0, 10}, {10, 0, 10}, {100, 0, 0}, {110, 0, 0}, {100, 0, 10}, {110, 0, 10}};
    const uint16_t joined[] = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    float between = Shared::StripDistance({55, 0, 5}, reinterpret_cast<const uint8_t*>(two), 12, 8, joined, 10, 1e30f);
    Check(Near(between, 45.0f, 1e-4f), "editor shared collision: a strip's joins (repeated indices) aren't triangles");

    Check(Shared::Belongs(3.5f, 48.0f) && Shared::Belongs(0.0f, 0.5f) && !Shared::Belongs(3.5f, 3.5f) &&
              !Shared::Belongs(3.5f, 1.0f) && !Shared::Belongs(Shared::kNear + 0.01f, 1e30f) &&
              Shared::Belongs(Shared::kNear, 1e30f) && !Shared::Belongs(std::nanf(""), 1e30f),
          "editor shared collision: a face is a piece's within kNear and strictly nearest");
    Check(Shared::Takeable(0x1000) && Shared::Takeable(0x0001) && !Shared::Takeable(0x0010) &&
              !Shared::Takeable(0x0040) && !Shared::Takeable(0x1050),
          "editor shared collision: triggers and non-collidable faces stay");
}
SELFTEST(Early, TestEditorSharedCollDistances);

void TestEditorSharedCollFaces()
{
    // Float vertices, 10-byte faces.
    float vertices[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint8_t faces[10] = {};
    uint16_t flags = 0x1001, terrain = 7, index[3] = {2, 0, 1};
    std::memcpy(faces, &flags, 2);
    std::memcpy(faces + 2, &terrain, 2);
    std::memcpy(faces + 4, index, 6);
    uint8_t bytes[3] = {10, 20, 30};
    Nx::CCollObjTriData g = {};
    g.m_numVertices = 3;
    g.m_numFaces = 1;
    g.m_faces = faces;
    g.m_vertices = vertices;
    g.m_vertexBytes = bytes;
    Shared::FaceCopy f = Shared::ReadFace(&g, 0);
    Check(f.flags == 0x1001 && f.terrain == 7 && Near(f.v[0], {7, 8, 9}, 0) && Near(f.v[1], {1, 2, 3}, 0) &&
              Near(f.v[2], {4, 5, 6}, 0) && f.bytes[0] == 30 && f.bytes[1] == 10 && f.bytes[2] == 20,
          "editor shared collision: a face read (float vertices, 10-byte faces)");

    // Compressed vertices (sixteenths from the box's minimum), 8-byte faces.
    uint16_t packed[6] = {16, 32, 48, 160, 8, 0};
    uint8_t small[16] = {0x05, 0x00, 0x06, 0x00, 0, 0, 0, 0, 0x01, 0x00, 0x02, 0x00, 1, 0, 1, 0};
    Nx::CCollObjTriData c = {};
    c.m_numVertices = 2;
    c.m_numFaces = 2;
    c.m_smallFaces = true;
    c.m_compressed = true;
    c.m_faces = small;
    c.m_vertices = packed;
    c.m_min = {100, -50, 7, 1};
    Shared::FaceCopy s = Shared::ReadFace(&c, 1);
    Check(s.flags == 1 && s.terrain == 2 && Near(s.v[0], {110, -49.5f, 7}, 0) && Near(s.v[1], {101, -48, 10}, 0) &&
              Near(s.v[2], {110, -49.5f, 7}, 0) && s.bytes[0] == 0x80,
          "editor shared collision: a face read (compressed vertices, 8-byte faces, no vertex bytes)");
}
SELFTEST(Early, TestEditorSharedCollFaces);

std::vector<Shared::FaceCopy> TwoFaces()
{
    // Two faces sharing an edge (four corners), a third corner repeated with another byte (kept apart).
    Shared::FaceCopy a = {{{0, 0, 0}, {10, 0, 0}, {0, 5, 0}}, 0x1000, 3, {1, 1, 1}};
    Shared::FaceCopy b = {{{10, 0, 0}, {10, 5, 2}, {0, 5, 0}}, 0x0001, 4, {1, 2, 1}};
    Shared::FaceCopy d = {{{0, 0, 0}, {10, 5, 2}, {-3, 1, 1}}, 0x0004, 5, {9, 2, 9}};
    return {a, b, d};
}

void TestEditorSharedCollCompanion()
{
    Shared::Companion companion(0xBF49DBEE, TwoFaces());
    const Nx::CCollObjTriData& g = companion.Geometry();
    Nx::CCollStatic* object = companion.Object();
    bool built = companion.FaceCount() == 3 && companion.VertexCount() == 6 && g.m_numFaces == 3 &&
                 !g.m_compressed && !g.m_smallFaces && !g.m_bsp && g.RejectionScale() == 0.0f &&
                 g.m_checksum == 0xBF49DBEE && object->m_checksum == 0xBF49DBEE && object->m_geometry == &g &&
                 object->m_buffer && reinterpret_cast<uintptr_t>(object->m_vtable) == 0x00645C18 &&
                 object->m_flags == 0 && companion.Active();
    // The faces as given: corners, flags, terrain, bytes.
    std::vector<Shared::FaceCopy> back = companion.Faces();
    std::vector<Shared::FaceCopy> given = TwoFaces();
    bool same = back.size() == 3;
    for (size_t f = 0; same && f < 3; ++f) {
        same &= back[f].flags == given[f].flags && back[f].terrain == given[f].terrain;
        for (int k = 0; k < 3; ++k)
            same &= Near(back[f].v[k], given[f].v[k], 0) && back[f].bytes[k] == given[f].bytes[k];
    }
    bool box = Near({g.m_min.x, g.m_min.y, g.m_min.z}, {-3, 0, 0}, 0) && Near({g.m_max.x, g.m_max.y, g.m_max.z}, {10, 5, 2}, 0);
    Check(built && same && box, "editor shared collision: companion built (shared corners, float, no tree, its box)");

    // Placed: each vertex where the placement takes its base (as the meshes), the box with them; identity exact.
    LE::Placement p = {3, {100.5f, -20, 7.25f}, 1.75f};
    companion.Place(p);
    bool placed = true;
    for (int i = 0; i < companion.VertexCount(); ++i)
        placed &= Near(companion.Vertex(i), LE::Apply(p, companion.BaseVertex(i)), 1e-4f);
    V3 lo = companion.Vertex(0), hi = lo;
    for (int i = 0; i < companion.VertexCount(); ++i) {
        V3 v = companion.Vertex(i);
        lo = {std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)};
        hi = {std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)};
    }
    placed &= Near({g.m_min.x, g.m_min.y, g.m_min.z}, lo, 0) && Near({g.m_max.x, g.m_max.y, g.m_max.z}, hi, 0);
    Mth::Vector raw;
    g.GetRawVertexPos(1, &raw);
    placed &= Near({raw.x, raw.y, raw.z}, LE::Apply(p, companion.BaseVertex(1)), 1e-4f);
    for (int i = 0; i < 50; ++i)
        companion.Place({i & 3, {i * 3.3f, -i * 0.7f, i * 1.1f}, 0.25f + 0.05f * i});
    companion.Place({0, {0, 0, 0}});
    back = companion.Faces();
    bool home = true;
    for (size_t f = 0; f < 3; ++f)
        for (int k = 0; k < 3; ++k)
            home &= Near(back[f].v[k], given[f].v[k], 0);
    Check(placed && home, "editor shared collision: placed with the piece (moved, turned, scaled); home is exact");

    // Rebase: where it is becomes the base; on / off is the object's flag 4.
    companion.Place({1, {5, 0, 0}});
    V3 now = companion.Vertex(2);
    companion.Rebase();
    companion.Place({0, {0, 0, 0}});
    bool rebased = Near(companion.Vertex(2), now, 0) && Near(companion.BaseVertex(2), now, 0);
    companion.SetActive(false);
    bool off = (object->m_flags & 4) && !companion.Active();
    companion.SetActive(true);
    Check(rebased && off && object->m_flags == 0, "editor shared collision: rebased; switched off and on (flag 4)");

    // At most kMostFaces faces.
    std::vector<Shared::FaceCopy> many(Shared::kMostFaces + 5, TwoFaces()[0]);
    Shared::Companion big(1, many);
    Check(big.FaceCount() == Shared::kMostFaces && big.VertexCount() == 3,
          "editor shared collision: at most kMostFaces faces (the no-tree face list's size)");
}
SELFTEST(Early, TestEditorSharedCollCompanion);

// A piece's own collision moved by whole sixteenths (ApplyTrack): the tree's split comes back exactly however the
// drag went; fractional steps would let it drift (each Translate rounds a split towards zero).
void TestEditorSharedCollSixteenths()
{
    auto drag = [](bool onGrid, int32_t* split) {
        alignas(8) Nx::CCollBSPNode nodes[3] = {};
        nodes[0].m_split = (static_cast<int32_t>(-577.3125 * 16) << 2); // (x at -577.3125)
        nodes[0].m_link = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&nodes[1]));
        nodes[1].m_split = nodes[2].m_split = 3;
        V3 start = {-577.3846f, -738.40497f, -2821.4768f}; // (the rail's node position: not on sixteenths)
        V3 applied = {0, 0, 0};
        for (int i = 1; i <= 120; ++i) {
            float t = static_cast<float>(i) / 120.0f;
            V3 position = start + V3{t * 431.37f, std::sin(t * 9.0f) * 3.3f, -t * 77.71f};
            if (i == 120)
                position = start; // (and back where it was)
            V3 wanted = onGrid ? LE::Walls::OnGrid(position - start) : position - start;
            V3 change = wanted - applied;
            Mth::Vector delta = {change.x, change.y, change.z, 0.0f};
            nodes[0].Translate(&delta);
            applied = wanted;
        }
        *split = nodes[0].m_split;
    };
    int32_t exact = 0, drifted = 0;
    drag(true, &exact);
    drag(false, &drifted);
    Check(exact == (static_cast<int32_t>(-577.3125 * 16) << 2) && drifted != exact,
          "editor shared collision: own collision moved on sixteenths - the tree comes back exactly (fractional steps drift)");
}
SELFTEST(Early, TestEditorSharedCollSixteenths);

} // namespace
