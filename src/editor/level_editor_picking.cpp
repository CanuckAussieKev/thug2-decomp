#include "editor/level_editor_picking.h"

#include <algorithm>
#include <cmath>

// NOT ORIGINAL - the level editor's picking rules (ours; level_editor_picking.h).
namespace frontend::level_editor::picking {

bool is_structure(V3 min, V3 max, V3 camera, float level_share)
{
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN: not a box - leave it to the rest)
        return false;
    // Inside it: a click from in here would always meet it (the room's walls, floor and ceiling).
    if (camera.x >= min.x - kInsideMargin && camera.x <= max.x + kInsideMargin && camera.y >= min.y - kInsideMargin &&
        camera.y <= max.y + kInsideMargin && camera.z >= min.z - kInsideMargin && camera.z <= max.z + kInsideMargin)
        return true;
    float e[3] = {max.x - min.x, max.y - min.y, max.z - min.z};
    if (e[1] <= kLowHeight && std::min(e[0], e[2]) >= kLowSpan && level_share >= kLevelTopShare)
        return true;
    std::sort(e, e + 3); // e[0] thinnest .. e[2] longest
    if (e[1] >= kWideSpan)
        return true;
    return e[1] >= kSlabSpan && e[0] <= e[1] * kSlabThickness;
}

void add_top_triangle(TopArea* area, V3 a, V3 b, V3 c)
{
    V3 n = perpendicular(b - a, c - a);
    float twice = std::sqrt(get_dot(n, n));
    if (!(twice > 0.0f))
        return;
    float up = std::fabs(n.y) / twice;
    if (up <= kUpNormalY)
        return;
    area->up += twice * 0.5f;
    if (up >= kLevelNormalY)
        area->level += twice * 0.5f;
}

void add_top_strip(TopArea* area, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
                   const uint16_t* indices, uint32_t index_count)
{
    auto at = [&](uint16_t i) {
        const float* f = reinterpret_cast<const float*>(positions + static_cast<size_t>(i) * stride);
        return V3{f[0], f[1], f[2]};
    };
    for (uint32_t i = 2; i < index_count; ++i) {
        uint16_t a = indices[i - 2], b = indices[i - 1], c = indices[i];
        if (a == b || b == c || a == c || a >= vertex_count || b >= vertex_count || c >= vertex_count)
            continue;
        add_top_triangle(area, at(a), at(b), at(c));
    }
}

float get_level_share(const TopArea& area) { return area.up > 0.0f ? area.level / area.up : 1.0f; }

bool material_see_through(uint32_t pass_flags, uint32_t blend_mode)
{
    if (pass_flags & 0x40)
        return true;
    switch (blend_mode & 0xFFFFFF) {
    case 1:
    case 2: // add, fixadd
    case 3:
    case 4: // subtract, fixsubtract
    case 7:
    case 8: // modulate, fixmodulate
    case 9:
    case 10: // brighten, fixbrighten
        return true;
    default:
        return false; // 0 diffuse, 5 blend, 6 fixblend (and anything else)
    }
}

Resolved settle_up(const std::vector<Met>& met)
{
    std::vector<const Met*> order;
    for (const Met& m : met)
        order.push_back(&m);
    std::stable_sort(order.begin(), order.end(), [](const Met* a, const Met* b) { return a->t < b->t; });
    Resolved r;
    bool searching = true; // (a plain click's search: until the first occluding entry)
    for (const Met* m : order) {
        if (!m->locked && m->passed && !r.passed) {
            r.passed = m->id;
            r.passed_t_ = m->t;
        }
        if (!searching)
            continue;
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
    return kGizmoSlack + std::max(0.0f, depth) * units_per_pixel * pixels;
}

Choice get_choose(const Candidates& c, uint32_t filter, bool everything)
{
    if (c.node >= 0 && (filter & kNodes))
        return Choice::kNode;
    Choice best = Choice::kNone;
    float best_t = 0.0f;
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
        consider(c.object && (filter & kObjects), true, c.object_t_, Choice::kObject, false);
        consider(c.scenery && (filter & kScenery), true, c.scenery_t_, Choice::kScenery, true);
        consider(c.passed && (filter & kScenery), true, c.passed_t_, Choice::kPassed, true);
        return best;
    }
    consider(c.blocked, false, c.block_t_, Choice::kNone, false);
    consider(c.object, (filter & kObjects) != 0, c.object_t_, Choice::kObject, false);
    consider(c.scenery, (filter & kScenery) != 0, c.scenery_t_, Choice::kScenery, true);
    return best;
}

std::vector<TableRow> cycle_order(int node, const std::vector<Layered>& objects, const std::vector<Layered>& scenery,
                                  uint32_t filter)
{
    std::vector<TableRow> order;
    if (node >= 0 && (filter & kNodes))
        order.push_back({Choice::kNode, static_cast<uint32_t>(node), 0.0f});
    if (filter & kObjects)
        for (const Layered& o : objects)
            order.push_back({Choice::kObject, o.id, o.t});
    if (filter & kScenery) {
        for (const Layered& s : scenery)
            if (!s.passed)
                order.push_back({Choice::kScenery, s.id, s.t});
        for (const Layered& s : scenery)
            if (s.passed)
                order.push_back({Choice::kPassed, s.id, s.t});
    }
    return order;
}

int get_index_of(const std::vector<TableRow>& order, Choice kind, uint32_t id)
{
    // (a passed piece taken by Alt is the same piece as in the passed part)
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i].id == id &&
            (order[i].kind == kind || ((kind == Choice::kScenery || kind == Choice::kPassed) &&
                                       (order[i].kind == Choice::kScenery || order[i].kind == Choice::kPassed))))
            return static_cast<int>(i);
    return -1;
}

bool same_spot(float x0, float y0, float x1, float y1, float tolerance)
{
    return std::fabs(x0 - x1) <= tolerance && std::fabs(y0 - y1) <= tolerance;
}

bool ray_triangle(V3 origin, V3 dir, V3 a, V3 b, V3 c, float* t)
{
    // (Moller and Trumbore's test)
    V3 e1 = b - a, e2 = c - a;
    V3 p = perpendicular(dir, e2);
    float det = get_dot(e1, p);
    if (std::fabs(det) < 1e-12f)
        return false;
    float inv = 1.0f / det;
    V3 s = origin - a;
    float u = get_dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f)
        return false;
    V3 q = perpendicular(s, e1);
    float v = get_dot(dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    float hit = get_dot(e2, q) * inv;
    if (!(hit > 0.0f))
        return false;
    *t = hit;
    return true;
}

bool ray_strip(V3 origin, V3 dir, const uint8_t* positions, uint32_t stride, uint32_t vertex_count,
               const uint16_t* indices, uint32_t index_count, float max_t, float* t)
{
    bool found = false;
    float best = max_t;
    auto at = [&](uint16_t i) {
        const float* f = reinterpret_cast<const float*>(positions + static_cast<size_t>(i) * stride);
        return V3{f[0], f[1], f[2]};
    };
    for (uint32_t i = 2; i < index_count; ++i) {
        uint16_t a = indices[i - 2], b = indices[i - 1], c = indices[i];
        if (a == b || b == c || a == c || a >= vertex_count || b >= vertex_count || c >= vertex_count)
            continue;
        float hit;
        if (ray_triangle(origin, dir, at(a), at(b), at(c), &hit) && hit < best) {
            best = hit;
            found = true;
        }
    }
    if (found)
        *t = best;
    return found;
}

namespace {
std::vector<Lock> g_locks;
}

bool latched(uint32_t kind, uint32_t id)
{
    for (const Lock& lock : g_locks)
        if (lock.kind == kind && lock.id == id)
            return true;
    return false;
}

bool set_locked(uint32_t kind, uint32_t id, bool locked)
{
    auto it = std::find_if(g_locks.begin(), g_locks.end(), [&](const Lock& l) { return l.kind == kind && l.id == id; });
    if (locked == (it != g_locks.end()))
        return false;
    if (locked)
        g_locks.push_back({kind, id});
    else
        g_locks.erase(it);
    return true;
}

const std::vector<Lock>& get_locks() { return g_locks; }
void clear_locks() { g_locks.clear(); }
void set_locks(const std::vector<Lock>& locks) { g_locks = locks; }

} // namespace frontend::level_editor::picking
