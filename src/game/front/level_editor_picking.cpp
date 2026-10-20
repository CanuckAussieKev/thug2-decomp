#include "game/front/level_editor_picking.h"

#include <algorithm>
#include <cmath>

// NOT ORIGINAL - the level editor's picking rules (ours; level_editor_picking.h).
namespace Front::LevelEditor::Picking {

bool IsStructure(V3 min, V3 max, V3 camera)
{
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN: not a box - leave it to the rest)
        return false;
    // Inside it: a click from in here would always meet it (the room's walls, floor and ceiling).
    if (camera.x >= min.x - kInsideMargin && camera.x <= max.x + kInsideMargin && camera.y >= min.y - kInsideMargin &&
        camera.y <= max.y + kInsideMargin && camera.z >= min.z - kInsideMargin && camera.z <= max.z + kInsideMargin)
        return true;
    float e[3] = {max.x - min.x, max.y - min.y, max.z - min.z};
    std::sort(e, e + 3); // e[0] thinnest .. e[2] longest
    if (e[1] >= kWideSpan)
        return true;
    return e[1] >= kSlabSpan && e[0] <= e[1] * kSlabThickness;
}

Choice Choose(const Candidates& c, uint32_t filter, bool everything)
{
    if (c.node >= 0 && (filter & kNodes))
        return Choice::kNode;
    Choice best = Choice::kNone;
    float bestT = 0.0f;
    auto consider = [&](bool have, float t, Choice choice, bool orEqual) {
        if (have && (best == Choice::kNone || t < bestT || (orEqual && t == bestT))) {
            best = choice;
            bestT = t;
        }
    };
    consider(c.object && (filter & kObjects), c.objectT, Choice::kObject, false);
    consider(c.scenery && (filter & kScenery), c.sceneryT, Choice::kScenery, true);
    consider(everything && c.passed && (filter & kScenery), c.passedT, Choice::kPassed, true);
    return best;
}

std::vector<Entry> CycleOrder(int node, const std::vector<Layered>& objects, const std::vector<Layered>& scenery,
                              uint32_t filter)
{
    std::vector<Entry> order;
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

int IndexOf(const std::vector<Entry>& order, Choice kind, uint32_t id)
{
    // (a passed piece taken by Alt is the same piece as in the passed part)
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i].id == id && (order[i].kind == kind ||
                                  ((kind == Choice::kScenery || kind == Choice::kPassed) &&
                                   (order[i].kind == Choice::kScenery || order[i].kind == Choice::kPassed))))
            return static_cast<int>(i);
    return -1;
}

bool SameSpot(float x0, float y0, float x1, float y1, float tolerance)
{
    return std::fabs(x0 - x1) <= tolerance && std::fabs(y0 - y1) <= tolerance;
}

bool RayTriangle(V3 origin, V3 dir, V3 a, V3 b, V3 c, float* t)
{
    // (Moller and Trumbore's test)
    V3 e1 = b - a, e2 = c - a;
    V3 p = Cross(dir, e2);
    float det = Dot(e1, p);
    if (std::fabs(det) < 1e-12f)
        return false;
    float inv = 1.0f / det;
    V3 s = origin - a;
    float u = Dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f)
        return false;
    V3 q = Cross(s, e1);
    float v = Dot(dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    float hit = Dot(e2, q) * inv;
    if (!(hit > 0.0f))
        return false;
    *t = hit;
    return true;
}

bool RayStrip(V3 origin, V3 dir, const uint8_t* positions, uint32_t stride, uint32_t vertexCount,
              const uint16_t* indices, uint32_t indexCount, float maxT, float* t)
{
    bool found = false;
    float best = maxT;
    auto at = [&](uint16_t i) {
        const float* f = reinterpret_cast<const float*>(positions + static_cast<size_t>(i) * stride);
        return V3{f[0], f[1], f[2]};
    };
    for (uint32_t i = 2; i < indexCount; ++i) {
        uint16_t a = indices[i - 2], b = indices[i - 1], c = indices[i];
        if (a == b || b == c || a == c || a >= vertexCount || b >= vertexCount || c >= vertexCount)
            continue;
        float hit;
        if (RayTriangle(origin, dir, at(a), at(b), at(c), &hit) && hit < best) {
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

bool Locked(uint32_t kind, uint32_t id)
{
    for (const Lock& lock : g_locks)
        if (lock.kind == kind && lock.id == id)
            return true;
    return false;
}

bool SetLocked(uint32_t kind, uint32_t id, bool locked)
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

const std::vector<Lock>& Locks() { return g_locks; }
void ClearLocks() { g_locks.clear(); }
void SetLocks(const std::vector<Lock>& locks) { g_locks = locks; }

} // namespace Front::LevelEditor::Picking
