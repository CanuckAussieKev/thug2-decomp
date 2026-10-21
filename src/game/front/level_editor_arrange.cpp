// NOT ORIGINAL - the level editor's arrange tools (ours; see level_editor_arrange.h).

#include "game/front/level_editor_arrange.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>

#include "game/core/crc.h"
#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_adjust.h"
#include "game/front/level_editor_carry.h"
#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_links.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_snap.h"
#include "game/front/level_editor_ui.h"
#include "game/gfx/mesh.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Arrange {

// --- the pure parts ---------------------------------------------------------------------------------------------

Span SpanAlong(const std::vector<V3>& points, V3 axis)
{
    Span span;
    for (size_t i = 0; i < points.size(); ++i) {
        float d = Dot(points[i], axis);
        if (i == 0 || d < span.lo)
            span.lo = d;
        if (i == 0 || d > span.hi)
            span.hi = d;
    }
    return span;
}

namespace {

float EdgeOf(const Span& span, Edge edge)
{
    return edge == Edge::Min ? span.lo : edge == Edge::Max ? span.hi : (span.lo + span.hi) * 0.5f;
}

float Component(V3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
float& ComponentRef(V3& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

} // namespace

std::vector<float> AlignMoves(const std::vector<Span>& spans, Edge edge, Reference reference)
{
    std::vector<float> moves(spans.size(), 0.0f);
    if (spans.empty())
        return moves;
    Span bounds = spans.front();
    if (reference == Reference::Bounds) {
        for (const Span& s : spans) {
            bounds.lo = std::min(bounds.lo, s.lo);
            bounds.hi = std::max(bounds.hi, s.hi);
        }
    }
    float target = EdgeOf(bounds, edge);
    for (size_t i = 0; i < spans.size(); ++i)
        moves[i] = target - EdgeOf(spans[i], edge);
    return moves;
}

std::vector<float> DistributeMoves(const std::vector<Span>& spans, bool gaps)
{
    std::vector<float> moves(spans.size(), 0.0f);
    size_t n = spans.size();
    if (n < 3)
        return moves;
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return EdgeOf(spans[a], Edge::Centre) < EdgeOf(spans[b], Edge::Centre);
    });
    const Span& first = spans[order.front()];
    const Span& last = spans[order.back()];
    if (!gaps) {
        float c0 = EdgeOf(first, Edge::Centre), c1 = EdgeOf(last, Edge::Centre);
        for (size_t k = 1; k + 1 < n; ++k) {
            float target = c0 + (c1 - c0) * static_cast<float>(k) / static_cast<float>(n - 1);
            moves[order[k]] = target - EdgeOf(spans[order[k]], Edge::Centre);
        }
        return moves;
    }
    float sizes = 0.0f;
    for (const Span& s : spans)
        sizes += s.hi - s.lo;
    float gap = (last.hi - first.lo - sizes) / static_cast<float>(n - 1);
    float at = first.hi + gap;
    for (size_t k = 1; k + 1 < n; ++k) {
        const Span& s = spans[order[k]];
        moves[order[k]] = at - s.lo;
        at += (s.hi - s.lo) + gap;
    }
    return moves;
}

V3 EdgeSnap(const Aabb& moving, V3 delta, const std::vector<Aabb>& others, float reach, int axes,
            std::vector<Contact>* contacts)
{
    V3 out = delta;
    Aabb m = {moving.min + delta, moving.max + delta};
    constexpr float kEps = 0.5f; // (boxes that only touch don't overlap)
    auto overlaps = [&](const Aabb& o, int axis, float slack) {
        return Component(m.min, axis) < Component(o.max, axis) + slack - kEps &&
               Component(m.max, axis) > Component(o.min, axis) - slack + kEps;
    };
    for (int axis = 0; axis < 3; ++axis) {
        if (!(axes & (1 << axis)))
            continue;
        float best = reach;
        float correction = 0.0f, at = 0.0f;
        int found = -1;
        auto consider = [&](float c, float face, int index) {
            if (std::fabs(c) <= best) {
                best = std::fabs(c);
                correction = c;
                at = face;
                found = index;
            }
        };
        for (size_t i = 0; i < others.size(); ++i) {
            const Aabb& o = others[i];
            int index = static_cast<int>(i);
            if (axis != 1) {
                // (side by side: at the same height, across within reach)
                int across = axis == 0 ? 2 : 0;
                if (!overlaps(o, 1, 0.0f) || !overlaps(o, across, reach))
                    continue;
                consider(Component(o.max, axis) - Component(m.min, axis), Component(o.max, axis), index);
                consider(Component(o.min, axis) - Component(m.max, axis), Component(o.min, axis), index);
            } else {
                // (stacked on it: over it; level with its top: beside it, within reach)
                if (overlaps(o, 0, 0.0f) && overlaps(o, 2, 0.0f))
                    consider(o.max.y - m.min.y, o.max.y, index);
                if (overlaps(o, 0, reach) && overlaps(o, 2, reach))
                    consider(o.max.y - m.max.y, o.max.y, index);
            }
        }
        if (found >= 0) {
            ComponentRef(out, axis) += correction;
            if (contacts)
                contacts->push_back({axis, at, found});
        }
    }
    return out;
}

float SnapReach(float distance) { return std::clamp(distance * 0.01f, 6.0f, 24.0f); }

float BoxSegmentDistance(const Aabb& box, V3 a, V3 b, V3* nearest)
{
    auto at = [&](float t) { return a + (b - a) * t; };
    auto distance = [&](float t) {
        V3 p = at(t);
        V3 q = {std::clamp(p.x, box.min.x, box.max.x), std::clamp(p.y, box.min.y, box.max.y),
                std::clamp(p.z, box.min.z, box.max.z)};
        V3 d = p - q;
        return std::sqrt(Dot(d, d));
    };
    // (the distance from a convex box along a line is convex: a ternary search finds its least)
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 60; ++i) {
        float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
        if (distance(m1) <= distance(m2))
            hi = m2;
        else
            lo = m1;
    }
    float t = (lo + hi) * 0.5f;
    if (nearest)
        *nearest = at(t);
    return distance(t);
}

std::vector<std::string> ReadoutLines(const Readout& r)
{
    std::vector<std::string> lines;
    char text[96];
    std::snprintf(text, sizeof(text), "size %.0f x %.0f x %.0f", r.size.x, r.size.y, r.size.z);
    lines.push_back(text);
    if (r.moved >= 0.5f) {
        std::snprintf(text, sizeof(text), "moved %.0f", r.moved);
        lines.push_back(text);
    }
    if (r.haveGround) {
        std::snprintf(text, sizeof(text), "above ground %.0f", r.above);
        lines.push_back(text);
    }
    if (r.haveRail) {
        std::snprintf(text, sizeof(text), "%s %.0f", r.ledge ? "ledge" : "rail", r.rail);
        lines.push_back(text);
    }
    return lines;
}

std::vector<Run> TopEdges(const std::vector<V3>& triangles, float weld, float minLength)
{
    std::vector<Run> runs;
    if (weld <= 0.0f)
        weld = 0.5f;
    // Points within `weld` one (on a grid of that size).
    std::map<std::tuple<long, long, long>, int> ids;
    std::vector<V3> points;
    auto id = [&](V3 p) {
        auto key = std::make_tuple(std::lround(p.x / weld), std::lround(p.y / weld), std::lround(p.z / weld));
        auto it = ids.find(key);
        if (it != ids.end())
            return it->second;
        int index = static_cast<int>(points.size());
        points.push_back(p);
        ids.emplace(key, index);
        return index;
    };
    struct EdgeUse {
        int top = 0, down = 0, up = 0, level = 0;
    };
    std::map<std::pair<int, int>, EdgeUse> edges;
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        int v[3] = {id(triangles[t]), id(triangles[t + 1]), id(triangles[t + 2])};
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
            continue;
        V3 n = Cross(points[v[1]] - points[v[0]], points[v[2]] - points[v[0]]);
        float length = std::sqrt(Dot(n, n));
        if (length < 1e-6f)
            continue;
        bool top = std::fabs(n.y / length) >= kTopNormalY;
        for (int e = 0; e < 3; ++e) {
            int a = v[e], b = v[(e + 1) % 3], c = v[(e + 2) % 3];
            EdgeUse& use = edges[{std::min(a, b), std::max(a, b)}];
            if (top) {
                ++use.top;
                continue;
            }
            float dy = points[c].y - (points[a].y + points[b].y) * 0.5f;
            if (dy < -1.0f)
                ++use.down;
            else if (dy > 1.0f)
                ++use.up;
            else
                ++use.level;
        }
    }
    // The lips: one upward triangle, and the surface falls away beyond it (or nothing does - an open edge).
    std::map<int, std::vector<int>> at; // (a point's lip edges)
    std::vector<std::pair<int, int>> lips;
    for (const auto& entry : edges) {
        const EdgeUse& use = entry.second;
        if (use.top != 1 || use.up > 0 || (use.down == 0 && use.level > 0))
            continue;
        int index = static_cast<int>(lips.size());
        lips.push_back(entry.first);
        at[entry.first.first].push_back(index);
        at[entry.first.second].push_back(index);
    }
    auto direction = [&](int from, int to) { return Normalize(points[to] - points[from]); };
    auto joins = [&](int prev, int mid, int next) {
        float c = Dot(direction(prev, mid), direction(mid, next));
        return c >= std::cos(kJoinDegrees * 3.14159265f / 180.0f);
    };
    std::vector<bool> used(lips.size(), false);
    // (the next edge from `point` along the run that came from `prev`: the point's only other lip edge, turning little)
    auto next = [&](int prev, int point, int* other) {
        const std::vector<int>& list = at[point];
        if (list.size() != 2)
            return -1;
        for (int e : list) {
            if (used[e])
                continue;
            int o = lips[e].first == point ? lips[e].second : lips[e].first;
            if (!joins(prev, point, o))
                return -1;
            *other = o;
            return e;
        }
        return -1;
    };
    for (size_t start = 0; start < lips.size(); ++start) {
        if (used[start])
            continue;
        used[start] = true;
        std::vector<int> chain = {lips[start].first, lips[start].second};
        // forward from the second point, then backward from the first
        for (int pass = 0; pass < 2; ++pass) {
            for (;;) {
                int n = static_cast<int>(chain.size());
                int prev = chain[n - 2], point = chain[n - 1], other = -1;
                int e = next(prev, point, &other);
                if (e < 0)
                    break;
                used[e] = true;
                chain.push_back(other);
                if (other == chain.front())
                    break; // (a closed loop)
            }
            if (pass == 0)
                std::reverse(chain.begin(), chain.end());
        }
        // Straight stretches as one segment (a point kept where the run turns more than 2 degrees).
        Run run;
        run.points.push_back(points[chain.front()]);
        for (size_t i = 1; i + 1 < chain.size(); ++i) {
            V3 a = Normalize(points[chain[i]] - run.points.back());
            V3 b = Normalize(points[chain[i + 1]] - points[chain[i]]);
            if (Dot(a, b) < std::cos(2.0f * 3.14159265f / 180.0f))
                run.points.push_back(points[chain[i]]);
        }
        run.points.push_back(points[chain.back()]);
        for (size_t i = 1; i < run.points.size(); ++i) {
            V3 d = run.points[i] - run.points[i - 1];
            run.length += std::sqrt(Dot(d, d));
        }
        if (run.length >= minLength)
            runs.push_back(std::move(run));
    }
    // (the longest first: they're the ones wanted most)
    std::stable_sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) { return a.length > b.length; });
    return runs;
}

int RunAt(const std::vector<Run>& runs, const std::function<bool(V3, float*, float*)>& project, float x, float y,
          float grab)
{
    int best = -1;
    float bestD = grab;
    for (size_t r = 0; r < runs.size(); ++r) {
        for (size_t i = 1; i < runs[r].points.size(); ++i) {
            float ax, ay, bx, by;
            if (!project(runs[r].points[i - 1], &ax, &ay) || !project(runs[r].points[i], &bx, &by))
                continue;
            float d = DistanceToSegment(x, y, ax, ay, bx, by);
            if (d <= bestD) {
                bestD = d;
                best = static_cast<int>(r);
            }
        }
    }
    return best;
}

Settings& Current()
{
    static Settings settings;
    return settings;
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

const V3 kWorld[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

void Corners(const OrientedBox& box, std::vector<V3>* out)
{
    V3 c[8];
    BoxCorners(box.origin, box.axes, box.min, box.max, c);
    out->insert(out->end(), c, c + 8);
}

// A piece the tools move as one: an object, or a scenery piece with the shadows cast by it.
struct Unit {
    Obj::CCompositeObject* object = nullptr;
    std::vector<uint32_t> sectors;
    std::vector<V3> corners;
};

std::vector<Unit> UnitsOf(const Edit::Selected& selection)
{
    std::vector<Unit> units;
    OrientedBox box;
    if (selection.kind == Edit::Kind::Objects) {
        for (uint32_t id : selection.ids) {
            Obj::CCompositeObject* object = LiveObject(id);
            if (!object)
                continue;
            Unit unit;
            unit.object = object;
            if (Objects::ModelBox(object, &box))
                Corners(box, &unit.corners);
            else
                unit.corners.push_back(Objects::PosOf(object));
            units.push_back(std::move(unit));
        }
        return units;
    }
    if (selection.kind != Edit::Kind::Scenery)
        return units;
    std::vector<uint32_t> shadows;
    for (uint32_t s : selection.ids) {
        uint32_t caster = 0;
        if (Scenery::IsShadow(s, &caster) &&
            std::find(selection.ids.begin(), selection.ids.end(), caster) != selection.ids.end() && caster != s) {
            shadows.push_back(s);
            continue;
        }
        Unit unit;
        unit.sectors.push_back(s);
        units.push_back(std::move(unit));
    }
    for (uint32_t s : shadows) {
        uint32_t caster = 0;
        Scenery::IsShadow(s, &caster);
        for (Unit& unit : units) {
            if (unit.sectors.front() == caster)
                unit.sectors.push_back(s);
        }
    }
    for (Unit& unit : units) {
        for (uint32_t s : unit.sectors) {
            if (Scenery::Box(s, &box))
                Corners(box, &unit.corners);
        }
    }
    units.erase(std::remove_if(units.begin(), units.end(), [](const Unit& u) { return u.corners.empty(); }),
                units.end());
    return units;
}

// The axes the tools go along: the world's, or (Local) the first piece's own - an object's rows, a sector's quarter
// turns.
void AxesOf(const std::vector<Unit>& units, bool local, V3 out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = kWorld[i];
    if (!local || units.empty())
        return;
    if (Obj::CCompositeObject* object = units.front().object) {
        for (int i = 0; i < 3; ++i)
            out[i] = Normalize({object->m_matrix.row[i].x, object->m_matrix.row[i].y, object->m_matrix.row[i].z});
        return;
    }
    Placement placement = {0, {0.0f, 0.0f, 0.0f}};
    Scenery::PlacementNow(units.front().sectors.front(), &placement);
    for (int i = 0; i < 3; ++i)
        out[i] = QuarterTurn(kWorld[i], placement.quarters);
}

// Each unit moved by its own amount along `axis` (one undo step).
void MoveUnits(const Edit::Selected& selection, const std::vector<Unit>& units, V3 axis,
               const std::vector<float>& moves)
{
    Edit::Begin();
    if (selection.kind == Edit::Kind::Scenery) {
        Carry::Disarm(); // (each piece moves by its own amount: nothing is carried along)
        for (size_t i = 0; i < units.size(); ++i) {
            if (std::fabs(moves[i]) < 1e-4f)
                continue;
            Scenery::SetSelection(units[i].sectors);
            Scenery::MoveTo(Scenery::Pivot() + axis * moves[i]);
        }
        Scenery::SetSelection(selection.ids);
    } else {
        for (size_t i = 0; i < units.size(); ++i) {
            if (units[i].object && std::fabs(moves[i]) >= 1e-4f)
                Objects::Place(units[i].object, Objects::PosOf(units[i].object) + axis * moves[i]);
        }
    }
    Edit::End();
}

const char* AxisName(int axis) { return axis == 0 ? "X" : axis == 1 ? "Y" : "Z"; }

} // namespace

bool Align(int axis, Edge edge, Reference reference, bool local)
{
    Edit::Selected selection = Edit::Selection();
    std::vector<Unit> units = UnitsOf(selection);
    if (units.size() < 2) {
        Hud::Message("Align: select two or more objects or scenery pieces (Shift+click; Alt+click one piece)", 4.0);
        return false;
    }
    axis = std::clamp(axis, 0, 2);
    V3 axes[3];
    AxesOf(units, local, axes);
    std::vector<Span> spans;
    for (const Unit& unit : units)
        spans.push_back(SpanAlong(unit.corners, axes[axis]));
    std::vector<float> moves = AlignMoves(spans, edge, reference);
    MoveUnits(selection, units, axes[axis], moves);
    char text[128];
    std::snprintf(text, sizeof(text), "Aligned %d pieces: %s on %s (%s, %s) - F6 adjusts, Ctrl+Z undoes",
                  static_cast<int>(units.size()), edge == Edge::Min ? "min" : edge == Edge::Max ? "max" : "centre",
                  AxisName(axis), local ? "local" : "world", reference == Reference::First ? "the first" : "the bounds");
    Hud::Message(text, 4.0);
    Adjust::Action action;
    action.kind = Adjust::Kind::Align;
    action.axis = axis;
    action.edge = static_cast<int>(edge);
    action.reference = static_cast<int>(reference);
    action.local = local;
    Adjust::Note(action);
    return true;
}

bool Distribute(int axis, bool gaps, bool local)
{
    Edit::Selected selection = Edit::Selection();
    std::vector<Unit> units = UnitsOf(selection);
    if (units.size() < 3) {
        Hud::Message("Distribute: select three or more objects or scenery pieces", 4.0);
        return false;
    }
    axis = std::clamp(axis, 0, 2);
    V3 axes[3];
    AxesOf(units, local, axes);
    std::vector<Span> spans;
    for (const Unit& unit : units)
        spans.push_back(SpanAlong(unit.corners, axes[axis]));
    MoveUnits(selection, units, axes[axis], DistributeMoves(spans, gaps));
    char text[128];
    std::snprintf(text, sizeof(text), "Distributed %d pieces along %s (%s) - F6 adjusts, Ctrl+Z undoes",
                  static_cast<int>(units.size()), AxisName(axis), gaps ? "equal gaps" : "evenly spaced centres");
    Hud::Message(text, 4.0);
    Adjust::Action action;
    action.kind = Adjust::Kind::Distribute;
    action.axis = axis;
    action.gaps = gaps;
    action.local = local;
    Adjust::Note(action);
    return true;
}

// --- Replace selection with ---

namespace {

// The axis-aligned bounds of sectors (false: none has a box).
bool SectorsBounds(const std::vector<uint32_t>& sectors, Aabb* out)
{
    bool have = false;
    OrientedBox box;
    for (uint32_t s : sectors) {
        V3 a, b;
        if (!Scenery::Box(s, &box))
            continue;
        BoundsOf(box, &a, &b);
        GrowBox(&out->min, &out->max, have, a, b);
        have = true;
    }
    return have;
}

V3 BottomCentre(const Aabb& box) { return {(box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f}; }

uint32_t KeyModel() { return Crc::GenerateCRCFromString("model"); }

// A node's model= (empty: none).
std::string ModelOf(uint32_t name)
{
    int index = Nodes::Find(name);
    Script::CStruct* s = index >= 0 ? Nodes::StructOf(Nodes::All()[index]) : nullptr;
    const char* model = nullptr;
    if (!s || !s->GetString(KeyModel(), &model, 0) || !model)
        return {};
    return model;
}

bool ReplaceScenery(const Edit::Selected& selection, uint32_t source)
{
    if (std::find(selection.ids.begin(), selection.ids.end(), source) != selection.ids.end()) {
        Hud::Message("Replace: the Add palette's piece is one of those selected", 4.0);
        return false;
    }
    std::vector<Unit> units = UnitsOf(selection);
    if (units.empty())
        return false;
    Placement sourceAt = {0, {0.0f, 0.0f, 0.0f}};
    Scenery::PlacementNow(source, &sourceAt);
    size_t depth = Edit::Depth();
    // (the pieces hidden first: undone last, they come back selected)
    Edit::PushSnapshot();
    Scenery::HideSelection();
    std::vector<uint32_t> made;
    for (const Unit& unit : units) {
        Aabb was;
        if (!SectorsBounds(unit.sectors, &was))
            continue;
        Placement placement = {0, {0.0f, 0.0f, 0.0f}};
        Scenery::PlacementNow(unit.sectors.front(), &placement);
        uint32_t copy = Scenery::Duplicate(source);
        if (!copy)
            continue;
        Scenery::SetSelection({copy});
        // (its turns and scale as the piece's own: the copy is the source as it is now)
        int quarters = ((placement.quarters - sourceAt.quarters) % 4 + 4) % 4;
        if (quarters)
            Scenery::Turn(quarters);
        V3 ratio = {placement.scale.x / sourceAt.scale.x, placement.scale.y / sourceAt.scale.y,
                    placement.scale.z / sourceAt.scale.z};
        if (std::fabs(ratio.x - 1.0f) > 1e-4f || std::fabs(ratio.y - 1.0f) > 1e-4f || std::fabs(ratio.z - 1.0f) > 1e-4f)
            Scenery::ScaleSelection(TurnedScale(ratio, placement.quarters), IsUniformScale(ratio));
        Aabb now;
        if (SectorsBounds({copy}, &now))
            Scenery::MoveTo(Scenery::Pivot() + (BottomCentre(was) - BottomCentre(now)));
        Scenery::Commit();
        made.push_back(copy);
    }
    if (made.empty()) {
        Edit::TakeBack();
        Hud::Message("Replace: that piece couldn't be copied", 4.0);
        return false;
    }
    Edit::PushMade({}, made);
    Edit::Fold(depth);
    Edit::Select({Edit::Kind::Scenery, made});
    Hud::Message("Replaced " + std::to_string(made.size()) + " piece(s) with " + Objects::Label(source) +
                     " - Ctrl+Z puts them back",
                 4.0);
    Log("level editor: replaced %u scenery piece(s) with %s", static_cast<unsigned>(made.size()),
        Objects::Label(source).c_str());
    return true;
}

bool ReplaceObjects(const Edit::Selected& selection, uint32_t source)
{
    std::string model = ModelOf(source);
    std::vector<Objects::State> copies;  // (object copies: replaced by copies of the source where they were)
    std::vector<uint32_t> copyIds, swapped;
    for (uint32_t id : selection.ids) {
        Obj::CCompositeObject* object = LiveObject(id);
        if (!object || id == source)
            continue;
        if (Objects::IsCopy(id)) {
            copies.push_back(Objects::Save(object));
            copyIds.push_back(id);
        } else {
            swapped.push_back(id);
        }
    }
    if (copies.empty() && (swapped.empty() || model.empty())) {
        Hud::Message(model.empty() ? "Replace: a level's own object can't be removed, and that object has no model= to "
                                     "give it (copies of objects can be replaced)"
                                   : "Replace: nothing selected to replace",
                     5.0);
        return false;
    }
    size_t depth = Edit::Depth();
    std::vector<uint32_t> made;
    if (!copies.empty()) {
        Edit::RemoveCopies(copyIds);
        for (const Objects::State& was : copies) {
            V3 pos = {was.pos.x, was.pos.y, was.pos.z};
            Obj::CCompositeObject* copy = Objects::MakeCopy(source, pos);
            if (!copy)
                continue;
            Objects::State state = was;
            state.id = copy->m_id;
            Objects::Restore(state);
            made.push_back(copy->m_id);
        }
        if (!made.empty())
            Edit::PushMade(made, {});
    }
    // (a level's own object: the source's model given to it - the Inspector's model swap)
    int swaps = 0;
    for (uint32_t id : model.empty() ? std::vector<uint32_t>{} : swapped) {
        std::string now = ModelOf(id);
        if (now.empty() || _stricmp(now.c_str(), model.c_str()) == 0)
            continue;
        Entities::Queue([id, now] { Entities::Detail::WatchModel(id, now); });
        if (Edit::SetField(id, KeyModel(), static_cast<int>(Fields::Kind::String), model))
            ++swaps;
    }
    Edit::Fold(depth);
    if (!made.empty())
        Edit::Select({Edit::Kind::Objects, made});
    char text[160];
    std::snprintf(text, sizeof(text), "Replaced %d copies; %d level objects given its model - Ctrl+Z puts them back",
                  static_cast<int>(made.size()), swaps);
    Hud::Message(text, 5.0);
    Log("level editor: replace with %s: %d copies made again, %d model swaps", Objects::Label(source).c_str(),
        static_cast<int>(made.size()), swaps);
    return !made.empty() || swaps > 0;
}

} // namespace

bool Replace()
{
    int tab = 0;
    uint32_t source = 0;
    if (!Edit::BrowserItem(&tab, &source)) {
        Hud::Message("Replace: open the Add palette (B) and highlight the piece or object to put in their place", 5.0);
        return false;
    }
    Edit::Selected selection = Edit::Selection();
    if (selection.kind == Edit::Kind::Scenery && tab == 0)
        return ReplaceScenery(selection, source);
    if (selection.kind == Edit::Kind::Objects && tab == 1)
        return ReplaceObjects(selection, source);
    Hud::Message(selection.kind == Edit::Kind::Scenery   ? "Replace: scenery is replaced by a scenery piece (the Add "
                                                           "palette's Scenery tab)"
                 : selection.kind == Edit::Kind::Objects ? "Replace: objects are replaced by an object (the Objects tab)"
                                                         : "Replace: select scenery pieces or objects first",
                 5.0);
    return false;
}

// --- edge-to-edge snapping ---

namespace {

struct SnapCache {
    std::vector<uint32_t> selection;
    uint32_t lastUsed = 0;
    std::vector<Aabb> boxes;
    std::vector<Contact> contacts;
    Aabb moving;
    uint32_t contactsAt = 0;
};
SnapCache g_snap;

struct GatherContext {
    const std::vector<uint32_t>* skip;
    Aabb near;
    std::vector<Aabb>* out;
};

void GatherSector(void* context, uint32_t checksum, uint8_t*)
{
    auto* c = static_cast<GatherContext*>(context);
    if (std::find(c->skip->begin(), c->skip->end(), checksum) != c->skip->end() || Scenery::Hidden(checksum))
        return;
    OrientedBox box;
    if (!Scenery::Box(checksum, &box))
        return;
    Aabb a;
    BoundsOf(box, &a.min, &a.max);
    // (not the ground or a whole street; only near the piece)
    if (!SectorSizeMovable(a.min, a.max, 2500.0f))
        return;
    if (a.max.x < c->near.min.x || a.min.x > c->near.max.x || a.max.y < c->near.min.y || a.min.y > c->near.max.y ||
        a.max.z < c->near.min.z || a.min.z > c->near.max.z)
        return;
    c->out->push_back(a);
}

} // namespace

V3 EdgeSnapped(V3 pos, V3 wanted, int axes)
{
    if (!Current().edgeSnap)
        return wanted;
    Edit::Selected selection = Edit::Selection();
    if (selection.kind != Edit::Kind::Scenery)
        return wanted;
    Aabb moving;
    if (!SectorsBounds(selection.ids, &moving))
        return wanted;
    uint32_t now = SDL_GetTicks();
    if (g_snap.selection != selection.ids || now - g_snap.lastUsed > 300) {
        // (the neighbours, once a drag: within 1500 of where the piece is - they don't move meanwhile)
        g_snap.selection = selection.ids;
        g_snap.boxes.clear();
        GatherContext context = {&selection.ids, {moving.min - V3{1500, 1500, 1500}, moving.max + V3{1500, 1500, 1500}},
                                 &g_snap.boxes};
        Scenery::ForEachLevelSector(GatherSector, &context);
    }
    g_snap.lastUsed = now;
    float distance = 0.0f;
    ViewNow view;
    if (CurrentView(&view)) {
        V3 d = pos - view.pos;
        distance = std::sqrt(Dot(d, d));
    }
    g_snap.contacts.clear();
    V3 delta = EdgeSnap(moving, wanted - pos, g_snap.boxes, SnapReach(distance), axes, &g_snap.contacts);
    g_snap.moving = {moving.min + delta, moving.max + delta};
    g_snap.contactsAt = now;
    return pos + delta;
}

// --- auto-rail along an edge ---

namespace {

bool g_edgePicking;
struct EdgeCache {
    std::vector<uint32_t> selection;
    V3 pivot = {};
    std::vector<Run> runs;
};
EdgeCache g_edges;
int g_edgeHover = -1;

// A sector's drawn triangles where it is now (its meshes' first index buffers as strips - as picking reads them).
void SectorTriangles(uint32_t checksum, std::vector<V3>* out)
{
    uint8_t* geom = Scenery::SectorGeometry(checksum);
    if (!geom)
        return;
    auto** meshes = *reinterpret_cast<Nx::sMesh***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    for (uint32_t i = 0; meshes && i < count && i < 4096 && out->size() < 600000; ++i) {
        Nx::sMesh* mesh = meshes[i];
        if (!mesh || mesh->billboard || (mesh->flags & 0x10))
            continue;
        Nx::sVertexBuffer* vertices = mesh->vertexBufferCount ? mesh->vertexBuffers[0] : nullptr;
        Nx::sIndexBuffer* indices = mesh->indexBuffers[0];
        if (!vertices || !vertices->data || !indices || !indices->indices || mesh->stride < 12)
            continue;
        auto at = [&](uint16_t k) {
            const float* f = reinterpret_cast<const float*>(vertices->data + static_cast<size_t>(k) * mesh->stride);
            return V3{f[0], f[1], f[2]};
        };
        const uint16_t* index = indices->indices;
        for (uint32_t k = 2; k < mesh->indexCounts[0]; ++k) {
            uint16_t a = index[k - 2], b = index[k - 1], c = index[k];
            if (a == b || b == c || a == c || a >= mesh->vertexCount || b >= mesh->vertexCount || c >= mesh->vertexCount)
                continue;
            out->push_back(at(a));
            out->push_back(at(b));
            out->push_back(at(c));
        }
    }
}

const std::vector<Run>& EdgeRuns()
{
    Edit::Selected selection = Edit::Selection();
    V3 pivot = selection.kind == Edit::Kind::Scenery ? Scenery::Pivot() : V3{};
    if (selection.ids != g_edges.selection || pivot.x != g_edges.pivot.x || pivot.y != g_edges.pivot.y ||
        pivot.z != g_edges.pivot.z) {
        g_edges.selection = selection.ids;
        g_edges.pivot = pivot;
        g_edges.runs.clear();
        if (selection.kind == Edit::Kind::Scenery) {
            std::vector<V3> triangles;
            for (uint32_t s : selection.ids) {
                if (!Scenery::IsShadow(s))
                    SectorTriangles(s, &triangles);
            }
            g_edges.runs = TopEdges(triangles, 0.5f, 16.0f);
            if (g_edges.runs.size() > 256)
                g_edges.runs.resize(256);
        }
    }
    return g_edges.runs;
}

bool PixelOf(const ViewNow& view, V3 p, float* x, float* y)
{
    Frame frame = FrameFromYawPitch(view.yaw, view.pitch);
    float nx, ny;
    if (!Project(view.pos, frame, view.lens, p, view.nearZ, &nx, &ny))
        return false;
    NdcToPixel(view.viewport, nx, ny, x, y);
    return true;
}

} // namespace

bool EdgePicking() { return g_edgePicking; }

void SetEdgePicking(bool on)
{
    if (on && Edit::Selection().kind != Edit::Kind::Scenery) {
        Hud::Message("Rail along an edge: select a scenery piece first (its top edges are offered)", 4.0);
        on = false;
    }
    g_edgePicking = on;
    g_edgeHover = -1;
    if (on) {
        size_t runs = EdgeRuns().size();
        Hud::Message(runs ? "Rail along an edge: click one of its top edges (cyan) - Shift+click lays more; F7 / Esc stops"
                          : "Rail along an edge: no top edges found on that piece",
                     5.0);
        if (!runs)
            g_edgePicking = false;
    }
}

bool EdgeClick(float x, float y)
{
    if (!g_edgePicking)
        return false;
    ViewNow view;
    if (!CurrentView(&view))
        return false;
    const std::vector<Run>& runs = EdgeRuns();
    int pick = RunAt(runs, [&](V3 p, float* px, float* py) { return PixelOf(view, p, px, py); }, x, y,
                     12.0f * static_cast<float>(view.viewport[3]) / 720.0f);
    if (pick < 0) {
        Hud::Message("Rail along an edge: click on one of the cyan edges (F7 / Esc stops)");
        return true;
    }
    Run run = runs[pick];
    Chains::State before = Chains::Current();
    std::string message;
    uint32_t select = 0;
    Edit::Selected selection = Edit::Selection();
    uint32_t cluster = selection.ids.empty() ? 0 : selection.ids.front();
    if (!Chains::AddRail(run.points, cluster, &message, &select)) {
        Hud::Message(message.empty() ? std::string("Rail along an edge: couldn't add the rail") : message, 4.0);
        return true;
    }
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    bool more = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    char text[128];
    std::snprintf(text, sizeof(text), "Rail laid along the edge: %d points, %.0f long - Ctrl+Z takes it away",
                  static_cast<int>(run.points.size()), run.length);
    // (Shift: the piece stays selected for the next edge; else the rail's first point is selected)
    Edit::ChainChanged(&before, more ? 0 : select, text);
    if (more)
        Edit::Select(selection);
    else
        g_edgePicking = false;
    Log("level editor: auto-rail along an edge of %s: %d points, %.0f long", Objects::Label(cluster).c_str(),
        static_cast<int>(run.points.size()), run.length);
    return true;
}

// --- the keys, the overlay ---

namespace {

// The rails' and ledges' segments where skaters grind them (cached while the nodes and rails stay).
struct Segment {
    V3 a, b;
    uint32_t from, to;
    bool ledge;
};
std::vector<Segment> g_segments;
uint32_t g_segmentsNodes = ~0u, g_segmentsRails = ~0u;

const std::vector<Segment>& Segments()
{
    if (g_segmentsNodes == Nodes::Generation() && g_segmentsRails == Scenery::RailGeneration())
        return g_segments;
    g_segmentsNodes = Nodes::Generation();
    g_segmentsRails = Scenery::RailGeneration();
    g_segments.clear();
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    auto place = [&](const Nodes::Node& n, V3* p) {
        if (Scenery::RailPoint(n.name, p, true))
            return true;
        *p = n.pos;
        return n.hasPos;
    };
    for (const Nodes::Node& n : nodes) {
        bool ledge = n.family == Nodes::Family::Ladder && Chains::IsLedgeNode(n.name);
        if (n.hidden || (n.family != Nodes::Family::Rail && !ledge))
            continue;
        V3 a;
        if (!place(n, &a))
            continue;
        for (int link : n.links) {
            if (link < 0 || link >= static_cast<int>(nodes.size()))
                continue;
            V3 b;
            if (place(nodes[link], &b))
                g_segments.push_back({a, b, n.name, nodes[link].name, ledge});
        }
    }
    return g_segments;
}

// The selection's axis-aligned box and its size (an object's own: its model box's).
bool SelectionBox(const Edit::Selected& selection, Aabb* box, V3* size)
{
    OrientedBox oriented;
    bool have = false;
    if (selection.kind == Edit::Kind::Objects) {
        for (uint32_t id : selection.ids) {
            Obj::CCompositeObject* object = LiveObject(id);
            if (!object || !Objects::ModelBox(object, &oriented))
                continue;
            V3 a, b;
            BoundsOf(oriented, &a, &b);
            if (!have && selection.ids.size() == 1)
                *size = BoxSize(oriented);
            GrowBox(&box->min, &box->max, have, a, b);
            have = true;
        }
        if (have && selection.ids.size() != 1)
            *size = box->max - box->min;
        return have;
    }
    if (selection.kind == Edit::Kind::Scenery && SectorsBounds(selection.ids, box)) {
        *size = box->max - box->min;
        return true;
    }
    return false;
}

} // namespace

namespace {

// The registry's entries (level_editor_actions.h): their indices, once registered.
int g_actWindow = -1, g_actAdjust = -1, g_actEdgeRail = -1, g_actLinked = -1;

int AddAction(const char* id, const char* name, const char* category, const char* tip, std::vector<Actions::Chord> keys,
              std::function<void()> run, std::function<int()> checked = nullptr)
{
    Actions::Action a;
    a.id = id;
    a.name = name;
    a.category = category;
    a.tip = tip;
    a.kind = checked ? Actions::Kind::Toggle : Actions::Kind::Press;
    a.defaults = a.keys = std::move(keys);
    a.run = std::move(run);
    a.checked = std::move(checked);
    return Actions::Register(a);
}

void Toggle(bool* setting) { *setting = !*setting; }

void RegisterActions()
{
    if (g_actWindow >= 0)
        return;
    using Actions::Chord;
    const uint8_t ctrlAlt = Actions::kCtrl | Actions::kAlt;
    g_actWindow = AddAction("arrange_window", "Arrange window", "Panels",
                            "Align / distribute, replace the selection, linked copies, rail along an edge, the ghost and "
                            "snapping settings",
                            {Chord{SDL_SCANCODE_F2, 0}}, [] { Toggle(&Current().window); });
    g_actAdjust = AddAction("adjust_last", "Adjust last action", "Panels",
                            "The last move / turn / scale / duplicate / align / place: change its numbers, made again as "
                            "the same undo step (shown after each; F6 hides / shows it)",
                            {Chord{SDL_SCANCODE_F6, 0}}, [] { Toggle(&Adjust::Shown()); });
    g_actEdgeRail = AddAction("edge_rail", "Rail along an edge", "Edit",
                              "A scenery piece selected: its top edges shown - click one to lay a grind rail exactly along "
                              "it (Shift+click: more)",
                              {Chord{SDL_SCANCODE_F7, 0}}, [] { SetEdgePicking(!g_edgePicking); });
    g_actLinked = AddAction("linked_duplicate", "Duplicate linked", "Edit",
                            "Copies that stay linked to their source: its scale (objects: its fields and model) reach them",
                            {Chord{SDL_SCANCODE_D, ctrlAlt}}, [] { Links::LinkedDuplicate(); });
    static const char* const kEdges[] = {"min", "centre", "max"};
    static const char* const kAxes = "xyz";
    for (int axis = 0; axis < 3; ++axis) {
        for (int edge = 0; edge < 3; ++edge) {
            std::string key = std::string("align_") + kAxes[axis] + "_" + kEdges[edge];
            std::string name = std::string("Align ") + AxisName(axis) + " " + kEdges[edge];
            AddAction(key.c_str(), name.c_str(), "Arrange",
                      "Line the selected pieces up (the Arrange window's reference and space)", {}, [axis, edge] {
                          Align(axis, static_cast<Edge>(edge), Current().reference, Current().local);
                      });
        }
        std::string key = std::string("distribute_") + kAxes[axis];
        std::string name = std::string("Distribute along ") + AxisName(axis);
        AddAction(key.c_str(), name.c_str(), "Arrange", "Spread the selected pieces evenly (three or more)", {},
                  [axis] { Distribute(axis, Current().gaps, Current().local); });
    }
    AddAction("replace_selection", "Replace selection with the Add palette's item", "Arrange",
              "Each selected piece / object copy replaced by the highlighted piece / object, keeping its place, turn and "
              "scale (a level's own object: its model)",
              {}, [] { Replace(); });
    AddAction("unlink", "Unlink", "Arrange", "The selected linked copies made independent (on a source: its group dissolved)",
              {}, [] { Links::UnlinkSelection(); });
    AddAction("edge_snap", "Edge-to-edge snapping", "Settings", "Scenery's box faces snap flush to a neighbour's while moved",
              {}, [] { Toggle(&Current().edgeSnap); }, [] { return Current().edgeSnap ? 1 : 0; });
    AddAction("drag_readout", "Measurements while dragging", "Settings",
              "Size, distance moved, height above the ground, nearest rail / ledge next to the cursor", {},
              [] { Toggle(&Current().measure); }, [] { return Current().measure ? 1 : 0; });
    AddAction("placement_ghost", "Placement ghost", "Settings", "A see-through preview of what a click places", {},
              [] { Toggle(&Current().ghost); }, [] { return Current().ghost ? 1 : 0; });
    AddAction("click_places", "Click places the Add palette's item", "Settings",
              "With the Add palette open, a click in the view places it (Shift: more)", {},
              [] { Toggle(&Current().clickPlaces); }, [] { return Current().clickPlaces ? 1 : 0; });
}

V3 g_dragFrom;
bool g_wasDragging;

} // namespace

void Update()
{
    RegisterActions();
    RunQueued();
    if (!Edit::Dragging()) {
        if (Actions::Pressed(g_actWindow) && !Ui::Running())
            Hud::Message("The Arrange window (F2) needs the editor's UI ([features] editor_ui=1)");
        else if (Actions::Pressed(g_actWindow))
            Toggle(&Current().window);
        if (Actions::Pressed(g_actAdjust))
            Toggle(&Adjust::Shown());
        if (Actions::Pressed(g_actEdgeRail))
            SetEdgePicking(!g_edgePicking);
        if (Actions::Pressed(g_actLinked))
            Links::LinkedDuplicate();
    }
    if (g_edgePicking && Edit::Selection().kind != Edit::Kind::Scenery)
        g_edgePicking = false; // (deselected: Esc stops it)
    bool dragging = Edit::Dragging();
    if (dragging && !g_wasDragging)
        g_dragFrom = Edit::Pos();
    g_wasDragging = dragging;
}

namespace {

struct Painter {
    V3 camera;
    Frame frame;
    Lens lens;
    const uint32_t* viewport;
    float nearZ;
    Ui::Overlay* overlay;
    bool Pixel(V3 p, float* x, float* y) const
    {
        float nx, ny;
        if (!Project(camera, frame, lens, p, nearZ, &nx, &ny))
            return false;
        NdcToPixel(viewport, nx, ny, x, y);
        return true;
    }
    void Line(V3 a, V3 b, uint32_t argb, float thickness = 1.0f) const
    {
        if (!ClipToNear(camera, frame, nearZ + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by;
        if (Pixel(a, &ax, &ay) && Pixel(b, &bx, &by))
            overlay->lines.push_back({ax, ay, bx, by, argb, thickness});
    }
    void Box(const OrientedBox& box, uint32_t argb, uint32_t fill) const
    {
        V3 c[8];
        BoxCorners(box.origin, box.axes, box.min, box.max, c);
        for (int i = 0; i < 8; ++i) {
            for (int bit = 1; bit < 8; bit <<= 1) {
                if (!(i & bit))
                    Line(c[i], c[i | bit], argb, 1.5f);
            }
        }
        if (!fill)
            return;
        // (its six faces, see-through: two triangles each - only when all four corners are in front)
        static const int kFaces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
        for (const auto& f : kFaces) {
            float x[4], y[4];
            bool ok = true;
            for (int k = 0; k < 4 && ok; ++k)
                ok = Pixel(c[f[k]], &x[k], &y[k]);
            if (!ok)
                continue;
            overlay->triangles.push_back({x[0], y[0], x[1], y[1], x[2], y[2], fill});
            overlay->triangles.push_back({x[0], y[0], x[2], y[2], x[3], y[3], fill});
        }
    }
    void Text(float x, float y, uint32_t argb, const std::string& text) const { overlay->texts.push_back({x, y, argb, text}); }
};

// A node's marker for the ghost: a post, a ring and the heading it will face.
void Marker(const Painter& p, V3 at, float yaw, uint32_t colour)
{
    uint32_t c = (colour & 0x00FFFFFF) | 0xC0000000;
    p.Line(at, at + V3{0.0f, 72.0f, 0.0f}, c, 2.0f);
    constexpr int kSegments = 24;
    for (int i = 0; i < kSegments; ++i) {
        float a0 = 6.2831853f * static_cast<float>(i) / kSegments, a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
        p.Line(at + V3{std::sin(a0) * 30.0f, 1.0f, std::cos(a0) * 30.0f}, at + V3{std::sin(a1) * 30.0f, 1.0f, std::cos(a1) * 30.0f},
               c, 1.5f);
    }
    V3 heading = {std::sin(yaw) * 50.0f, 1.0f, std::cos(yaw) * 50.0f};
    p.Line(at + V3{0.0f, 1.0f, 0.0f}, at + heading, c, 2.0f);
}

// The surface's normal under the cursor (false: none).
bool SurfaceNormal(V3* normal)
{
    V3 origin, dir;
    if (!Edit::Ray(&origin, &dir))
        return false;
    Snap::SurfaceHit hit;
    if (!Snap::Surface(origin, Normalize(dir), 30000.0f, {}, &hit))
        return false;
    *normal = hit.normal;
    return true;
}

// The box turned so its up axis lies along `normal` (its heading kept) about the bottom-centre point `at`.
void TiltBox(OrientedBox* box, V3 at, V3 normal)
{
    V3 rows[3] = {box->axes[0], box->axes[1], box->axes[2]};
    if (!Snap::AlignRows(rows, normal))
        return;
    // (each corner's offset from `at`, in the box's frame, put back along the new rows)
    V3 origin = box->origin - at;
    float lengths[3];
    for (int i = 0; i < 3; ++i)
        lengths[i] = std::max(1e-6f, Dot(box->axes[i], box->axes[i]));
    V3 local = {Dot(origin, box->axes[0]) / lengths[0], Dot(origin, box->axes[1]) / lengths[1],
                Dot(origin, box->axes[2]) / lengths[2]};
    for (int i = 0; i < 3; ++i)
        box->axes[i] = rows[i];
    box->origin = at + rows[0] * local.x + rows[1] * local.y + rows[2] * local.z;
}

void AddGhost(const Painter& p)
{
    if (!Current().ghost || Edit::Dragging())
        return;
    V3 origin, dir;
    if (!Edit::Ray(&origin, &dir))
        return;
    int tab = 0;
    uint32_t id = 0;
    uint32_t colour = 0;
    int armed = Edit::PlaceArmed(&colour);
    V3 at = Edit::CursorPoint();
    constexpr uint32_t kLine = 0xD080E0FF, kFill = 0x2880E0FF;
    if (armed >= 0) {
        Marker(p, at, Edit::Yaw(), colour ? colour : 0xFF80E0FF);
        return;
    }
    if (!Edit::BrowserItem(&tab, &id))
        return;
    OrientedBox box;
    if (tab == 0) {
        if (!Scenery::Box(id, &box))
            return;
        Aabb bounds;
        BoundsOf(box, &bounds.min, &bounds.max);
        box.origin = box.origin + (at - BottomCentre(bounds));
        p.Box(box, kLine, kFill);
        return;
    }
    Obj::CCompositeObject* object = LiveObject(id);
    if (!object || !Objects::ModelBox(object, &box)) {
        Marker(p, at, Edit::Yaw(), 0xFF80E0FF);
        return;
    }
    box.origin = box.origin + (at - Objects::PosOf(object));
    V3 normal;
    if (Current().ghostSlope && SurfaceNormal(&normal))
        TiltBox(&box, at, normal);
    p.Box(box, kLine, kFill);
}

void AddReadout(const Painter& p)
{
    if (!Current().measure || !Edit::Dragging())
        return;
    Edit::Selected selection = Edit::Selection();
    Readout r;
    Aabb box;
    bool haveBox = SelectionBox(selection, &box, &r.size);
    V3 moved = Edit::Pos() - g_dragFrom;
    r.moved = std::sqrt(Dot(moved, moved));
    V3 base, ground;
    if (Edit::GroundMark(&base, &ground)) {
        r.haveGround = true;
        r.above = base.y - ground.y;
    }
    V3 nearest = {}, from = {};
    if (haveBox) {
        float best = 3000.0f;
        for (const Segment& s : Segments()) {
            if (selection.kind == Edit::Kind::Scenery && Scenery::CarriesRail(s.from) && Scenery::CarriesRail(s.to))
                continue; // (its own rails move with it)
            // (quickly: a segment far off its box passed)
            Aabb grown = {box.min - V3{best, best, best}, box.max + V3{best, best, best}};
            V3 lo = {std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y), std::min(s.a.z, s.b.z)};
            V3 hi = {std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y), std::max(s.a.z, s.b.z)};
            if (hi.x < grown.min.x || lo.x > grown.max.x || hi.y < grown.min.y || lo.y > grown.max.y ||
                hi.z < grown.min.z || lo.z > grown.max.z)
                continue;
            V3 on;
            float d = BoxSegmentDistance(box, s.a, s.b, &on);
            if (d < best) {
                best = d;
                nearest = on;
                r.haveRail = true;
                r.rail = d;
                r.ledge = s.ledge;
            }
        }
        if (r.haveRail) {
            from = {std::clamp(nearest.x, box.min.x, box.max.x), std::clamp(nearest.y, box.min.y, box.max.y),
                    std::clamp(nearest.z, box.min.z, box.max.z)};
            p.Line(from, nearest, 0xC0FF60D0, 1.0f);
        }
    }
    float x, y;
    Edit::Cursor(&x, &y);
    float line = 17.0f * static_cast<float>(p.viewport[3]) / 720.0f;
    std::vector<std::string> lines = ReadoutLines(r);
    if (!haveBox)
        lines.erase(lines.begin()); // (a node: no size)
    for (size_t i = 0; i < lines.size(); ++i)
        p.Text(x + 22.0f * line / 17.0f, y + 18.0f * line / 17.0f + line * static_cast<float>(i), 0xFFE8F0FF, lines[i]);
}

void AddContacts(const Painter& p)
{
    if (!Edit::Dragging() || SDL_GetTicks() - g_snap.contactsAt > 150)
        return;
    for (const Contact& c : g_snap.contacts) {
        if (c.other < 0 || c.other >= static_cast<int>(g_snap.boxes.size()))
            continue;
        // (the face met: the neighbour's rectangle on that plane where the two boxes meet)
        const Aabb& o = g_snap.boxes[c.other];
        const Aabb& m = g_snap.moving;
        int u = c.axis == 0 ? 1 : 0, v = c.axis == 2 ? 1 : 2;
        float u0 = std::max(Component(o.min, u), Component(m.min, u)), u1 = std::min(Component(o.max, u), Component(m.max, u));
        float v0 = std::max(Component(o.min, v), Component(m.min, v)), v1 = std::min(Component(o.max, v), Component(m.max, v));
        if (u1 < u0) // (beside it, not over it: the neighbour's whole face)
            u0 = Component(o.min, u), u1 = Component(o.max, u);
        if (v1 < v0)
            v0 = Component(o.min, v), v1 = Component(o.max, v);
        V3 corner[4];
        const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
        for (int k = 0; k < 4; ++k) {
            ComponentRef(corner[k], c.axis) = c.at;
            ComponentRef(corner[k], u) = us[k];
            ComponentRef(corner[k], v) = vs[k];
        }
        for (int k = 0; k < 4; ++k)
            p.Line(corner[k], corner[(k + 1) % 4], 0xFF40FF70, 2.5f);
    }
}

void AddEdges(const Painter& p)
{
    if (!g_edgePicking)
        return;
    const std::vector<Run>& runs = EdgeRuns();
    float x, y;
    Edit::Cursor(&x, &y);
    g_edgeHover = RunAt(runs, [&](V3 q, float* px, float* py) { return p.Pixel(q, px, py); }, x, y,
                        12.0f * static_cast<float>(p.viewport[3]) / 720.0f);
    for (size_t r = 0; r < runs.size(); ++r) {
        bool lit = static_cast<int>(r) == g_edgeHover;
        for (size_t i = 1; i < runs[r].points.size(); ++i)
            p.Line(runs[r].points[i - 1], runs[r].points[i], lit ? 0xFFFFE040 : 0xD040E0F0, lit ? 3.5f : 2.0f);
        if (lit) {
            char text[48];
            std::snprintf(text, sizeof(text), "rail here: %.0f long", runs[r].length);
            float px, py;
            if (p.Pixel(runs[r].points.front(), &px, &py))
                p.Text(px + 8.0f, py - 20.0f, 0xFFFFE040, text);
        }
    }
}

} // namespace

void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                Ui::Overlay* overlay)
{
    Painter p = {camera, frame, lens, viewport, nearZ, overlay};
    AddGhost(p);
    AddContacts(p);
    AddReadout(p);
    AddEdges(p);
}

bool PlaceClick(bool more)
{
    int tab = 0;
    uint32_t id = 0;
    if (!Current().clickPlaces || !Edit::BrowserItem(&tab, &id))
        return false;
    V3 at = Edit::CursorPoint();
    Edit::PlaceBrowserItem(at, more);
    // (an object stood square to the surface's slope - the ghost showed it so)
    V3 normal;
    Edit::Selected placed = Edit::Selection();
    if (tab == 1 && Current().ghostSlope && placed.kind == Edit::Kind::Objects && SurfaceNormal(&normal)) {
        for (uint32_t pid : placed.ids) {
            Obj::CCompositeObject* object = LiveObject(pid);
            if (!object)
                continue;
            for (Mth::Matrix* m : {&object->m_matrix, &object->m_displayMatrix}) {
                V3 rows[3];
                for (int i = 0; i < 3; ++i)
                    rows[i] = {m->row[i].x, m->row[i].y, m->row[i].z};
                if (!Snap::AlignRows(rows, normal))
                    continue;
                for (int i = 0; i < 3; ++i) {
                    m->row[i].x = rows[i].x;
                    m->row[i].y = rows[i].y;
                    m->row[i].z = rows[i].z;
                }
            }
        }
    }
    return true;
}

void Forget()
{
    g_snap = SnapCache{};
    g_edges = EdgeCache{};
    g_edgePicking = false;
    g_segmentsNodes = g_segmentsRails = ~0u;
    g_segments.clear();
}

} // namespace Front::LevelEditor::Arrange
