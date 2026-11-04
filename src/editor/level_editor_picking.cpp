#include "editor/level_editor_picking.h"
#include "editor/level_editor.h"
#include "gpu/vertex_view.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/render_state.h"
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
    std::array<float, 3> e = {{max.x - min.x, max.y - min.y, max.z - min.z}};
    if (core::at(e, 1) <= kLowHeight && std::min(core::at(e, 0), core::at(e, 2)) >= kLowSpan &&
        level_share >= kLevelTopShare) {
        return true;
    }
    std::sort(e.begin(), e.begin() + 3); // e[0] thinnest .. e[2] longest
    if (core::at(e, 1) >= kWideSpan) {
        return true;
    }
    return core::at(e, 1) >= kSlabSpan && core::at(e, 0) <= core::at(e, 1) * kSlabThickness;
}

void add_top_triangle(TopArea* area, V3 a, V3 b, V3 c)
{
    const V3 n = perpendicular(b - a, c - a);
    const float twice = std::sqrt(get_dot(n, n));
    if (!(twice > 0.0F)) {
        return;
    }
    const float up = std::fabs(n.y) / twice;
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
    auto at = [&](uint16_t i) {
        const auto f = gpu::VertexView(positions, stride, vertex_count).float3(i);
        return V3{.x = core::at(f, 0), .y = core::at(f, 1), .z = core::at(f, 2)};
    };
    const std::span<const uint16_t> index_run(indices, index_count);
    for (uint32_t i = 2; i < index_count; ++i) {
        const uint16_t a = core::at(index_run, i - 2);
        const uint16_t b = core::at(index_run, i - 1);
        const uint16_t c = core::at(index_run, i);
        if (a == b || b == c || a == c || a >= vertex_count || b >= vertex_count || c >= vertex_count) {
            continue;
        }
        add_top_triangle(area, at(a), at(b), at(c));
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
    for (const Met& m : met) {
        order.push_back(&m);
    }
    std::ranges::stable_sort(order, [](const Met* a, const Met* b) { return a->t < b->t; });
    Resolved r;
    bool searching = true; // (a plain click's search: until the first occluding entry)
    for (const Met* m : order) {
        if (!m->locked && m->passed && (r.passed == 0U)) {
            r.passed = m->id;
            r.passed_t_ = m->t;
        }
        if (!searching) {
            continue;
        }
        if (!m->locked && !m->passed) {
            // (a pickable piece: taken - it is what occludes the rest, if it does; a see-through pickable one can't be
            // taken without occluding, as no pickable piece is see-through)
            r.take = m->id;
            r.take_t_ = m->t;
            if (m->occludes && !r.blocked) {
                r.blocked = true;
                r.block_t_ = m->t;
            }
            searching = false;
        } else if (m->occludes) {
            r.blocked = true;
            r.block_t_ = m->t;
            searching = false;
        }
    }
    return r;
}

bool gizmo_in_front(float gizmo_depth, bool blocked, float block_depth, float slack)
{
    return !blocked || !(gizmo_depth > block_depth + slack); // (NaN: in front - as before)
}

float gizmo_slack(float depth, float units_per_pixel, float pixels)
{
    return kGizmoSlack + (std::max(0.0F, depth) * units_per_pixel * pixels);
}

Choice get_choose(const Candidates& c, uint32_t filter, bool everything)
{
    if (c.node >= 0 && ((filter & kNodes) != 0U)) {
        return Choice::kNode;
    }
    Choice best = Choice::kNone;
    float best_t = 0.0F;
    bool any = false;
    // (`allowed` false: the kind is filtered out - it still stands in the way of what's behind it)
    auto consider = [&](bool have, bool allowed, float t, Choice choice, bool or_equal) {
        if (have && (!any || t < best_t || (or_equal && t == best_t))) {
            best = allowed ? choice : Choice::kNone;
            best_t = t;
            any = true;
        }
    };
    if (everything) {
        consider(c.object && ((filter & kObjects) != 0U), true, c.object_t_, Choice::kObject, false);
        consider(c.scenery && ((filter & kScenery) != 0U), true, c.scenery_t_, Choice::kScenery, true);
        consider(c.passed && ((filter & kScenery) != 0U), true, c.passed_t_, Choice::kPassed, true);
        return best;
    }
    consider(c.blocked, false, c.block_t_, Choice::kNone, false);
    consider(c.object, (filter & kObjects) != 0, c.object_t_, Choice::kObject, false);
    consider(c.scenery, (filter & kScenery) != 0, c.scenery_t_, Choice::kScenery, true);
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
        for (const Layered& o : objects) {
            order.push_back({.kind = Choice::kObject, .id = o.id, .t = o.t});
        }
    }
    if ((filter & kScenery) != 0U) {
        for (const Layered& s : scenery) {
            if (!s.passed) {
                order.push_back({.kind = Choice::kScenery, .id = s.id, .t = s.t});
            }
        }
        for (const Layered& s : scenery) {
            if (s.passed) {
                order.push_back({.kind = Choice::kPassed, .id = s.id, .t = s.t});
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

bool ray_triangle(Ray ray, V3 a, V3 b, V3 c, float* t)
{
    const auto origin = ray.origin;
    const auto dir = ray.dir;
    // (Moller and Trumbore's test)
    const V3 e1 = b - a;
    const V3 e2 = c - a;
    const V3 p = perpendicular(dir, e2);
    const float det = get_dot(e1, p);
    if (std::fabs(det) < kParallelEpsilon) {
        return false;
    }
    const float inv = 1.0F / det;
    const V3 s = origin - a;
    const float u = get_dot(s, p) * inv;
    if (u < 0.0F || u > 1.0F) {
        return false;
    }
    const V3 q = perpendicular(s, e1);
    const float v = get_dot(dir, q) * inv;
    if (v < 0.0F || u + v > 1.0F) {
        return false;
    }
    const float hit = get_dot(e2, q) * inv;
    if (!(hit > 0.0F)) {
        return false;
    }
    *t = hit;
    return true;
}

bool ray_strip(V3 origin, V3 dir, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
               const uint16_t* indices, StripLimit strip_limit, float* t)
{
    const auto index_count = strip_limit.index_count_;
    const auto max_t = strip_limit.max_t_;
    bool found = false;
    float best = max_t;
    auto at = [&](uint16_t i) {
        const auto f = gpu::VertexView(positions, stride, vertex_count).float3(i);
        return V3{.x = core::at(f, 0), .y = core::at(f, 1), .z = core::at(f, 2)};
    };
    const std::span<const uint16_t> index_run(indices, index_count);
    for (uint32_t i = 2; i < index_count; ++i) {
        const uint16_t a = core::at(index_run, i - 2);
        const uint16_t b = core::at(index_run, i - 1);
        const uint16_t c = core::at(index_run, i);
        if (a == b || b == c || a == c || a >= vertex_count || b >= vertex_count || c >= vertex_count) {
            continue;
        }
        float hit = 0.0F;
        if (ray_triangle({.origin = origin, .dir = dir}, at(a), at(b), at(c), &hit) && hit < best) {
            best = hit;
            found = true;
        }
    }
    if (found) {
        *t = best;
    }
    return found;
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
    for (const Lock& lock : stored_locks()) {
        if (lock.kind == kind && lock.id == id) {
            return true;
        }
    }
    return false;
}

bool set_locked(uint32_t kind, uint32_t id, bool locked)
{
    auto it = std::ranges::find_if(stored_locks(), [&](const Lock& l) { return l.kind == kind && l.id == id; });
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
