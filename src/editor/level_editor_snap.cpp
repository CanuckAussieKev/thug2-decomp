// NOT ORIGINAL - the level editor's snapping (ours; see level_editor_snap.h).

#include "editor/level_editor_snap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_scenery.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/gfx/geom.h"
#include "gpu/vertex_buffers.h"
#include "gpu/vertex_view.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"

namespace frontend::level_editor::snap {

namespace {

constexpr float kDegenerateLengthSquared = 1e-8f; // a squared length this small is no direction
constexpr float kDegenerateEdge = 1e-6f; // a third edge this short makes a degenerate triangle
constexpr float kDegenerateAreaSquared = 1e-6f; // a flat vector this small has no direction
constexpr int kMaxSelfHits = 8; // how often the ray restarts past the selection's own collision

} // namespace

int get_nearest(const std::vector<V3>& points, V3 to)
{
    int best = -1;
    float best_d = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const V3 d = core::at(points, i) - to;
        const float dd = get_dot(d, d);
        if (best < 0 || dd < best_d) {
            best = static_cast<int>(i);
            best_d = dd;
        }
    }
    return best;
}

int nearest_on_screen(const std::vector<V3>& points, const std::function<bool(V3, float*, float*)>& project, float x,
                      float y)
{
    int best = -1;
    float best_d = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        float px = 0.0f;
        float py = 0.0f;
        if (!project(core::at(points, i), &px, &py))
            continue;
        const float dd = (px - x) * (px - x) + (py - y) * (py - y);
        if (best < 0 || dd < best_d) {
            best = static_cast<int>(i);
            best_d = dd;
        }
    }
    return best;
}

bool align_rows(std::span<V3, 3> rows, V3 normal)
{
    const float n2 = get_dot(normal, normal);
    if (n2 < kDegenerateLengthSquared)
        return false;
    const V3 up = normal * (1.0f / std::sqrt(n2));
    std::array<float, 3> lengths{};
    for (int i = 0; i < 3; ++i)
        core::at(lengths, i) = std::sqrt(get_dot(core::at(rows, i), core::at(rows, i)));
    if (core::at(lengths, 2) < kDegenerateEdge)
        return false;
    // (the at row's heading kept: its part across the surface)
    V3 at = core::at(rows, 2) * (1.0f / core::at(lengths, 2));
    const V3 flat = at - up * get_dot(at, up);
    if (get_dot(flat, flat) < kDegenerateAreaSquared)
        return false;
    at = make_unit(flat);
    const V3 right = perpendicular(up, at); // (right = up x at, as the game's rows are)
    core::at(rows, 0) = right * core::at(lengths, 0);
    core::at(rows, 1) = up * core::at(lengths, 1);
    core::at(rows, 2) = at * core::at(lengths, 2);
    return true;
}

namespace {

// The feeler (as level_editor.cpp's Cast and level_editor_scenery.cpp's cast_static): constructor 0x00417760, cast
// 0x00417710 (start, end, movables: 0 - static collision only); its normal at +0x24, its point at +0x34, the collision
// object hit at +0x54 and its checksum at +0x58.
bool get_cast(V3 from, V3 to, SurfaceHit* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0f};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const auto& normal = ray->hit().normal;
    const auto& point = ray->hit().point;
    hit->normal = {.x = normal[0], .y = normal[1], .z = normal[2]};
    hit->point = {.x = point[0], .y = point[1], .z = point[2]};
    hit->checksum = ray->hit().object ? ray->hit().object_checksum_ : 0;
    return true;
}

} // namespace

bool get_surface(V3 origin, V3 dir, float max_t, const std::vector<uint32_t>& skip, SurfaceHit* hit)
{
    V3 from = origin;
    const V3 end = origin + dir * max_t;
    // (the selection's own collision met first: the ray goes on from just past it - a few times at most)
    for (int tries = 0; tries < kMaxSelfHits; ++tries) {
        SurfaceHit h;
        if (!get_cast(from, end, &h))
            return false;
        if (h.checksum == 0 || std::ranges::find(skip, h.checksum) == skip.end()) {
            *hit = h;
            return true;
        }
        from = h.point + dir * 1.0f;
        if (get_dot(end - from, dir) <= 0.0f)
            return false;
    }
    return false;
}

std::vector<V3> sector_vertices(checksums::Name sector, size_t limit)
{
    const uint32_t checksum = sector.value();
    std::vector<V3> out;
    const render::PcGeom* geom = scenery::sector_geometry(checksum);
    if (!geom)
        return out;
    // (the geometry's meshes, +0x3C / +0x40 - as level_editor_scenery.cpp's ray_meshes reads them: each vertex's
    // position its first 12 bytes, where the sector is now)
    const std::span<render::MeshRef*> meshes = geom->meshes() ? geom->mesh_run() : std::span<render::MeshRef*>();
    const uint32_t count = geom->mesh_count();
    for (uint32_t i = 0; !meshes.empty() && i < count && i < kMeshScanLimit && out.size() < limit; ++i) {
        const render::MeshRef* mesh = core::at(meshes, i);
        if (!mesh || mesh->billboard() || (mesh->flags() & kMeshHiddenFlag))
            continue;
        const render::VertexBuffer* vertices = mesh->vertex_buffer_count() ? core::at(mesh->vertex_buffers(), 0) : nullptr;
        if (!vertices || !vertices->data() || mesh->stride() < kPositionBytes)
            continue;
        for (uint32_t v = 0; v < mesh->vertex_count() && out.size() < limit; ++v) {
            const auto p = gpu::VertexView(vertices->data(), mesh->stride(), mesh->vertex_count()).float3(v);
            out.push_back({.x = core::at(p, 0), .y = core::at(p, 1), .z = core::at(p, 2)});
        }
    }
    return out;
}

} // namespace frontend::level_editor::snap
