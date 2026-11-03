// NOT ORIGINAL - the level editor's arrange tools (ours; see level_editor_arrange.h).

#include "editor/level_editor_arrange.h"
#include "platform/keyboard.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "engine/maths/vec2.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_links.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_snap.h"
#include "editor/level_editor_ui.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "gpu/vertex_buffers.h"
#include "platform/run_log.h"
#include "engine/gfx/geom.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "gpu/vertex_view.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::arrange {

// --- the pure parts ---------------------------------------------------------------------------------------------

Span span_along(const std::vector<V3>& points, V3 axis)
{
    Span span;
    for (size_t i = 0; i < points.size(); ++i) {
        const float d = get_dot(core::at(points, i), axis);
        if (i == 0 || d < span.lo)
            span.lo = d;
        if (i == 0 || d > span.hi)
            span.hi = d;
    }
    return span;
}

namespace {

float edge_of(const Span& span, Edge edge)
{
    return edge == Edge::kMin ? span.lo : edge == Edge::kMax ? span.hi : (span.lo + span.hi) * 0.5f;
}

float get_component(V3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
float& component_ref(V3& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

} // namespace

std::vector<float> align_moves(const std::vector<Span>& spans, Edge edge, Reference reference)
{
    std::vector<float> moves(spans.size(), 0.0f);
    if (spans.empty())
        return moves;
    Span bounds = spans.front();
    if (reference == Reference::kBounds) {
        for (const Span& s : spans) {
            bounds.lo = std::min(bounds.lo, s.lo);
            bounds.hi = std::max(bounds.hi, s.hi);
        }
    }
    const float target = edge_of(bounds, edge);
    for (size_t i = 0; i < spans.size(); ++i)
        core::at(moves, i) = target - edge_of(core::at(spans, i), edge);
    return moves;
}

std::vector<float> distribute_moves(const std::vector<Span>& spans, bool gaps)
{
    std::vector<float> moves(spans.size(), 0.0f);
    const size_t n = spans.size();
    if (n < 3)
        return moves;
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i)
        core::at(order, i) = i;
    std::ranges::stable_sort(
        order, [&](size_t a, size_t b) { return edge_of(core::at(spans, a), Edge::kCentre) < edge_of(core::at(spans, b), Edge::kCentre); });
    const Span& first = core::at(spans, order.front());
    const Span& last = core::at(spans, order.back());
    if (!gaps) {
        const float c0 = edge_of(first, Edge::kCentre);
        const float c1 = edge_of(last, Edge::kCentre);
        for (size_t k = 1; k + 1 < n; ++k) {
            const float target = c0 + (c1 - c0) * static_cast<float>(k) / static_cast<float>(n - 1);
            core::at(moves, core::at(order, k)) = target - edge_of(core::at(spans, core::at(order, k)), Edge::kCentre);
        }
        return moves;
    }
    float sizes = 0.0f;
    for (const Span& s : spans)
        sizes += s.hi - s.lo;
    const float gap = (last.hi - first.lo - sizes) / static_cast<float>(n - 1);
    float at = first.hi + gap;
    for (size_t k = 1; k + 1 < n; ++k) {
        const Span& s = core::at(spans, core::at(order, k));
        core::at(moves, core::at(order, k)) = at - s.lo;
        at += (s.hi - s.lo) + gap;
    }
    return moves;
}

V3 edge_snap(const Aabb& moving, V3 delta, const std::vector<Aabb>& others, SnapReach snap_reach,
             std::vector<Contact>* contacts)
{
    const auto reach = snap_reach.reach;
    const auto axes = snap_reach.axes;
    V3 out = delta;
    Aabb m = {.min = moving.min + delta, .max = moving.max + delta};
    constexpr float kEps = 0.5f; // (boxes that only touch don't overlap)
    auto overlaps = [&](const Aabb& o, int axis, float slack) {
        return get_component(m.min, axis) < get_component(o.max, axis) + slack - kEps &&
               get_component(m.max, axis) > get_component(o.min, axis) - slack + kEps;
    };
    for (int axis = 0; axis < 3; ++axis) {
        if (!(axes & (1 << axis)))
            continue;
        float best = reach;
        float correction = 0.0f, at = 0.0f;
        int found = -1;
        // A snap candidate: its correction, the face it lands on and the box it is of.
        struct Candidate {
            float c{};
            float face{};
            int index{};
        };
        auto consider = [&](Candidate candidate) {
            const auto c = candidate.c;
            const auto face = candidate.face;
            const auto index = candidate.index;
            if (std::fabs(c) <= best) {
                best = std::fabs(c);
                correction = c;
                at = face;
                found = index;
            }
        };
        for (size_t i = 0; i < others.size(); ++i) {
            const Aabb& o = core::at(others, i);
            const int index = static_cast<int>(i);
            if (axis != 1) {
                // (side by side: at the same height, across within reach)
                const int across = axis == 0 ? 2 : 0;
                if (!overlaps(o, 1, 0.0f) || !overlaps(o, across, reach))
                    continue;
                consider({.c = get_component(o.max, axis) - get_component(m.min, axis), .face = get_component(o.max, axis), .index = index});
                consider({.c = get_component(o.min, axis) - get_component(m.max, axis), .face = get_component(o.min, axis), .index = index});
            } else {
                // (stacked on it: over it; level with its top: beside it, within reach)
                if (overlaps(o, 0, 0.0f) && overlaps(o, 2, 0.0f))
                    consider({.c = o.max.y - m.min.y, .face = o.max.y, .index = index});
                if (overlaps(o, 0, reach) && overlaps(o, 2, reach))
                    consider({.c = o.max.y - m.max.y, .face = o.max.y, .index = index});
            }
        }
        if (found >= 0) {
            component_ref(out, axis) += correction;
            if (contacts)
                contacts->push_back({.axis = axis, .at = at, .other = found});
        }
    }
    return out;
}

float snap_reach(float distance) { return std::clamp(distance * 0.01f, 6.0f, 24.0f); }

float box_segment_distance(const Aabb& box, V3 a, V3 b, V3* nearest)
{
    auto at = [&](float t) { return a + (b - a) * t; };
    auto distance = [&](float t) {
        const V3 p = at(t);
        const V3 q = {.x = std::clamp(p.x, box.min.x, box.max.x), .y = std::clamp(p.y, box.min.y, box.max.y),
                      .z = std::clamp(p.z, box.min.z, box.max.z)};
        const V3 d = p - q;
        return std::sqrt(get_dot(d, d));
    };
    // (the distance from a convex box along a line is convex: a ternary search finds its least)
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 60; ++i) {
        const float m1 = lo + (hi - lo) / 3.0f;
        const float m2 = hi - (hi - lo) / 3.0f;
        if (distance(m1) <= distance(m2))
            hi = m2;
        else
            lo = m1;
    }
    const float t = (lo + hi) * 0.5f;
    if (nearest)
        *nearest = at(t);
    return distance(t);
}

std::vector<std::string> readout_lines(const Readout& r)
{
    std::vector<std::string> lines;
    std::array<char, 96> text{};
    text::format_to(text, "size %.0f x %.0f x %.0f", r.size.x, r.size.y, r.size.z);
    lines.emplace_back(text.data());
    if (r.moved >= 0.5f) {
        text::format_to(text, "moved %.0f", r.moved);
        lines.emplace_back(text.data());
    }
    if (r.have_ground_) {
        text::format_to(text, "above ground %.0f", r.above);
        lines.emplace_back(text.data());
    }
    if (r.have_rail_) {
        text::format_to(text, "%s %.0f", r.ledge ? "ledge" : "rail", r.rail);
        lines.emplace_back(text.data());
    }
    return lines;
}

std::vector<Run> top_edges(const std::vector<V3>& triangles, EdgeLimits edge_limits)
{
    auto weld = edge_limits.weld;
    const auto min_length = edge_limits.min_length_;
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
        const int index = static_cast<int>(points.size());
        points.push_back(p);
        ids.emplace(key, index);
        return index;
    };
    struct EdgeUse {
        int top = 0, down = 0, up = 0, level = 0;
    };
    std::map<std::pair<int, int>, EdgeUse> edges;
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const std::array<int, 3> v = {{id(core::at(triangles, t)), id(core::at(triangles, t + 1)), id(core::at(triangles, t + 2))}};
        if (core::at(v, 0) == core::at(v, 1) || core::at(v, 1) == core::at(v, 2) || core::at(v, 0) == core::at(v, 2))
            continue;
        const V3 n = perpendicular(core::at(points, core::at(v, 1)) - core::at(points, core::at(v, 0)), core::at(points, core::at(v, 2)) - core::at(points, core::at(v, 0)));
        const float length = std::sqrt(get_dot(n, n));
        if (length < 1e-6f)
            continue;
        const bool top = std::fabs(n.y / length) >= kTopNormalY;
        for (int e = 0; e < 3; ++e) {
            const int a = core::at(v, e);
            const int b = core::at(v, (e + 1) % 3);
            const int c = core::at(v, (e + 2) % 3);
            EdgeUse& use = edges[{std::min(a, b), std::max(a, b)}];
            if (top) {
                ++use.top;
                continue;
            }
            const float dy = core::at(points, c).y - (core::at(points, a).y + core::at(points, b).y) * 0.5f;
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
        const int index = static_cast<int>(lips.size());
        lips.push_back(entry.first);
        at[entry.first.first].push_back(index);
        at[entry.first.second].push_back(index);
    }
    auto direction = [&](int from, int to) { return make_unit(core::at(points, to) - core::at(points, from)); };
    auto joins = [&](int prev, int mid, int next) {
        const float c = get_dot(direction(prev, mid), direction(mid, next));
        return c >= std::cos(kJoinDegrees * 3.14159265f / 180.0f);
    };
    std::vector<bool> used(lips.size(), false);
    // (the next edge from `point` along the run that came from `prev`: the point's only other lip edge, turning little)
    auto next = [&](int prev, int point, int* other) {
        const std::vector<int>& list = at[point];
        if (list.size() != 2)
            return -1;
        for (const int e : list) {
            if (core::at(used, e))
                continue;
            const int o = core::at(lips, e).first == point ? core::at(lips, e).second : core::at(lips, e).first;
            if (!joins(prev, point, o))
                return -1;
            *other = o;
            return e;
        }
        return -1;
    };
    for (size_t start = 0; start < lips.size(); ++start) {
        if (core::at(used, start))
            continue;
        core::at(used, start) = true;
        std::vector<int> chain = {core::at(lips, start).first, core::at(lips, start).second};
        // forward from the second point, then backward from the first
        for (int pass = 0; pass < 2; ++pass) {
            for (;;) {
                const int n = static_cast<int>(chain.size());
                const int prev = core::at(chain, n - 2);
                const int point = core::at(chain, n - 1);
                int other = -1;
                const int e = next(prev, point, &other);
                if (e < 0)
                    break;
                core::at(used, e) = true;
                chain.push_back(other);
                if (other == chain.front())
                    break; // (a closed loop)
            }
            if (pass == 0)
                std::ranges::reverse(chain);
        }
        // Straight stretches as one segment (a point kept where the run turns more than 2 degrees).
        Run run;
        run.points.push_back(core::at(points, chain.front()));
        for (size_t i = 1; i + 1 < chain.size(); ++i) {
            const V3 a = make_unit(core::at(points, core::at(chain, i)) - run.points.back());
            const V3 b = make_unit(core::at(points, core::at(chain, i + 1)) - core::at(points, core::at(chain, i)));
            if (get_dot(a, b) < std::cos(2.0f * 3.14159265f / 180.0f))
                run.points.push_back(core::at(points, core::at(chain, i)));
        }
        run.points.push_back(core::at(points, chain.back()));
        for (size_t i = 1; i < run.points.size(); ++i) {
            const V3 d = core::at(run.points, i) - core::at(run.points, i - 1);
            run.length += std::sqrt(get_dot(d, d));
        }
        if (run.length >= min_length)
            runs.push_back(std::move(run));
    }
    // (the longest first: they're the ones wanted most)
    std::ranges::stable_sort(runs, [](const Run& a, const Run& b) { return a.length > b.length; });
    return runs;
}

int run_at(const std::vector<Run>& runs, const std::function<bool(V3, float*, float*)>& project, maths::Vec2 at,
           float grab)
{
    const auto x = at.x;
    const auto y = at.y;
    int best = -1;
    float best_d = grab;
    for (size_t r = 0; r < runs.size(); ++r) {
        for (size_t i = 1; i < core::at(runs, r).points.size(); ++i) {
            float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
            if (!project(core::at(core::at(runs, r).points, i - 1), &ax, &ay) || !project(core::at(core::at(runs, r).points, i), &bx, &by))
                continue;
            const float d = distance_to_segment({.x = x, .y = y}, {.x = ax, .y = ay}, {.x = bx, .y = by});
            if (d <= best_d) {
                best_d = d;
                best = static_cast<int>(r);
            }
        }
    }
    return best;
}

Settings& at_hand()
{
    static Settings settings;
    return settings;
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

const std::array<V3, 3> kWorld = {{{.x = 1.0f, .y = 0.0f, .z = 0.0f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 0.0f, .y = 0.0f, .z = 1.0f}}};

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && !(object->object_flags() & objects::kObjectFlagDead) ? object : nullptr;
}

void get_corners(const OrientedBox& box, std::vector<V3>* out)
{
    std::array<V3, 8> c{};
    box_corners(box.origin, box.axes, box.min, box.max, c);
    out->insert(out->end(), c.begin(), c.end());
}

// A piece the tools move as one: an object, or a scenery piece with the shadows cast by it.
struct Unit {
    objects::GameEntity* object = nullptr;
    std::vector<uint32_t> sectors;
    std::vector<V3> corners;
};

std::vector<Unit> units_of(const edit::Selected& selection)
{
    std::vector<Unit> units;
    OrientedBox box{};
    if (selection.kind == edit::Kind::kObject) {
        for (const uint32_t id : selection.ids) {
            objects::GameEntity* object = live_object(id);
            if (!object)
                continue;
            Unit unit;
            unit.object = object;
            if (object_edit::model_box(object, &box))
                get_corners(box, &unit.corners);
            else
                unit.corners.push_back(object_edit::pos_of(object));
            units.push_back(std::move(unit));
        }
        return units;
    }
    if (selection.kind != edit::Kind::kScenery)
        return units;
    std::vector<uint32_t> shadows;
    for (const uint32_t s : selection.ids) {
        uint32_t caster = 0;
        if (scenery::is_shadow(s, &caster) && std::ranges::find(selection.ids, caster) != selection.ids.end() &&
            caster != s) {
            shadows.push_back(s);
            continue;
        }
        Unit unit;
        unit.sectors.push_back(s);
        units.push_back(std::move(unit));
    }
    for (const uint32_t s : shadows) {
        uint32_t caster = 0;
        scenery::is_shadow(s, &caster);
        for (Unit& unit : units) {
            if (unit.sectors.front() == caster)
                unit.sectors.push_back(s);
        }
    }
    for (Unit& unit : units) {
        for (const uint32_t s : unit.sectors) {
            if (scenery::cuboid(s, &box))
                get_corners(box, &unit.corners);
        }
    }
    std::erase_if(units, [](const Unit& u) { return u.corners.empty(); });
    return units;
}

// The axes the tools go along: the world's, or (Local) the first piece's own - an object's rows, a sector's quarter
// turns.
void axes_of(const std::vector<Unit>& units, bool local, std::span<V3, 3> out)
{
    for (int i = 0; i < 3; ++i)
        core::at(out, i) = core::at(kWorld, i);
    if (!local || units.empty())
        return;
    if (const objects::GameEntity* object = units.front().object) {
        for (int i = 0; i < 3; ++i)
            core::at(out, i) = make_unit(
                {.x = core::at(object->basis_frame().row, i).x, .y = core::at(object->basis_frame().row, i).y, .z = core::at(object->basis_frame().row, i).z});
        return;
    }
    Placement placement = {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
    scenery::placement_now(units.front().sectors.front(), &placement);
    for (int i = 0; i < 3; ++i)
        core::at(out, i) = get_quarter_turn(core::at(kWorld, i), placement.quarters);
}

// Each unit moved by its own amount along `axis` (one undo step).
void move_units(const edit::Selected& selection, const std::vector<Unit>& units, V3 axis,
                const std::vector<float>& moves)
{
    edit::get_begin();
    if (selection.kind == edit::Kind::kScenery) {
        carry::disarm(); // (each piece moves by its own amount: nothing is carried along)
        for (size_t i = 0; i < units.size(); ++i) {
            if (std::fabs(core::at(moves, i)) < 1e-4f)
                continue;
            scenery::set_selection(core::at(units, i).sectors);
            scenery::move_to(scenery::get_pivot() + axis * core::at(moves, i));
        }
        scenery::set_selection(selection.ids);
    } else {
        for (size_t i = 0; i < units.size(); ++i) {
            if (core::at(units, i).object && std::fabs(core::at(moves, i)) >= 1e-4f)
                object_edit::get_place(core::at(units, i).object, object_edit::pos_of(core::at(units, i).object) + axis * core::at(moves, i));
        }
    }
    edit::get_end();
}

const char* get_axis_name(int axis)
{
    constexpr std::array<const char*, 3> kNames = {"X", "Y", "Z"};
    return core::at(kNames, axis == 0 || axis == 1 ? axis : 2);
}

} // namespace

bool justify(int axis, Edge edge, Reference reference, bool local)
{
    const edit::Selected selection = edit::get_selection();
    const std::vector<Unit> units = units_of(selection);
    if (units.size() < 2) {
        hud::get_message("Align: select 2 or more items", 4.0);
        return false;
    }
    axis = std::clamp(axis, 0, 2);
    std::array<V3, 3> axes{};
    axes_of(units, local, axes);
    std::vector<Span> spans;
    spans.reserve(units.size());
    for (const Unit& unit : units)
        spans.push_back(span_along(unit.corners, core::at(axes, axis)));
    const std::vector<float> moves = align_moves(spans, edge, reference);
    move_units(selection, units, core::at(axes, axis), moves);
    std::array<char, 128> text{};
    text::format_to(text, "Aligned %d pieces: %s on %s (%s, %s)", static_cast<int>(units.size()),
                    edge == Edge::kMin   ? "min"
                    : edge == Edge::kMax ? "max"
                                         : "centre",
                    get_axis_name(axis), local ? "local" : "world",
                    reference == Reference::kFirst ? "the first" : "the bounds");
    hud::get_message(text.data(), 4.0);
    adjust::ActStep action;
    action.kind = adjust::Kind::kJustify;
    action.axis = axis;
    action.edge = static_cast<int>(edge);
    action.reference = static_cast<int>(reference);
    action.local = local;
    adjust::get_note(action);
    return true;
}

bool distribute(int axis, bool gaps, bool local)
{
    const edit::Selected selection = edit::get_selection();
    const std::vector<Unit> units = units_of(selection);
    if (units.size() < 3) {
        hud::get_message("Distribute: select 3 or more items", 4.0);
        return false;
    }
    axis = std::clamp(axis, 0, 2);
    std::array<V3, 3> axes{};
    axes_of(units, local, axes);
    std::vector<Span> spans;
    spans.reserve(units.size());
    for (const Unit& unit : units)
        spans.push_back(span_along(unit.corners, core::at(axes, axis)));
    move_units(selection, units, core::at(axes, axis), distribute_moves(spans, gaps));
    std::array<char, 128> text{};
    text::format_to(text, "Distributed %d pieces along %s (%s)", static_cast<int>(units.size()), get_axis_name(axis),
                    gaps ? "equal gaps" : "evenly spaced centres");
    hud::get_message(text.data(), 4.0);
    adjust::ActStep action;
    action.kind = adjust::Kind::kDistribute;
    action.axis = axis;
    action.gaps = gaps;
    action.local = local;
    adjust::get_note(action);
    return true;
}

// --- Replace selection with ---

namespace {

// The axis-aligned bounds of sectors (false: none has a box).
bool sectors_bounds(const std::vector<uint32_t>& sectors, Aabb* out)
{
    bool have = false;
    OrientedBox box{};
    for (const uint32_t s : sectors) {
        V3 a{}, b{};
        if (!scenery::cuboid(s, &box))
            continue;
        bounds_of(box, &a, &b);
        grow_box(&out->min, &out->max, have, a, b);
        have = true;
    }
    return have;
}

V3 bottom_centre(const Aabb& box)
{
    return {.x = (box.min.x + box.max.x) * 0.5f, .y = box.min.y, .z = (box.min.z + box.max.z) * 0.5f};
}

uint32_t key_model() { return checksums::hash_from_text("model"); }

// A node's model= (empty: none).
std::string model_of(uint32_t name)
{
    const int index = nodes::get_find(name);
    script::Struct* s = index >= 0 ? nodes::struct_of(core::at(nodes::get_all(), index)) : nullptr;
    const char* model = nullptr;
    if (!s || !s->fetch_text(key_model(), &model, 0) || !model)
        return {};
    return model;
}

bool replace_scenery(const edit::Selected& selection, uint32_t source)
{
    if (std::ranges::find(selection.ids, source) != selection.ids.end()) {
        hud::get_message("Replace: the replacement is part of the selection", 4.0);
        return false;
    }
    const std::vector<Unit> units = units_of(selection);
    if (units.empty())
        return false;
    Placement source_at = {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
    scenery::placement_now(source, &source_at);
    const size_t depth = edit::deepness();
    // (the pieces hidden first: undone last, they come back selected)
    edit::push_snapshot();
    carry::disarm(); // (nothing follows: what stood on the pieces stays where it is)
    scenery::hide_selection();
    std::vector<uint32_t> made;
    for (const Unit& unit : units) {
        Aabb was;
        if (!sectors_bounds(unit.sectors, &was))
            continue;
        Placement placement = {.quarters = 0, .offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f}};
        scenery::placement_now(unit.sectors.front(), &placement);
        const uint32_t copy = scenery::get_duplicate(source);
        if (!copy)
            continue;
        scenery::set_selection({copy});
        // (its turns and scale as the piece's own: the copy is the source as it is now)
        const int quarters = ((placement.quarters - source_at.quarters) % 4 + 4) % 4;
        if (quarters)
            scenery::get_turn(quarters);
        const V3 ratio = {.x = placement.scale.x / source_at.scale.x, .y = placement.scale.y / source_at.scale.y,
                          .z = placement.scale.z / source_at.scale.z};
        if (std::fabs(ratio.x - 1.0f) > 1e-4f || std::fabs(ratio.y - 1.0f) > 1e-4f || std::fabs(ratio.z - 1.0f) > 1e-4f)
            scenery::scale_selection(turned_scale(ratio, placement.quarters), is_uniform_scale(ratio));
        Aabb now;
        if (sectors_bounds({copy}, &now))
            scenery::move_to(scenery::get_pivot() + (bottom_centre(was) - bottom_centre(now)));
        scenery::finalise();
        made.push_back(copy);
    }
    if (made.empty()) {
        edit::take_back();
        hud::get_message("Replace: that piece couldn't be copied", 4.0);
        return false;
    }
    edit::push_made({.objects = {}, .sectors = made});
    edit::fold(depth);
    edit::choose_one({.kind = edit::Kind::kScenery, .ids = made});
    hud::get_message("Replaced " + std::to_string(made.size()) + " piece(s) with " + object_edit::caption(source) +
                         " - Ctrl+Z puts them back",
                     4.0);
    get_log("level editor: replaced %u scenery piece(s) with %s", static_cast<unsigned>(made.size()),
            object_edit::caption(source).c_str());
    return true;
}

bool replace_objects(const edit::Selected& selection, uint32_t source)
{
    const std::string model = model_of(source);
    std::vector<object_edit::State> copies; // (object copies: replaced by copies of the source where they were)
    std::vector<uint32_t> copy_ids, swapped;
    for (const uint32_t id : selection.ids) {
        objects::GameEntity* object = live_object(id);
        if (!object || id == source)
            continue;
        if (object_edit::is_copy(id)) {
            copies.push_back(object_edit::store_away(object));
            copy_ids.push_back(id);
        } else {
            swapped.push_back(id);
        }
    }
    if (copies.empty() && (swapped.empty() || model.empty())) {
        hud::get_message(model.empty() ? "Replace: original object has no model to swap" : "Replace: nothing selected",
                         5.0);
        return false;
    }
    const size_t depth = edit::deepness();
    std::vector<uint32_t> made;
    if (!copies.empty()) {
        edit::remove_copies(copy_ids);
        for (const object_edit::State& was : copies) {
            const V3 pos = {.x = was.pos.x, .y = was.pos.y, .z = was.pos.z};
            const objects::GameEntity* copy = object_edit::make_copy(source, pos);
            if (!copy)
                continue;
            object_edit::State state = was;
            state.id = copy->ident();
            object_edit::get_restore(state);
            made.push_back(copy->ident());
        }
        if (!made.empty())
            edit::push_made({.objects = made, .sectors = {}});
    }
    // (a level's own object: the source's model given to it - the Inspector's model swap)
    int swaps = 0;
    for (const uint32_t id : model.empty() ? std::vector<uint32_t>{} : swapped) {
        const std::string now = model_of(id);
        if (now.empty() || _stricmp(now.c_str(), model.c_str()) == 0)
            continue;
        entities::get_queue([id, now] { entities::detail::watch_model(id, now); });
        if (edit::set_field({.node = id, .key = key_model(), .kind = static_cast<int>(fields::Kind::kChars)}, model))
            ++swaps;
    }
    edit::fold(depth);
    if (!made.empty())
        edit::choose_one({.kind = edit::Kind::kObject, .ids = made});
    std::array<char, 160> text{};
    text::format_to(text, "Replaced %d copies; %d level objects re-modelled", static_cast<int>(made.size()), swaps);
    hud::get_message(text.data(), 5.0);
    get_log("level editor: replace with %s: %d copies made again, %d model swaps", object_edit::caption(source).c_str(),
            static_cast<int>(made.size()), swaps);
    return !made.empty() || swaps > 0;
}

} // namespace

bool get_replace()
{
    int tab = 0;
    uint32_t source = 0;
    if (!edit::browser_item(&tab, &source)) {
        hud::get_message("Replace: highlight an item in Add Piece (B)", 5.0);
        return false;
    }
    const edit::Selected selection = edit::get_selection();
    if (selection.kind == edit::Kind::kScenery && tab == 0)
        return replace_scenery(selection, source);
    if (selection.kind == edit::Kind::kObject && tab == 1)
        return replace_objects(selection, source);
    hud::get_message(selection.kind == edit::Kind::kScenery  ? "Replace: choose a scenery piece"
                     : selection.kind == edit::Kind::kObject ? "Replace: choose an object"
                                                             : "Replace: nothing selected",
                     5.0);
    return false;
}

// --- edge-to-edge snapping ---

namespace {

struct SnapCache {
    std::vector<uint32_t> selection;
    uint32_t last_used_ = 0;
    std::vector<Aabb> boxes;
    std::vector<Contact> contacts;
    Aabb moving;
    uint32_t contacts_at_ = 0;
};
SnapCache& stored_snap()
{
    static core::NoDestroy<SnapCache> snap;
    return snap.get();
}

struct GatherContext {
    const std::vector<uint32_t>* skip{};
    Aabb near;
    std::vector<Aabb>* out{};
};

void gather_sector(void* context, uint32_t checksum, render::WorldCell*)
{
    auto* c = static_cast<GatherContext*>(context);
    if (std::ranges::find(*c->skip, checksum) != c->skip->end() || scenery::get_hidden(checksum))
        return;
    OrientedBox box{};
    if (!scenery::cuboid(checksum, &box))
        return;
    Aabb a;
    bounds_of(box, &a.min, &a.max);
    // (not the ground or a whole street; only near the piece)
    if (!sector_size_movable(a.min, a.max, 2500.0f))
        return;
    if (a.max.x < c->near.min.x || a.min.x > c->near.max.x || a.max.y < c->near.min.y || a.min.y > c->near.max.y ||
        a.max.z < c->near.min.z || a.min.z > c->near.max.z)
        return;
    c->out->push_back(a);
}

} // namespace

V3 edge_snapped(V3 pos, V3 wanted, int axes)
{
    if (!at_hand().edge_snap_)
        return wanted;
    const edit::Selected selection = edit::get_selection();
    if (selection.kind != edit::Kind::kScenery)
        return wanted;
    Aabb moving;
    if (!sectors_bounds(selection.ids, &moving))
        return wanted;
    const uint32_t now = SDL_GetTicks();
    if (stored_snap().selection != selection.ids || now - stored_snap().last_used_ > 300) {
        // (the neighbours, once a drag: within 1500 of where the piece is - they don't move meanwhile)
        stored_snap().selection = selection.ids;
        stored_snap().boxes.clear();
        GatherContext context = {
            .skip = &selection.ids, .near = {.min = moving.min - V3{.x = 1500, .y = 1500, .z = 1500}, .max = moving.max + V3{.x = 1500, .y = 1500, .z = 1500}}, .out = &stored_snap().boxes};
        scenery::for_each_level_sector(gather_sector, &context);
    }
    stored_snap().last_used_ = now;
    float distance = 0.0f;
    ViewNow view{};
    if (current_view(&view)) {
        const V3 d = pos - view.pos;
        distance = std::sqrt(get_dot(d, d));
    }
    stored_snap().contacts.clear();
    const V3 delta = edge_snap(moving, wanted - pos, stored_snap().boxes, {.reach = snap_reach(distance), .axes = axes}, &stored_snap().contacts);
    stored_snap().moving = {.min = moving.min + delta, .max = moving.max + delta};
    stored_snap().contacts_at_ = now;
    return pos + delta;
}

// --- auto-rail along an edge ---

namespace {

bool& stored_edge_picking()
{
    static bool edge_picking{};
    return edge_picking;
}
struct EdgeCache {
    std::vector<uint32_t> selection;
    V3 pivot = {};
    std::vector<Run> runs;
};
// NOT ORIGINAL grouping - level_editor_arrange.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_arrange_state()).
struct LevelEditorArrangeState {
    EdgeCache edges_{};
    int edge_hover_ = -1;
};

LevelEditorArrangeState& level_editor_arrange_state()
{
    static core::NoDestroy<LevelEditorArrangeState> state;
    return state.get();
}

// A sector's drawn triangles where it is now (its meshes' first index buffers as strips - as picking reads them).
void sector_triangles(uint32_t checksum, std::vector<V3>* out)
{
    const render::PcGeom* geom = scenery::sector_geometry(checksum);
    if (!geom)
        return;
    const std::span<render::MeshRef*> meshes = geom->meshes() ? geom->mesh_run() : std::span<render::MeshRef*>();
    const uint32_t count = geom->mesh_count();
    for (uint32_t i = 0; !meshes.empty() && i < count && i < 4096 && out->size() < 600000; ++i) {
        render::MeshRef* mesh = core::at(meshes, i);
        if (!mesh || mesh->billboard() || (mesh->flags() & 0x10))
            continue;
        render::VertexBuffer* vertices = mesh->vertex_buffer_count() ? core::at(mesh->vertex_buffers(), 0) : nullptr;
        const render::IdxStore* indices = core::at(mesh->index_buffers(), 0);
        if (!vertices || !vertices->data() || !indices || !indices->indices() || mesh->stride() < 12)
            continue;
        auto at = [&](uint16_t k) {
            const auto f = gpu::VertexView(vertices->data(), mesh->stride(), mesh->vertex_count()).float3(k);
            return V3{.x = core::at(f, 0), .y = core::at(f, 1), .z = core::at(f, 2)};
        };
        const std::span<const uint16_t> index(indices->indices(), core::at(mesh->index_counts(), 0));
        for (uint32_t k = 2; k < index.size(); ++k) {
            const uint16_t a = core::at(index, k - 2);
            const uint16_t b = core::at(index, k - 1);
            const uint16_t c = core::at(index, k);
            if (a == b || b == c || a == c || a >= mesh->vertex_count() || b >= mesh->vertex_count() ||
                c >= mesh->vertex_count())
                continue;
            out->push_back(at(a));
            out->push_back(at(b));
            out->push_back(at(c));
        }
    }
}

const std::vector<Run>& edge_runs()
{
    const edit::Selected selection = edit::get_selection();
    const V3 pivot = selection.kind == edit::Kind::kScenery ? scenery::get_pivot() : V3{};
    if (selection.ids != level_editor_arrange_state().edges_.selection || pivot.x != level_editor_arrange_state().edges_.pivot.x || pivot.y != level_editor_arrange_state().edges_.pivot.y ||
        pivot.z != level_editor_arrange_state().edges_.pivot.z) {
        level_editor_arrange_state().edges_.selection = selection.ids;
        level_editor_arrange_state().edges_.pivot = pivot;
        level_editor_arrange_state().edges_.runs.clear();
        if (selection.kind == edit::Kind::kScenery) {
            std::vector<V3> triangles;
            for (const uint32_t s : selection.ids) {
                if (!scenery::is_shadow(s))
                    sector_triangles(s, &triangles);
            }
            level_editor_arrange_state().edges_.runs = top_edges(triangles, {.weld = 0.5f, .min_length_ = 16.0f});
            if (level_editor_arrange_state().edges_.runs.size() > 256)
                level_editor_arrange_state().edges_.runs.resize(256);
        }
    }
    return level_editor_arrange_state().edges_.runs;
}

bool pixel_of(const ViewNow& view, V3 p, float* x, float* y)
{
    const TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    float nx = 0.0f, ny = 0.0f;
    if (!cast_onto(view.pos, frame, view.lens, p, view.near_z_, &nx, &ny))
        return false;
    ndc_to_pixel(view.viewport, nx, ny, x, y);
    return true;
}

} // namespace

bool edge_picking() { return stored_edge_picking(); }

void set_edge_picking(bool on)
{
    if (on && edit::get_selection().kind != edit::Kind::kScenery) {
        hud::get_message("Rail Along Edge: select a scenery piece", 4.0);
        on = false;
    }
    stored_edge_picking() = on;
    level_editor_arrange_state().edge_hover_ = -1;
    if (on) {
        const size_t runs = edge_runs().size();
        hud::get_message(
            runs ? "Rail Along Edge: click a top edge (Shift: more)" : "Rail Along Edge: no top edges found", 5.0);
        if (!runs)
            stored_edge_picking() = false;
    }
}

bool edge_click(maths::Vec2 at)
{
    const float x = at.x, y = at.y;
    if (!stored_edge_picking())
        return false;
    ViewNow view{};
    if (!current_view(&view))
        return false;
    const std::vector<Run>& runs = edge_runs();
    const int pick = run_at(
        runs, [&](V3 p, float* px, float* py) { return pixel_of(view, p, px, py); }, {.x = x, .y = y},
        12.0f * static_cast<float>(core::at(view.viewport, 3)) / 720.0f);
    if (pick < 0) {
        hud::get_message("Rail Along Edge: click a highlighted edge");
        return true;
    }
    const Run& run = core::at(runs, pick);
    chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    edit::Selected selection = edit::get_selection();
    const uint32_t cluster = selection.ids.empty() ? 0 : selection.ids.front();
    if (!chains::add_rail(run.points, cluster, &message, &select)) {
        hud::get_message(message.empty() ? std::string("Rail along an edge: couldn't add the rail") : message, 4.0);
        return true;
    }
    const platform::KeyState keys = platform::keyboard_keys();
    const bool more = core::at(keys, SDL_SCANCODE_LSHIFT) || core::at(keys, SDL_SCANCODE_RSHIFT);
    std::array<char, 128> text{};
    text::format_to(text, "Rail added: %d points, %.0f long", static_cast<int>(run.points.size()), run.length);
    // (Shift: the piece stays selected for the next edge; else the rail's first point is selected)
    edit::chain_changed(&before, more ? 0 : select, text.data());
    if (more)
        edit::choose_one(selection);
    else
        stored_edge_picking() = false;
    get_log("level editor: auto-rail along an edge of %s: %d points, %.0f long", object_edit::caption(cluster).c_str(),
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
// NOT ORIGINAL grouping - the segments and the nodes and rails they were made from (were globals). Made on first use,
// never destroyed (segments()).
struct Segments {
    std::vector<Segment> segments_{};
    uint32_t segments_nodes_ = ~0u;
    uint32_t segments_rails_ = ~0u;
};

Segments& segments()
{
    static core::NoDestroy<Segments> state;
    return state.get();
}

const std::vector<Segment>& current_segments()
{
    if (segments().segments_nodes_ == nodes::get_generation() && segments().segments_rails_ == scenery::rail_generation())
        return segments().segments_;
    segments().segments_nodes_ = nodes::get_generation();
    segments().segments_rails_ = scenery::rail_generation();
    segments().segments_.clear();
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    auto place = [&](const nodes::Elem& n, V3* p) {
        if (scenery::get_rail_point(n.name, p, true))
            return true;
        *p = n.pos;
        return n.has_pos_;
    };
    for (const nodes::Elem& n : nodes) {
        const bool ledge = n.family == nodes::Family::kLadder && chains::is_ledge_node(n.name);
        if (n.hidden || (n.family != nodes::Family::kRail && !ledge))
            continue;
        V3 a{};
        if (!place(n, &a))
            continue;
        for (const int link : n.links) {
            if (link < 0 || std::cmp_greater_equal(link, nodes.size()))
                continue;
            V3 b{};
            if (place(core::at(nodes, link), &b))
                segments().segments_.push_back({.a = a, .b = b, .from = n.name, .to = core::at(nodes, link).name, .ledge = ledge});
        }
    }
    return segments().segments_;
}

// The selection's axis-aligned box and its size (an object's own: its model box's).
bool selection_box(const edit::Selected& selection, Aabb* box, V3* size)
{
    OrientedBox oriented{};
    bool have = false;
    if (selection.kind == edit::Kind::kObject) {
        for (const uint32_t id : selection.ids) {
            objects::GameEntity* object = live_object(id);
            if (!object || !object_edit::model_box(object, &oriented))
                continue;
            V3 a{}, b{};
            bounds_of(oriented, &a, &b);
            if (!have && selection.ids.size() == 1)
                *size = box_size(oriented);
            grow_box(&box->min, &box->max, have, a, b);
            have = true;
        }
        if (have && selection.ids.size() != 1)
            *size = box->max - box->min;
        return have;
    }
    if (selection.kind == edit::Kind::kScenery && sectors_bounds(selection.ids, box)) {
        *size = box->max - box->min;
        return true;
    }
    return false;
}

} // namespace

namespace {

// NOT ORIGINAL grouping - the arrange actions' windows (were globals). Made on first use, never destroyed
// (arrange_actions()).
struct ArrangeActions {
    // The registry's entries (level_editor_actions.h): their indices, once registered.
    int act_window_ = -1;
    int act_adjust_ = -1;
    int act_edge_rail_ = -1;
    int act_linked_ = -1;
};

ArrangeActions& arrange_actions()
{
    static core::NoDestroy<ArrangeActions> state;
    return state.get();
}

int add_action(const char* id, const char* name, const char* category, const char* tip,
               std::vector<actions::Chord> keys, std::function<void()> run, std::function<int()> checked = nullptr)
{
    actions::ActStep a;
    a.id = id;
    a.name = name;
    a.category = category;
    a.tip = tip;
    a.kind = checked ? actions::Kind::kFlipOver : actions::Kind::kPress;
    a.defaults = a.keys = std::move(keys);
    a.run = std::move(run);
    a.checked = std::move(checked);
    return actions::enrol(a);
}

void flip_over(bool* setting) { *setting = !*setting; }

void register_actions()
{
    if (arrange_actions().act_window_ >= 0)
        return;
    using actions::Chord;
    const uint8_t ctrl_alt = actions::kCtrl | actions::kAlt;
    arrange_actions().act_window_ = add_action("arrange_window", "Arrange", "Panels", "Align, distribute and replace",
                              {Chord{.scancode = SDL_SCANCODE_F2, .mods = 0}}, [] { flip_over(&at_hand().window); });
    arrange_actions().act_adjust_ =
        add_action("adjust_last", "Adjust Last Operation", "Panels", "Edit the parameters of the last operation",
                   {Chord{.scancode = SDL_SCANCODE_F4, .mods = 0}}, [] { flip_over(&adjust::get_shown()); });
    arrange_actions().act_edge_rail_ = add_action("edge_rail", "Rail Along Edge", "Edit", "Click a scenery edge to add a rail along it",
                                 {Chord{.scancode = SDL_SCANCODE_R, .mods = actions::kAlt}}, [] { set_edge_picking(!stored_edge_picking()); });
    arrange_actions().act_linked_ =
        add_action("linked_duplicate", "Duplicate Linked", "Edit", "Duplicate; copies follow the source's changes",
                   {Chord{.scancode = SDL_SCANCODE_D, .mods = ctrl_alt}}, [] { links::linked_duplicate(); });
    static const auto kEdges = std::to_array<const char*>({"min", "centre", "max"});
    static constexpr std::string_view kAxes = "xyz";
    for (int axis = 0; axis < 3; ++axis) {
        for (int edge = 0; edge < 3; ++edge) {
            const std::string key = std::string("align_") + core::at(kAxes, axis) + "_" + core::at(kEdges, edge);
            const std::string name = std::string("Align ") + get_axis_name(axis) + " " + core::at(kEdges, edge);
            add_action(key.c_str(), name.c_str(), "Arrange", "Align the selection", {},
                       [axis, edge] { justify(axis, static_cast<Edge>(edge), at_hand().reference, at_hand().local); });
        }
        const std::string key = std::string("distribute_") + core::at(kAxes, axis);
        const std::string name = std::string("Distribute along ") + get_axis_name(axis);
        add_action(key.c_str(), name.c_str(), "Arrange", "Space the selection evenly (3 or more)", {},
                   [axis] { distribute(axis, at_hand().gaps, at_hand().local); });
    }
    add_action("replace_selection", "Replace Selection", "Arrange", "Replace with the item highlighted in Add Piece",
               {}, [] { get_replace(); });
    add_action("unlink", "Unlink Copies", "Arrange", "Make linked copies independent", {},
               [] { links::unlink_selection(); });
    add_action(
        "edge_snap", "Edge Snapping", "Settings", "Snap scenery flush to neighbours", {},
        [] { flip_over(&at_hand().edge_snap_); }, [] { return at_hand().edge_snap_ ? 1 : 0; });
    add_action(
        "drag_readout", "Drag Measurements", "Settings", "Show distances while dragging", {},
        [] { flip_over(&at_hand().measure); }, [] { return at_hand().measure ? 1 : 0; });
    add_action(
        "placement_ghost", "Placement Preview", "Settings", "Preview what a click places", {},
        [] { flip_over(&at_hand().ghost); }, [] { return at_hand().ghost ? 1 : 0; });
    add_action(
        "click_places", "Click to Place", "Settings", "Place the Add Piece item with a click", {},
        [] { flip_over(&at_hand().click_places_); }, [] { return at_hand().click_places_ ? 1 : 0; });
}

// NOT ORIGINAL grouping - where a drag started and whether one was going on (were globals). Made on first use, never
// destroyed (arrange_drag()).
struct ArrangeDrag {
    V3 drag_from_{};
    bool was_dragging_{};
};

ArrangeDrag& arrange_drag()
{
    static core::NoDestroy<ArrangeDrag> state;
    return state.get();
}

} // namespace

void tick_frame()
{
    register_actions();
    run_queued();
    if (!edit::get_dragging()) {
        if (actions::squeezed(arrange_actions().act_window_) && !ui::underway())
            hud::get_message("Requires the editor UI (editor_ui=1)");
        else if (actions::squeezed(arrange_actions().act_window_))
            flip_over(&at_hand().window);
        if (actions::squeezed(arrange_actions().act_adjust_))
            flip_over(&adjust::get_shown());
        if (actions::squeezed(arrange_actions().act_edge_rail_))
            set_edge_picking(!stored_edge_picking());
        if (actions::squeezed(arrange_actions().act_linked_))
            links::linked_duplicate();
    }
    if (stored_edge_picking() && edit::get_selection().kind != edit::Kind::kScenery)
        stored_edge_picking() = false; // (deselected: Esc stops it)
    const bool dragging = edit::get_dragging();
    if (dragging && !arrange_drag().was_dragging_)
        arrange_drag().drag_from_ = edit::get_pos();
    arrange_drag().was_dragging_ = dragging;
}

namespace {

struct Painter {
    V3 camera{};
    TickCycle frame{};
    Lens lens{};
    std::span<const uint32_t, 4> viewport;
    float near_z_{};
    ui::Overlay* overlay{};
    bool get_pixel(V3 p, float* x, float* y) const
    {
        float nx = 0.0f, ny = 0.0f;
        if (!cast_onto(camera, frame, lens, p, near_z_, &nx, &ny))
            return false;
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    }
    void get_seg(V3 a, V3 b, uint32_t argb, float thickness = 1.0f) const
    {
        if (!clip_to_near(camera, frame, near_z_ + 0.01f, &a, &b))
            return;
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (get_pixel(a, &ax, &ay) && get_pixel(b, &bx, &by))
            overlay->lines.push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
    }
    // A box's edge colour and its fill colour.
    struct BoxColours {
        uint32_t argb{};
        uint32_t fill{};
    };

    void cuboid(const OrientedBox& box, BoxColours colours) const
    {
        const auto argb = colours.argb;
        const auto fill = colours.fill;
        std::array<V3, 8> c{};
        box_corners(box.origin, box.axes, box.min, box.max, c);
        for (int i = 0; i < 8; ++i) {
            for (int bit = 1; bit < 8; bit <<= 1) {
                if (!(i & bit))
                    get_seg(core::at(c, i), core::at(c, i | bit), argb, 1.5f);
            }
        }
        if (!fill)
            return;
        // (its six faces, see-through: two triangles each - only when all four corners are in front)
        static const std::array<std::array<int, 4>, 6> kFaces = {{{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                         {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}}};
        for (const auto& f : kFaces) {
            std::array<float, 4> x{};
            std::array<float, 4> y{};
            bool ok = true;
            for (int k = 0; k < 4 && ok; ++k)
                ok = get_pixel(core::at(c, core::at(f, k)), &core::at(x, k), &core::at(y, k));
            if (!ok)
                continue;
            overlay->triangles.push_back({.ax = core::at(x, 0), .ay = core::at(y, 0), .bx = core::at(x, 1), .by = core::at(y, 1), .cx = core::at(x, 2), .cy = core::at(y, 2), .argb = fill});
            overlay->triangles.push_back({.ax = core::at(x, 0), .ay = core::at(y, 0), .bx = core::at(x, 2), .by = core::at(y, 2), .cx = core::at(x, 3), .cy = core::at(y, 3), .argb = fill});
        }
    }
    void draw_label(float x, float y, uint32_t argb, const std::string& text) const
    {
        overlay->texts.push_back({.x = x, .y = y, .argb = argb, .text = text});
    }
};

// A marker's yaw and its colour.
struct MarkerLook {
    float yaw{};
    uint32_t colour{};
};

// A node's marker for the ghost: a post, a ring and the heading it will face.
void get_marker(const Painter& p, V3 at, MarkerLook marker_look)
{
    const auto yaw = marker_look.yaw;
    const auto colour = marker_look.colour;
    const uint32_t c = (colour & 0x00FFFFFF) | 0xC0000000;
    p.get_seg(at, at + V3{.x = 0.0f, .y = 72.0f, .z = 0.0f}, c, 2.0f);
    constexpr int kSegments = 24;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSegments;
        const float a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
        p.get_seg(at + V3{.x = std::sin(a0) * 30.0f, .y = 1.0f, .z = std::cos(a0) * 30.0f},
                  at + V3{.x = std::sin(a1) * 30.0f, .y = 1.0f, .z = std::cos(a1) * 30.0f}, c, 1.5f);
    }
    const V3 heading = {.x = std::sin(yaw) * 50.0f, .y = 1.0f, .z = std::cos(yaw) * 50.0f};
    p.get_seg(at + V3{.x = 0.0f, .y = 1.0f, .z = 0.0f}, at + heading, c, 2.0f);
}

// The surface's normal under the cursor (false: none).
bool surface_normal(V3* normal)
{
    V3 origin{}, dir{};
    if (!edit::get_ray({.origin = &origin, .dir = &dir}))
        return false;
    snap::SurfaceHit hit;
    if (!snap::get_surface(origin, make_unit(dir), 30000.0f, {}, &hit))
        return false;
    *normal = hit.normal;
    return true;
}

// The box turned so its up axis lies along `normal` (its heading kept) about the bottom-centre point `at`.
void tilt_box(OrientedBox* box, SurfaceHit surface)
{
    const auto at = surface.point;
    const auto normal = surface.normal;
    std::array<V3, 3> rows = {{core::at(box->axes, 0), core::at(box->axes, 1), core::at(box->axes, 2)}};
    if (!snap::align_rows(rows, normal))
        return;
    // (each corner's offset from `at`, in the box's frame, put back along the new rows)
    const V3 origin = box->origin - at;
    std::array<float, 3> lengths{};
    for (int i = 0; i < 3; ++i)
        core::at(lengths, i) = std::max(1e-6f, get_dot(core::at(box->axes, i), core::at(box->axes, i)));
    const V3 local = {.x = get_dot(origin, core::at(box->axes, 0)) / core::at(lengths, 0), .y = get_dot(origin, core::at(box->axes, 1)) / core::at(lengths, 1),
                      .z = get_dot(origin, core::at(box->axes, 2)) / core::at(lengths, 2)};
    for (int i = 0; i < 3; ++i)
        core::at(box->axes, i) = core::at(rows, i);
    box->origin = at + core::at(rows, 0) * local.x + core::at(rows, 1) * local.y + core::at(rows, 2) * local.z;
}

void add_ghost(const Painter& p)
{
    if (!at_hand().ghost || edit::get_dragging())
        return;
    V3 origin{}, dir{};
    if (!edit::get_ray({.origin = &origin, .dir = &dir}))
        return;
    int tab = 0;
    uint32_t id = 0;
    uint32_t colour = 0;
    const int armed = edit::place_armed(&colour);
    const V3 at = edit::cursor_point();
    constexpr uint32_t kLine = 0xD080E0FF, kFill = 0x2880E0FF;
    if (armed >= 0) {
        get_marker(p, at, {.yaw = edit::get_yaw(), .colour = colour ? colour : 0xFF80E0FF});
        return;
    }
    if (!edit::browser_item(&tab, &id))
        return;
    OrientedBox box{};
    if (tab == 0) {
        if (!scenery::cuboid(id, &box))
            return;
        Aabb bounds;
        bounds_of(box, &bounds.min, &bounds.max);
        box.origin = box.origin + (at - bottom_centre(bounds));
        p.cuboid(box, {.argb = kLine, .fill = kFill});
        return;
    }
    objects::GameEntity* object = live_object(id);
    if (!object || !object_edit::model_box(object, &box)) {
        get_marker(p, at, {.yaw = edit::get_yaw(), .colour = 0xFF80E0FF});
        return;
    }
    box.origin = box.origin + (at - object_edit::pos_of(object));
    V3 normal{};
    if (at_hand().ghost_slope_ && surface_normal(&normal))
        tilt_box(&box, {.point = at, .normal = normal});
    p.cuboid(box, {.argb = kLine, .fill = kFill});
}

void add_readout(const Painter& p)
{
    if (!at_hand().measure || !edit::get_dragging())
        return;
    const edit::Selected selection = edit::get_selection();
    Readout r;
    Aabb box;
    const bool have_box = selection_box(selection, &box, &r.size);
    const V3 moved = edit::get_pos() - arrange_drag().drag_from_;
    r.moved = std::sqrt(get_dot(moved, moved));
    V3 base{}, ground{};
    if (edit::ground_mark({.base = &base, .ground = &ground})) {
        r.have_ground_ = true;
        r.above = base.y - ground.y;
    }
    V3 nearest = {}, from = {};
    if (have_box) {
        float best = 3000.0f;
        for (const Segment& s : current_segments()) {
            if (selection.kind == edit::Kind::kScenery && scenery::carries_rail(s.from) && scenery::carries_rail(s.to))
                continue; // (its own rails move with it)
            // (quickly: a segment far off its box passed)
            const Aabb grown = {.min = box.min - V3{.x = best, .y = best, .z = best}, .max = box.max + V3{.x = best, .y = best, .z = best}};
            const V3 lo = {.x = std::min(s.a.x, s.b.x), .y = std::min(s.a.y, s.b.y), .z = std::min(s.a.z, s.b.z)};
            const V3 hi = {.x = std::max(s.a.x, s.b.x), .y = std::max(s.a.y, s.b.y), .z = std::max(s.a.z, s.b.z)};
            if (hi.x < grown.min.x || lo.x > grown.max.x || hi.y < grown.min.y || lo.y > grown.max.y ||
                hi.z < grown.min.z || lo.z > grown.max.z)
                continue;
            V3 on{};
            const float d = box_segment_distance(box, s.a, s.b, &on);
            if (d < best) {
                best = d;
                nearest = on;
                r.have_rail_ = true;
                r.rail = d;
                r.ledge = s.ledge;
            }
        }
        if (r.have_rail_) {
            from = {.x = std::clamp(nearest.x, box.min.x, box.max.x), .y = std::clamp(nearest.y, box.min.y, box.max.y),
                    .z = std::clamp(nearest.z, box.min.z, box.max.z)};
            p.get_seg(from, nearest, 0xC0FF60D0, 1.0f);
        }
    }
    const auto [x, y] = edit::get_cursor();
    const float line = 17.0f * static_cast<float>(core::at(p.viewport, 3)) / 720.0f;
    std::vector<std::string> lines = readout_lines(r);
    if (!have_box)
        lines.erase(lines.begin()); // (a node: no size)
    for (size_t i = 0; i < lines.size(); ++i)
        p.draw_label(x + 22.0f * line / 17.0f, y + 18.0f * line / 17.0f + line * static_cast<float>(i), 0xFFE8F0FF,
                     core::at(lines, i));
}

void add_contacts(const Painter& p)
{
    if (!edit::get_dragging() || SDL_GetTicks() - stored_snap().contacts_at_ > 150)
        return;
    for (const Contact& c : stored_snap().contacts) {
        if (c.other < 0 || std::cmp_greater_equal(c.other, stored_snap().boxes.size()))
            continue;
        // (the face met: the neighbour's rectangle on that plane where the two boxes meet)
        const Aabb& o = core::at(stored_snap().boxes, c.other);
        const Aabb& m = stored_snap().moving;
        const int u = c.axis == 0 ? 1 : 0;
        const int v = c.axis == 2 ? 1 : 2;
        float u0 = std::max(get_component(o.min, u), get_component(m.min, u)),
              u1 = std::min(get_component(o.max, u), get_component(m.max, u));
        float v0 = std::max(get_component(o.min, v), get_component(m.min, v)),
              v1 = std::min(get_component(o.max, v), get_component(m.max, v));
        if (u1 < u0) // (beside it, not over it: the neighbour's whole face)
            u0 = get_component(o.min, u), u1 = get_component(o.max, u);
        if (v1 < v0)
            v0 = get_component(o.min, v), v1 = get_component(o.max, v);
        std::array<V3, 4> corner{};
        const std::array<float, 4> us = {{u0, u1, u1, u0}};
        const std::array<float, 4> vs = {{v0, v0, v1, v1}};
        for (int k = 0; k < 4; ++k) {
            component_ref(core::at(corner, k), c.axis) = c.at;
            component_ref(core::at(corner, k), u) = core::at(us, k);
            component_ref(core::at(corner, k), v) = core::at(vs, k);
        }
        for (int k = 0; k < 4; ++k)
            p.get_seg(core::at(corner, k), core::at(corner, (k + 1) % 4), 0xFF40FF70, 2.5f);
    }
}

void add_edges(const Painter& p)
{
    if (!stored_edge_picking())
        return;
    const std::vector<Run>& runs = edge_runs();
    const auto [x, y] = edit::get_cursor();
    level_editor_arrange_state().edge_hover_ = run_at(
        runs, [&](V3 q, float* px, float* py) { return p.get_pixel(q, px, py); }, {.x = x, .y = y},
        12.0f * static_cast<float>(core::at(p.viewport, 3)) / 720.0f);
    for (size_t r = 0; r < runs.size(); ++r) {
        const bool lit = ints::as_signed(r) == level_editor_arrange_state().edge_hover_;
        for (size_t i = 1; i < core::at(runs, r).points.size(); ++i)
            p.get_seg(core::at(core::at(runs, r).points, i - 1), core::at(core::at(runs, r).points, i), lit ? 0xFFFFE040 : 0xD040E0F0, lit ? 3.5f : 2.0f);
        if (lit) {
            std::array<char, 48> text{};
            text::format_to(text, "rail here: %.0f long", core::at(runs, r).length);
            float px = 0.0f, py = 0.0f;
            if (p.get_pixel(core::at(runs, r).points.front(), &px, &py))
                p.draw_label(px + 8.0f, py - 20.0f, 0xFFFFE040, text.data());
        }
    }
}

} // namespace

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 ui::Overlay* overlay)
{
    const Painter p = {.camera = camera, .frame = frame, .lens = lens, .viewport = viewport, .near_z_ = near_z, .overlay = overlay};
    add_ghost(p);
    add_contacts(p);
    add_readout(p);
    add_edges(p);
}

bool place_click(bool more)
{
    int tab = 0;
    uint32_t id = 0;
    if (!at_hand().click_places_ || !edit::browser_item(&tab, &id))
        return false;
    const V3 at = edit::cursor_point();
    edit::place_browser_item(at, more);
    // (an object stood square to the surface's slope - the ghost showed it so)
    V3 normal{};
    const edit::Selected placed = edit::get_selection();
    if (tab == 1 && at_hand().ghost_slope_ && placed.kind == edit::Kind::kObject && surface_normal(&normal)) {
        for (const uint32_t pid : placed.ids) {
            objects::GameEntity* object = live_object(pid);
            if (!object)
                continue;
            for (maths::Mat4* m : {&object->basis_frame(), &object->display_matrix()}) {
                std::array<V3, 3> rows{};
                for (int i = 0; i < 3; ++i)
                    core::at(rows, i) = {.x = core::at(m->row, i).x, .y = core::at(m->row, i).y, .z = core::at(m->row, i).z};
                if (!snap::align_rows(rows, normal))
                    continue;
                for (int i = 0; i < 3; ++i) {
                    core::at(m->row, i).x = core::at(rows, i).x;
                    core::at(m->row, i).y = core::at(rows, i).y;
                    core::at(m->row, i).z = core::at(rows, i).z;
                }
            }
        }
    }
    return true;
}

void get_forget()
{
    stored_snap() = SnapCache{};
    level_editor_arrange_state().edges_ = EdgeCache{};
    stored_edge_picking() = false;
    segments().segments_nodes_ = segments().segments_rails_ = ~0u;
    segments().segments_.clear();
}

} // namespace frontend::level_editor::arrange
