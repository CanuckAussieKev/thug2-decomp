#include "editor/level_editor_picking.h"
#include "editor/level_editor.h"
#include "engine/gfx/vertex_view.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/mesh_formats.h"
#include <span>

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// NOT ORIGINAL - the level editor's picking rules (ours; level_editor_picking.h).
namespace frontend::level_editor::picking {

namespace {

constexpr int kPassSeeThroughFlag = 0x40; // a pass drawn see-through regardless of its blend
constexpr float kParallelEpsilon = 1e-12F; // a triangle edge-on to the ray (determinant near zero)

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

bool is_structure(V3 min, V3 max, V3 camera, float level_share)
{
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) { // (also NaN: not a box - leave it to the rest)
        return false;
    }
    // Inside it: a click from in here would always meet it (the room's walls, floor and ceiling).
    if (camera.x >= min.x - kInsideMargin && camera.x <= max.x + kInsideMargin && camera.y >= min.y - kInsideMargin &&
        camera.y <= max.y + kInsideMargin && camera.z >= min.z - kInsideMargin && camera.z <= max.z + kInsideMargin) {
        return true;
    }
    std::array<float, 3> extent = {{max.x - min.x, max.y - min.y, max.z - min.z}};
    if (core::at(extent, 1) <= kLowHeight && std::min(core::at(extent, 0), core::at(extent, 2)) >= kLowSpan &&
        level_share >= kLevelTopShare) {
        return true;
    }
    std::sort(extent.begin(), extent.begin() + 3); // e[0] thinnest .. e[2] longest
    if (core::at(extent, 1) >= kWideSpan) {
        return true;
    }
    return core::at(extent, 1) >= kSlabSpan && core::at(extent, 0) <= core::at(extent, 1) * kSlabThickness;
}

void add_top_triangle(TopArea* area, V3 a, V3 b, V3 corner_c)
{
    const V3 normal = perpendicular(b - a, corner_c - a);
    const float twice = std::sqrt(get_dot(normal, normal));
    if (!(twice > 0.0F)) {
        return;
    }
    const float up = std::fabs(normal.y) / twice;
    if (up <= kUpNormalY) {
        return;
    }
    area->up += twice * kHalf;
    if (up >= kLevelNormalY) {
        area->level += twice * kHalf;
    }
}

void add_top_strip(TopArea* area, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
                   const uint16_t* indices, uint32_t index_count)
{
    auto at = [&](uint16_t index) {
        const auto floats = gpu::VertexView(positions, stride, vertex_count).float3(index);
        return V3{.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
    };
    const std::span<const uint16_t> index_run(indices, index_count);
    for (uint32_t i = 2; i < index_count; ++i) {
        const uint16_t index_a = core::at(index_run, i - 2);
        const uint16_t index_b = core::at(index_run, i - 1);
        const uint16_t index_c = core::at(index_run, i);
        if (index_a == index_b || index_b == index_c || index_a == index_c || index_a >= vertex_count ||
            index_b >= vertex_count || index_c >= vertex_count) {
            continue;
        }
        add_top_triangle(area, at(index_a), at(index_b), at(index_c));
    }
}

float get_level_share(const TopArea& area) { return area.up > 0.0F ? area.level / area.up : 1.0F; }

bool material_see_through(PassBlend pass_blend)
{
    const auto pass_flags = pass_blend.pass_flags_;
    const auto blend_mode = pass_blend.blend_mode_;
    if ((pass_flags & kPassSeeThroughFlag) != 0U) {
        return true;
    }
    switch (blend_mode & render::kBlendModeMask) {
    case 1:
    case 2: // add, fixadd
    case 3:
    case 4: // subtract, fixsubtract
    case render::kBlendModeModulate:
    case render::kBlendModeFixModulate: // modulate, fixmodulate
    case render::kBlendModeBrighten:
    case render::kBlendModeFixBrighten: // brighten, fixbrighten
        return true;
    default:
        return false; // 0 diffuse, 5 blend, 6 fixblend (and anything else)
    }
}

Resolved settle_up(const std::vector<Met>& met)
{
    std::vector<const Met*> order;
    order.reserve(met.size());
    for (const Met& met_entry : met) {
        order.push_back(&met_entry);
    }
    std::ranges::stable_sort(order, [](const Met* a, const Met* b) { return a->t < b->t; });
    Resolved resolved;
    bool searching = true; // (a plain click's search: until the first occluding entry)
    for (const Met* met_entry : order) {
        if (!met_entry->locked && met_entry->passed && (resolved.passed == 0U)) {
            resolved.passed = met_entry->id;
            resolved.passed_t_ = met_entry->t;
        }
        if (!searching) {
            continue;
        }
        if (!met_entry->locked && !met_entry->passed) {
            // (a pickable piece: taken - it is what occludes the rest, if it does; a see-through pickable one can't be
            // taken without occluding, as no pickable piece is see-through)
            resolved.take = met_entry->id;
            resolved.take_t_ = met_entry->t;
            if (met_entry->occludes && !resolved.blocked) {
                resolved.blocked = true;
                resolved.block_t_ = met_entry->t;
            }
            searching = false;
        } else if (met_entry->occludes) {
            resolved.blocked = true;
            resolved.block_t_ = met_entry->t;
            searching = false;
        }
    }
    return resolved;
}

bool gizmo_in_front(float gizmo_depth, bool blocked, float block_depth, float slack)
{
    return !blocked || !(gizmo_depth > block_depth + slack); // (NaN: in front - as before)
}

float gizmo_slack(float depth, float units_per_pixel, float pixels)
{
    return kGizmoSlack + (std::max(0.0F, depth) * units_per_pixel * pixels);
}

Choice get_choose(const Candidates& candidates, uint32_t filter, bool everything)
{
    if (candidates.node >= 0 && ((filter & kNodes) != 0U)) {
        return Choice::kNode;
    }
    Choice best = Choice::kNone;
    float best_t = 0.0F;
    bool any = false;
    // (`allowed` false: the kind is filtered out - it still stands in the way of what's behind it)
    auto consider = [&](bool have, bool allowed, float fraction, Choice choice, bool or_equal) {
        if (have && (!any || fraction < best_t || (or_equal && fraction == best_t))) {
            best = allowed ? choice : Choice::kNone;
            best_t = fraction;
            any = true;
        }
    };
    if (everything) {
        consider(candidates.object && ((filter & kObjects) != 0U), true, candidates.object_t_, Choice::kObject, false);
        consider(candidates.scenery && ((filter & kScenery) != 0U), true, candidates.scenery_t_, Choice::kScenery,
                 true);
        consider(candidates.passed && ((filter & kScenery) != 0U), true, candidates.passed_t_, Choice::kPassed, true);
        return best;
    }
    consider(candidates.blocked, false, candidates.block_t_, Choice::kNone, false);
    consider(candidates.object, (filter & kObjects) != 0, candidates.object_t_, Choice::kObject, false);
    consider(candidates.scenery, (filter & kScenery) != 0, candidates.scenery_t_, Choice::kScenery, true);
    return best;
}

std::vector<TableRow> cycle_order(int node, const std::vector<Layered>& objects, std::span<const Layered> scenery,
                                  uint32_t filter)
{
    std::vector<TableRow> order;
    if (node >= 0 && ((filter & kNodes) != 0U)) {
        order.push_back({.kind = Choice::kNode, .id = static_cast<uint32_t>(node), .t = 0.0F});
    }
    if ((filter & kObjects) != 0U) {
        for (const Layered& object : objects) {
            order.push_back({.kind = Choice::kObject, .id = object.id, .t = object.t});
        }
    }
    if ((filter & kScenery) != 0U) {
        for (const Layered& layered : scenery) {
            if (!layered.passed) {
                order.push_back({.kind = Choice::kScenery, .id = layered.id, .t = layered.t});
            }
        }
        for (const Layered& layered : scenery) {
            if (layered.passed) {
                order.push_back({.kind = Choice::kPassed, .id = layered.id, .t = layered.t});
            }
        }
    }
    return order;
}

int get_index_of(const std::vector<TableRow>& order, Choice kind, uint32_t id)
{
    // (a passed piece taken by Alt is the same piece as in the passed part)
    for (size_t i = 0; i < order.size(); ++i) {
        if (core::at(order, i).id == id &&
            (core::at(order, i).kind == kind ||
             ((kind == Choice::kScenery || kind == Choice::kPassed) &&
              (core::at(order, i).kind == Choice::kScenery || core::at(order, i).kind == Choice::kPassed)))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool same_spot(float x0, float y0, float x1, float y1, float tolerance)
{
    return std::fabs(x0 - x1) <= tolerance && std::fabs(y0 - y1) <= tolerance;
}

bool ray_triangle(Ray ray, V3 a, V3 b, V3 corner_c, float* hit_distance)
{
    const auto origin = ray.origin;
    const auto dir = ray.dir;
    // (Moller and Trumbore's test)
    const V3 e1 = b - a;
    const V3 e2 = corner_c - a;
    const V3 dir_cross_e2 = perpendicular(dir, e2);
    const float det = get_dot(e1, dir_cross_e2);
    if (std::fabs(det) < kParallelEpsilon) {
        return false;
    }
    const float inv = 1.0F / det;
    const V3 to_origin = origin - a;
    const float u = get_dot(to_origin, dir_cross_e2) * inv;
    if (u < 0.0F || u > 1.0F) {
        return false;
    }
    const V3 origin_cross_e1 = perpendicular(to_origin, e1);
    const float v = get_dot(dir, origin_cross_e1) * inv;
    if (v < 0.0F || u + v > 1.0F) {
        return false;
    }
    const float hit = get_dot(e2, origin_cross_e1) * inv;
    if (!(hit > 0.0F)) {
        return false;
    }
    *hit_distance = hit;
    return true;
}

bool ray_strip(V3 origin, V3 dir, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
               const uint16_t* indices, StripLimit strip_limit, float* hit_distance)
{
    const auto index_count = strip_limit.index_count_;
    const auto max_t = strip_limit.max_t_;
    bool found = false;
    float best = max_t;
    auto at = [&](uint16_t index) {
        const auto floats = gpu::VertexView(positions, stride, vertex_count).float3(index);
        return V3{.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
    };
    const std::span<const uint16_t> index_run(indices, index_count);
    for (uint32_t i = 2; i < index_count; ++i) {
        const uint16_t index_a = core::at(index_run, i - 2);
        const uint16_t index_b = core::at(index_run, i - 1);
        const uint16_t index_c = core::at(index_run, i);
        if (index_a == index_b || index_b == index_c || index_a == index_c || index_a >= vertex_count ||
            index_b >= vertex_count || index_c >= vertex_count) {
            continue;
        }
        float hit = 0.0F;
        if (ray_triangle({.origin = origin, .dir = dir}, at(index_a), at(index_b), at(index_c), &hit) && hit < best) {
            best = hit;
            found = true;
        }
    }
    if (found) {
        *hit_distance = best;
    }
    return found;
}

float triangle_distance_sq(V3 point, V3 a, V3 b, V3 corner_c)
{
    // (the nearest point by the triangle's regions - its corners, edges and face - with barycentric weights)
    const V3 ab = b - a;
    const V3 ac = corner_c - a;
    const V3 ap = point - a;
    auto distance_sq = [&](V3 nearest) {
        const V3 apart = point - nearest;
        return get_dot(apart, apart);
    };
    const float d1 = get_dot(ab, ap);
    const float d2 = get_dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return distance_sq(a);
    }
    const V3 bp = point - b;
    const float d3 = get_dot(ab, bp);
    const float d4 = get_dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return distance_sq(b);
    }
    const float vc = (d1 * d4) - (d3 * d2);
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return distance_sq(a + (ab * (d1 / (d1 - d3))));
    }
    const V3 cp = point - corner_c;
    const float d5 = get_dot(ab, cp);
    const float d6 = get_dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return distance_sq(corner_c);
    }
    const float vb = (d5 * d2) - (d1 * d6);
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return distance_sq(a + (ac * (d2 / (d2 - d6))));
    }
    const float va = (d3 * d6) - (d5 * d4);
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        return distance_sq(b + ((corner_c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))));
    }
    const float sum = va + vb + vc;
    if (sum <= 0.0F) {
        return distance_sq(a); // (a degenerate triangle)
    }
    return distance_sq(a + (ab * (vb / sum)) + (ac * (vc / sum)));
}

bool strip_near(V3 point, float reach, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
                const uint16_t* indices, uint32_t index_count)
{
    auto at = [&](uint16_t index) {
        const auto floats = gpu::VertexView(positions, stride, vertex_count).float3(index);
        return V3{.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
    };
    const float reach_sq = reach * reach;
    const std::span<const uint16_t> index_run(indices, index_count);
    for (uint32_t i = 2; i < index_count; ++i) {
        const uint16_t index_a = core::at(index_run, i - 2);
        const uint16_t index_b = core::at(index_run, i - 1);
        const uint16_t index_c = core::at(index_run, i);
        if (index_a == index_b || index_b == index_c || index_a == index_c || index_a >= vertex_count ||
            index_b >= vertex_count || index_c >= vertex_count) {
            continue;
        }
        const V3 one = at(index_a);
        const V3 two = at(index_b);
        const V3 three = at(index_c);
        // (the triangle's box first: most are far away)
        if (point.x < std::min({one.x, two.x, three.x}) - reach || point.x > std::max({one.x, two.x, three.x}) + reach ||
            point.y < std::min({one.y, two.y, three.y}) - reach || point.y > std::max({one.y, two.y, three.y}) + reach ||
            point.z < std::min({one.z, two.z, three.z}) - reach || point.z > std::max({one.z, two.z, three.z}) + reach) {
            continue;
        }
        if (triangle_distance_sq(point, one, two, three) <= reach_sq) {
            return true;
        }
    }
    return false;
}

namespace {
std::vector<Lock>& stored_locks()
{
    static core::NoDestroy<std::vector<Lock>> locks;
    return locks.get();
}
}

bool latched(uint32_t kind, uint32_t id)
{
    return std::ranges::any_of(stored_locks(), [&](const Lock& lock) { return lock.kind == kind && lock.id == id; });
}

bool set_locked(uint32_t kind, uint32_t id, bool locked)
{
    auto it =
        std::ranges::find_if(stored_locks(), [&](const Lock& lock) { return lock.kind == kind && lock.id == id; });
    if (locked == (it != stored_locks().end())) {
        return false;
    }
    if (locked) {
        stored_locks().push_back({.kind = kind, .id = id});
    } else {
        stored_locks().erase(it);
    }
    return true;
}

const std::vector<Lock>& get_locks() { return stored_locks(); }
void clear_locks() { stored_locks().clear(); }
void set_locks(const std::vector<Lock>& locks) { stored_locks() = locks; }

} // namespace frontend::level_editor::picking
