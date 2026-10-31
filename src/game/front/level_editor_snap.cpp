// NOT ORIGINAL - the level editor's snapping (ours; see level_editor_snap.h).

#include "game/front/level_editor_snap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "game/front/level_editor_scenery.h"
#include "game/gfx/mesh.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"

namespace Front::LevelEditor::Snap {

int Nearest(const std::vector<V3>& points, V3 to)
{
    int best = -1;
    float bestD = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        V3 d = points[i] - to;
        float dd = Dot(d, d);
        if (best < 0 || dd < bestD) {
            best = static_cast<int>(i);
            bestD = dd;
        }
    }
    return best;
}

int NearestOnScreen(const std::vector<V3>& points, const std::function<bool(V3, float*, float*)>& project, float x,
                    float y)
{
    int best = -1;
    float bestD = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        float px, py;
        if (!project(points[i], &px, &py))
            continue;
        float dd = (px - x) * (px - x) + (py - y) * (py - y);
        if (best < 0 || dd < bestD) {
            best = static_cast<int>(i);
            bestD = dd;
        }
    }
    return best;
}

bool AlignRows(V3 rows[3], V3 normal)
{
    float n2 = Dot(normal, normal);
    if (n2 < 1e-8f)
        return false;
    V3 up = normal * (1.0f / std::sqrt(n2));
    float lengths[3];
    for (int i = 0; i < 3; ++i)
        lengths[i] = std::sqrt(Dot(rows[i], rows[i]));
    if (lengths[2] < 1e-6f)
        return false;
    // (the at row's heading kept: its part across the surface)
    V3 at = rows[2] * (1.0f / lengths[2]);
    V3 flat = at - up * Dot(at, up);
    if (Dot(flat, flat) < 1e-6f)
        return false;
    at = Normalize(flat);
    V3 right = Cross(up, at); // (right = up x at, as the game's rows are)
    rows[0] = right * lengths[0];
    rows[1] = up * lengths[1];
    rows[2] = at * lengths[2];
    return true;
}

namespace {

// The feeler (as level_editor.cpp's Cast and level_editor_scenery.cpp's CastStatic): constructor 0x00417760, cast
// 0x00417710 (start, end, movables: 0 - static collision only); its normal at +0x24, its point at +0x34, the collision
// object hit at +0x54 and its checksum at +0x58.
bool Cast(V3 from, V3 to, SurfaceHit* hit)
{
    alignas(16) uint8_t feeler[0x100] = {};
    auto* ray = reinterpret_cast<Nx::CFeeler*>(feeler);
    ray->Construct();
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!ray->GetCollisionAlong(&start, &end, false))
        return false;
    const float* normal = reinterpret_cast<const float*>(feeler + 0x24);
    const float* point = reinterpret_cast<const float*>(feeler + 0x34);
    hit->normal = {normal[0], normal[1], normal[2]};
    hit->point = {point[0], point[1], point[2]};
    hit->checksum = *reinterpret_cast<void* const*>(feeler + 0x54) ? *reinterpret_cast<const uint32_t*>(feeler + 0x58)
                                                                   : 0;
    return true;
}

} // namespace

bool Surface(V3 origin, V3 dir, float maxT, const std::vector<uint32_t>& skip, SurfaceHit* hit)
{
    V3 from = origin;
    V3 end = origin + dir * maxT;
    // (the selection's own collision met first: the ray goes on from just past it - a few times at most)
    for (int tries = 0; tries < 8; ++tries) {
        SurfaceHit h;
        if (!Cast(from, end, &h))
            return false;
        if (h.checksum == 0 || std::find(skip.begin(), skip.end(), h.checksum) == skip.end()) {
            *hit = h;
            return true;
        }
        from = h.point + dir * 1.0f;
        if (Dot(end - from, dir) <= 0.0f)
            return false;
    }
    return false;
}

std::vector<V3> SectorVertices(uint32_t checksum, size_t limit)
{
    std::vector<V3> out;
    uint8_t* geom = Scenery::SectorGeometry(checksum);
    if (!geom)
        return out;
    // (the geometry's meshes, +0x3C / +0x40 - as level_editor_scenery.cpp's RayMeshes reads them: each vertex's
    // position its first 12 bytes, where the sector is now)
    auto** meshes = *reinterpret_cast<Nx::sMesh***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    for (uint32_t i = 0; meshes && i < count && i < 4096 && out.size() < limit; ++i) {
        Nx::sMesh* mesh = meshes[i];
        if (!mesh || mesh->billboard || (mesh->flags & 0x10))
            continue;
        Nx::sVertexBuffer* vertices = mesh->vertexBufferCount ? mesh->vertexBuffers[0] : nullptr;
        if (!vertices || !vertices->data || mesh->stride < 12)
            continue;
        for (uint32_t v = 0; v < mesh->vertexCount && out.size() < limit; ++v) {
            const float* p = reinterpret_cast<const float*>(vertices->data + v * mesh->stride);
            out.push_back({p[0], p[1], p[2]});
        }
    }
    return out;
}

} // namespace Front::LevelEditor::Snap
