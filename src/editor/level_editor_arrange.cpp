// NOT ORIGINAL - the level editor's arrange tools (ours; see level_editor_arrange.h).

#include "editor/level_editor_arrange.h"
#include "engine/pad_input/input_source.h"

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
#include "base/maths/vec2.h"
#include "base/core/checksums.h"
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
#include "engine/gfx/mesh_records.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_geom.h"
#include "base/core/ints.h"
#include "base/core/down_cast.h"
#include "engine/gfx/vertex_view.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/trig.h"

namespace frontend::level_editor::arrange {

namespace {

constexpr float kSnapReachPerUnit = 0.01F; // the snap reach grows with the distance ...
constexpr float kMinSnapReach = 6.0F;      // ... between these
constexpr float kMaxSnapReach = 24.0F;
constexpr int kSearchSteps = 60;               // ternary search steps
constexpr std::size_t kLineTextSize = 96;
constexpr float kMinMoved = 0.5F;    // a move smaller than this is none
constexpr float kDefaultWeld = 0.5F; // the grid size points are welded on
constexpr float kTinyLength = 1e-6F;
constexpr float kFullTurnPerPi = 2.0F; // a full turn is two pi radians
constexpr float kMoveEpsilon = 1e-4F;  // a move smaller than this is none
constexpr std::size_t kReportTextSize = 128;
constexpr float kRatioEpsilon = 1e-4F; // a scale ratio this close to 1 is unchanged
constexpr std::size_t kMessageTextSize = 160;
constexpr float kMaxMovableSize = 2500.0F;     // a sector larger than this is not moved by the snap
constexpr int kSnapRefreshMilliseconds = 300;  // the snap neighbourhood is gathered again after this long
constexpr int kSnapNeighbourhood = 1500;       // the moving box grown by this each way
constexpr uint32_t kMaxMeshesRead = 4096;
constexpr std::size_t kMaxTrianglesRead = 600000;
constexpr uint32_t kMeshSharedFlags = 0x10;    // mesh flags: skipped
constexpr uint32_t kPositionBytes = 12;        // a vertex position: three floats
constexpr float kMinEdgeLength = 16.0F;        // top edges shorter than this are dropped
constexpr std::size_t kMaxEdgeRuns = 256;
constexpr float kBoxLineWidth = 1.5F;
constexpr float kHeadHeight = 72.0F; // the marker pole
constexpr float kMarkerWidth = 2.0F;
constexpr float kHeadingRadius = 30.0F; // the heading arc
constexpr float kSurfaceReach = 30000.0F;
constexpr float kMinAxisLength = 1e-6F;
constexpr uint32_t kMarkerColour = 0xFF80E0FF; // ARGB
constexpr float kNearestReach = 3000.0F;       // contacts further than this are not drawn
constexpr uint32_t kContactColour = 0xC0FF60D0; // ARGB
constexpr float kReadoutX = 22.0F;              // the readout beside the cursor, laid out for a 17-pixel line
constexpr float kReadoutLineHeight = 17.0F;
constexpr float kReadoutY = 18.0F;
constexpr uint32_t kReadoutColour = 0xFFE8F0FF; // ARGB
constexpr uint32_t kContactMilliseconds = 150; // contacts older than this are not drawn
constexpr uint32_t kSnapBoxColour = 0xFF40FF70; // ARGB
constexpr float kSnapBoxWidth = 2.5F;
constexpr float kEdgePickReach = 12.0F;         // pixels, at the reference view height
constexpr uint32_t kLitEdgeColour = 0xFFFFE040; // ARGB
constexpr uint32_t kUnlitEdgeColour = 0xD040E0F0; // ARGB
constexpr float kLitEdgeWidth = 3.5F;
constexpr float kUnlitEdgeWidth = 2.0F;
constexpr std::size_t kEdgeTextSize = 48;
constexpr float kEdgeLabelOffsetX = 8.0F;
constexpr float kEdgeLabelRise = 20.0F;

} // namespace

// --- the pure parts ---------------------------------------------------------------------------------------------

Span span_along(const std::vector<V3>& points, V3 axis)
{
    Span span;
    for (size_t i = 0; i < points.size(); ++i) {
        const float distance = get_dot(core::at(points, i), axis);
        if (i == 0 || distance < span.lo) {
            span.lo = distance;
        }
        if (i == 0 || distance > span.hi) {
            span.hi = distance;
        }
    }
    return span;
}

namespace {

constexpr float kHalf = 0.5F; // half

float edge_of(const Span& span, Edge edge)
{
    if (edge == Edge::kMin) {
        return span.lo;
    }
    if (edge == Edge::kMax) {
        return span.hi;
    }
    return (span.lo + span.hi) * kHalf;
}
const char* edge_name(Edge edge)
{
    if (edge == Edge::kMin) {
        return "min";
    }
    if (edge == Edge::kMax) {
        return "max";
    }
    return "centre";
}

} // namespace

std::vector<float> align_moves(const std::vector<Span>& spans, Edge edge, Reference reference)
{
    std::vector<float> moves(spans.size(), 0.0F);
    if (spans.empty()) {
        return moves;
    }
    Span bounds = spans.front();
    if (reference == Reference::kBounds) {
        for (const Span& span : spans) {
            bounds.lo = std::min(bounds.lo, span.lo);
            bounds.hi = std::max(bounds.hi, span.hi);
        }
    }
    const float target = edge_of(bounds, edge);
    for (size_t i = 0; i < spans.size(); ++i) {
        core::at(moves, i) = target - edge_of(core::at(spans, i), edge);
    }
    return moves;
}

std::vector<float> distribute_moves(const std::vector<Span>& spans, bool gaps)
{
    std::vector<float> moves(spans.size(), 0.0F);
    const size_t count = spans.size();
    if (count < 3) {
        return moves;
    }
    std::vector<size_t> order(count);
    for (size_t i = 0; i < count; ++i) {
        core::at(order, i) = i;
    }
    std::ranges::stable_sort(
        order, [&](size_t a, size_t b) { return edge_of(core::at(spans, a), Edge::kCentre) < edge_of(core::at(spans, b), Edge::kCentre); });
    const Span& first = core::at(spans, order.front());
    const Span& last = core::at(spans, order.back());
    if (!gaps) {
        const float c0 = edge_of(first, Edge::kCentre);
        const float c1 = edge_of(last, Edge::kCentre);
        for (size_t k = 1; k + 1 < count; ++k) {
            const float target = c0 + ((c1 - c0) * static_cast<float>(k) / static_cast<float>(count - 1));
            core::at(moves, core::at(order, k)) = target - edge_of(core::at(spans, core::at(order, k)), Edge::kCentre);
        }
        return moves;
    }
    float sizes = 0.0F;
    for (const Span& span : spans) {
        sizes += span.hi - span.lo;
    }
    const float gap = (last.hi - first.lo - sizes) / static_cast<float>(count - 1);
    float at = first.hi + gap;
    for (size_t k = 1; k + 1 < count; ++k) {
        const Span& span = core::at(spans, core::at(order, k));
        core::at(moves, core::at(order, k)) = at - span.lo;
        at += (span.hi - span.lo) + gap;
    }
    return moves;
}

V3 edge_snap(const Aabb& moving, V3 delta, const std::vector<Aabb>& others, SnapReach snap_reach,
             std::vector<Contact>* contacts)
{
    const auto reach = snap_reach.reach;
    const auto axes = snap_reach.axes;
    V3 out = delta;
    Aabb moving_box = {.min = moving.min + delta, .max = moving.max + delta};
    constexpr float kEps = 0.5F; // (boxes that only touch don't overlap)
    auto overlaps = [&](const Aabb& other_box, int axis, float slack) {
        return axis_value(moving_box.min, axis) < axis_value(other_box.max, axis) + slack - kEps &&
               axis_value(moving_box.max, axis) > axis_value(other_box.min, axis) - slack + kEps;
    };
    for (int axis = 0; axis < 3; ++axis) {
        if ((axes & (1 << axis)) == 0) {
            continue;
        }
        float best = reach;
        float correction = 0.0F;
        float at = 0.0F;
        int found = -1;
        // A snap candidate: its correction, the face it lands on and the box it is of.
        struct Candidate {
            float c{};
            float face{};
            int index{};
        };
        auto consider = [&](Candidate candidate) {
            const auto candidate_c = candidate.c;
            const auto face = candidate.face;
            const auto index = candidate.index;
            if (std::fabs(candidate_c) <= best) {
                best = std::fabs(candidate_c);
                correction = candidate_c;
                at = face;
                found = index;
            }
        };
        for (size_t i = 0; i < others.size(); ++i) {
            const Aabb& other_box = core::at(others, i);
            const int index = static_cast<int>(i);
            if (axis != 1) {
                // (side by side: at the same height, across within reach)
                const int across = axis == 0 ? 2 : 0;
                if (!overlaps(other_box, 1, 0.0F) || !overlaps(other_box, across, reach)) {
                    continue;
                }
                consider({.c = axis_value(other_box.max, axis) - axis_value(moving_box.min, axis),
                          .face = axis_value(other_box.max, axis),
                          .index = index});
                consider({.c = axis_value(other_box.min, axis) - axis_value(moving_box.max, axis),
                          .face = axis_value(other_box.min, axis),
                          .index = index});
            } else {
                // (stacked on it: over it; level with its top: beside it, within reach)
                if (overlaps(other_box, 0, 0.0F) && overlaps(other_box, 2, 0.0F)) {
                    consider({.c = other_box.max.y - moving_box.min.y, .face = other_box.max.y, .index = index});
                }
                if (overlaps(other_box, 0, reach) && overlaps(other_box, 2, reach)) {
                    consider({.c = other_box.max.y - moving_box.max.y, .face = other_box.max.y, .index = index});
                }
            }
        }
        if (found >= 0) {
            axis_ref(out, axis) += correction;
            if (contacts) {
                contacts->push_back({.axis = axis, .at = at, .other = found});
            }
        }
    }
    return out;
}

float snap_reach(float distance) { return std::clamp(distance * kSnapReachPerUnit, kMinSnapReach, kMaxSnapReach); }

float box_segment_distance(const Aabb& box, V3 a, V3 b, V3* nearest)
{
    auto at = [&](float fraction) { return a + (b - a) * fraction; };
    auto distance = [&](float fraction) {
        const V3 point = at(fraction);
        const V3 in_box = {.x = std::clamp(point.x, box.min.x, box.max.x),
                           .y = std::clamp(point.y, box.min.y, box.max.y),
                           .z = std::clamp(point.z, box.min.z, box.max.z)};
        const V3 delta = point - in_box;
        return std::sqrt(get_dot(delta, delta));
    };
    // (the distance from a convex box along a line is convex: a ternary search finds its least)
    float lo = 0.0F;
    float hi = 1.0F;
    for (int i = 0; i < kSearchSteps; ++i) {
        const float m1 = lo + ((hi - lo) / 3.0F);
        const float m2 = hi - ((hi - lo) / 3.0F);
        if (distance(m1) <= distance(m2)) {
            hi = m2;
        } else {
            lo = m1;
        }
    }
    const float fraction = (lo + hi) * 0.5F;
    if (nearest) {
        *nearest = at(fraction);
    }
    return distance(fraction);
}

std::vector<std::string> readout_lines(const Readout& readout)
{
    std::vector<std::string> lines;
    std::array<char, kLineTextSize> text{};
    text::format_to(text, "size %.0f x %.0f x %.0f", readout.size.x, readout.size.y, readout.size.z);
    lines.emplace_back(text.data());
    if (readout.moved >= kMinMoved) {
        text::format_to(text, "moved %.0f", readout.moved);
        lines.emplace_back(text.data());
    }
    if (readout.have_ground_) {
        text::format_to(text, "above ground %.0f", readout.above);
        lines.emplace_back(text.data());
    }
    if (readout.have_rail_) {
        text::format_to(text, "%s %.0f", readout.ledge ? "ledge" : "rail", readout.rail);
        lines.emplace_back(text.data());
    }
    return lines;
}

namespace {

// How an edge is used by the triangles around it: as a top's, or a side's whose third point is below, above or
// level with it.
struct EdgeUse {
    int top = 0, down = 0, up = 0, level = 0;
};

// The triangles' points welded (within `weld` one, on a grid of that size) into `points`, and their edges by use.
std::map<std::pair<int, int>, EdgeUse> classify_edges(const std::vector<V3>& triangles, float weld,
                                                      std::vector<V3>& points)
{
    std::map<std::tuple<long, long, long>, int> ids;
    auto id = [&](V3 point) {
        auto key =
            std::make_tuple(std::lround(point.x / weld), std::lround(point.y / weld), std::lround(point.z / weld));
        auto it = ids.find(key);
        if (it != ids.end()) {
            return it->second;
        }
        const int index = static_cast<int>(points.size());
        points.push_back(point);
        ids.emplace(key, index);
        return index;
    };
    std::map<std::pair<int, int>, EdgeUse> edges;
    for (size_t triangle = 0; triangle + 2 < triangles.size(); triangle += 3) {
        const std::array<int, 3> v = {{id(core::at(triangles, triangle)), id(core::at(triangles, triangle + 1)),
                                       id(core::at(triangles, triangle + 2))}};
        if (core::at(v, 0) == core::at(v, 1) || core::at(v, 1) == core::at(v, 2) || core::at(v, 0) == core::at(v, 2)) {
            continue;
        }
        const V3 normal = perpendicular(core::at(points, core::at(v, 1)) - core::at(points, core::at(v, 0)),
                                        core::at(points, core::at(v, 2)) - core::at(points, core::at(v, 0)));
        const float length = std::sqrt(get_dot(normal, normal));
        if (length < kTinyLength) {
            continue;
        }
        const bool top = std::fabs(normal.y / length) >= kTopNormalY;
        for (int edge = 0; edge < 3; ++edge) {
            const int vertex_a = core::at(v, edge);
            const int vertex_b = core::at(v, (edge + 1) % 3);
            const int vertex_c = core::at(v, (edge + 2) % 3);
            EdgeUse& use = edges[{std::min(vertex_a, vertex_b), std::max(vertex_a, vertex_b)}];
            if (top) {
                ++use.top;
                continue;
            }
            const float dy =
                core::at(points, vertex_c).y - ((core::at(points, vertex_a).y + core::at(points, vertex_b).y) * 0.5F);
            if (dy < -1.0F) {
                ++use.down;
            } else if (dy > 1.0F) {
                ++use.up;
            } else {
                ++use.level;
            }
        }
    }
    return edges;
}

} // namespace

std::vector<Run> top_edges(const std::vector<V3>& triangles, EdgeLimits edge_limits)
{
    auto weld = edge_limits.weld;
    const auto min_length = edge_limits.min_length_;
    std::vector<Run> runs;
    if (weld <= 0.0F) {
        weld = kDefaultWeld;
    }
    // Points within `weld` one (on a grid of that size).
    std::vector<V3> points;
    const std::map<std::pair<int, int>, EdgeUse> edges = classify_edges(triangles, weld, points);
    // The lips: one upward triangle, and the surface falls away beyond it (or nothing does - an open edge).
    std::map<int, std::vector<int>> at; // (a point's lip edges)
    std::vector<std::pair<int, int>> lips;
    for (const auto& entry : edges) {
        const EdgeUse& use = entry.second;
        if (use.top != 1 || use.up > 0 || (use.down == 0 && use.level > 0)) {
            continue;
        }
        const int index = static_cast<int>(lips.size());
        lips.push_back(entry.first);
        at[entry.first.first].push_back(index);
        at[entry.first.second].push_back(index);
    }
    auto direction = [&](int from, int to) { return make_unit(core::at(points, to) - core::at(points, from)); };
    auto joins = [&](int prev, int mid, int next) {
        const float turn_cos = get_dot(direction(prev, mid), direction(mid, next));
        return turn_cos >= maths::runtime32::cos(kJoinDegrees * maths::kPi / kDegreesPerHalfTurn);
    };
    std::vector<bool> used(lips.size(), false);
    // (the next edge from `point` along the run that came from `prev`: the point's only other lip edge, turning little)
    auto next = [&](int prev, int point, int* other) {
        const std::vector<int>& list = at[point];
        if (list.size() != 2) {
            return -1;
        }
        for (const int lip_index : list) {
            if (core::at(used, lip_index)) {
                continue;
            }
            const int far_end = core::at(lips, lip_index).first == point ? core::at(lips, lip_index).second
                                                                         : core::at(lips, lip_index).first;
            if (!joins(prev, point, far_end)) {
                return -1;
            }
            *other = far_end;
            return lip_index;
        }
        return -1;
    };
    for (size_t start = 0; start < lips.size(); ++start) {
        if (core::at(used, start)) {
            continue;
        }
        core::at(used, start) = true;
        std::vector<int> chain = {core::at(lips, start).first, core::at(lips, start).second};
        // forward from the second point, then backward from the first
        for (int pass = 0; pass < 2; ++pass) {
            for (;;) {
                const int count = static_cast<int>(chain.size());
                const int prev = core::at(chain, count - 2);
                const int point = core::at(chain, count - 1);
                int other = -1;
                const int lip_index = next(prev, point, &other);
                if (lip_index < 0) {
                    break;
                }
                core::at(used, lip_index) = true;
                chain.push_back(other);
                if (other == chain.front()) {
                    break; // (a closed loop)
                }
            }
            if (pass == 0) {
                std::ranges::reverse(chain);
            }
        }
        // Straight stretches as one segment (a point kept where the run turns more than 2 degrees).
        Run run;
        run.points.push_back(core::at(points, chain.front()));
        for (size_t i = 1; i + 1 < chain.size(); ++i) {
            const V3 point_a = make_unit(core::at(points, core::at(chain, i)) - run.points.back());
            const V3 point_b =
                make_unit(core::at(points, core::at(chain, i + 1)) - core::at(points, core::at(chain, i)));
            if (get_dot(point_a, point_b) < maths::runtime32::cos(kFullTurnPerPi * maths::kPi / kDegreesPerHalfTurn)) {
                run.points.push_back(core::at(points, core::at(chain, i)));
            }
        }
        run.points.push_back(core::at(points, chain.back()));
        for (size_t i = 1; i < run.points.size(); ++i) {
            const V3 delta = core::at(run.points, i) - core::at(run.points, i - 1);
            run.length += std::sqrt(get_dot(delta, delta));
        }
        if (run.length >= min_length) {
            runs.push_back(std::move(run));
        }
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
    for (size_t run_index = 0; run_index < runs.size(); ++run_index) {
        for (size_t i = 1; i < core::at(runs, run_index).points.size(); ++i) {
            float ax = 0.0F;
            float ay = 0.0F;
            float bx = 0.0F;
            float by = 0.0F;
            if (!project(core::at(core::at(runs, run_index).points, i - 1), &ax, &ay) ||
                !project(core::at(core::at(runs, run_index).points, i), &bx, &by)) {
                continue;
            }
            const float distance = distance_to_segment({.x = x, .y = y}, {.x = ax, .y = ay}, {.x = bx, .y = by});
            if (distance <= best_d) {
                best_d = distance;
                best = static_cast<int>(run_index);
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

const std::array<V3, 3> kWorld = {
    {{.x = 1.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 1.0F, .z = 0.0F}, {.x = 0.0F, .y = 0.0F, .z = 1.0F}}};

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) ? object : nullptr;
}

void get_corners(const OrientedBox& box, std::vector<V3>* out)
{
    std::array<V3, kBoxCornerCount> corner{};
    box_corners(box.origin, box.axes, box.min, box.max, corner);
    out->insert(out->end(), corner.begin(), corner.end());
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
            if (!object) {
                continue;
            }
            Unit unit;
            unit.object = object;
            if (object_edit::model_box(object, &box)) {
                get_corners(box, &unit.corners);
            } else {
                unit.corners.push_back(object_edit::pos_of(object));
            }
            units.push_back(std::move(unit));
        }
        return units;
    }
    if (selection.kind != edit::Kind::kScenery) {
        return units;
    }
    std::vector<uint32_t> shadows;
    for (const uint32_t sector : selection.ids) {
        uint32_t caster = 0;
        if (scenery::is_shadow(sector, &caster) && std::ranges::find(selection.ids, caster) != selection.ids.end() &&
            caster != sector) {
            shadows.push_back(sector);
            continue;
        }
        Unit unit;
        unit.sectors.push_back(sector);
        units.push_back(std::move(unit));
    }
    for (const uint32_t sector : shadows) {
        uint32_t caster = 0;
        scenery::is_shadow(sector, &caster);
        for (Unit& unit : units) {
            if (unit.sectors.front() == caster) {
                unit.sectors.push_back(sector);
            }
        }
    }
    for (Unit& unit : units) {
        for (const uint32_t sector : unit.sectors) {
            if (scenery::cuboid(sector, &box)) {
                get_corners(box, &unit.corners);
            }
        }
    }
    std::erase_if(units, [](const Unit& u) { return u.corners.empty(); });
    return units;
}

// The axes the tools go along: the world's, or (Local) the first piece's own - an object's rows, a sector's quarter
// turns.
void axes_of(const std::vector<Unit>& units, bool local, std::span<V3, 3> out)
{
    for (int i = 0; i < 3; ++i) {
        core::at(out, i) = core::at(kWorld, i);
    }
    if (!local || units.empty()) {
        return;
    }
    if (const objects::GameEntity* object = units.front().object) {
        for (int i = 0; i < 3; ++i) {
            core::at(out, i) = make_unit(
                {.x = core::at(object->basis_frame().row, i).x, .y = core::at(object->basis_frame().row, i).y, .z = core::at(object->basis_frame().row, i).z});
        }
        return;
    }
    Placement placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
    scenery::placement_now(units.front().sectors.front(), &placement);
    for (int i = 0; i < 3; ++i) {
        core::at(out, i) = get_quarter_turn(core::at(kWorld, i), placement.quarters);
    }
}

// Each unit moved by its own amount along `axis` (one undo step).
void move_units(const edit::Selected& selection, const std::vector<Unit>& units, V3 axis,
                const std::vector<float>& moves)
{
    edit::get_begin();
    if (selection.kind == edit::Kind::kScenery) {
        carry::disarm(); // (each piece moves by its own amount: nothing is carried along)
        for (size_t i = 0; i < units.size(); ++i) {
            if (std::fabs(core::at(moves, i)) < kMoveEpsilon) {
                continue;
            }
            scenery::set_selection(core::at(units, i).sectors);
            scenery::move_to(scenery::get_pivot() + axis * core::at(moves, i));
        }
        scenery::set_selection(selection.ids);
    } else {
        for (size_t i = 0; i < units.size(); ++i) {
            if (core::at(units, i).object && std::fabs(core::at(moves, i)) >= kMoveEpsilon) {
                object_edit::get_place(core::at(units, i).object, object_edit::pos_of(core::at(units, i).object) + axis * core::at(moves, i));
            }
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
        hud::get_message("Align: select 2 or more items", hud::kShortMessageSeconds);
        return false;
    }
    axis = std::clamp(axis, 0, 2);
    std::array<V3, 3> axes{};
    axes_of(units, local, axes);
    std::vector<Span> spans;
    spans.reserve(units.size());
    for (const Unit& unit : units) {
        spans.push_back(span_along(unit.corners, core::at(axes, axis)));
    }
    const std::vector<float> moves = align_moves(spans, edge, reference);
    move_units(selection, units, core::at(axes, axis), moves);
    std::array<char, kReportTextSize> text{};
    text::format_to(text, "Aligned %d pieces: %s on %s (%s, %s)", static_cast<int>(units.size()), edge_name(edge),
                    get_axis_name(axis), local ? "local" : "world",
                    reference == Reference::kFirst ? "the first" : "the bounds");
    hud::get_message(text.data(), hud::kShortMessageSeconds);
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
        hud::get_message("Distribute: select 3 or more items", hud::kShortMessageSeconds);
        return false;
    }
    axis = std::clamp(axis, 0, 2);
    std::array<V3, 3> axes{};
    axes_of(units, local, axes);
    std::vector<Span> spans;
    spans.reserve(units.size());
    for (const Unit& unit : units) {
        spans.push_back(span_along(unit.corners, core::at(axes, axis)));
    }
    move_units(selection, units, core::at(axes, axis), distribute_moves(spans, gaps));
    std::array<char, kReportTextSize> text{};
    text::format_to(text, "Distributed %d pieces along %s (%s)", static_cast<int>(units.size()), get_axis_name(axis),
                    gaps ? "equal gaps" : "evenly spaced centres");
    hud::get_message(text.data(), hud::kShortMessageSeconds);
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
    for (const uint32_t sector : sectors) {
        V3 point_a{};
        V3 point_b{};
        if (!scenery::cuboid(sector, &box)) {
            continue;
        }
        bounds_of(box, &point_a, &point_b);
        grow_box(&out->min, &out->max, have, point_a, point_b);
        have = true;
    }
    return have;
}

V3 bottom_centre(const Aabb& box)
{
    return {.x = (box.min.x + box.max.x) * kHalf, .y = box.min.y, .z = (box.min.z + box.max.z) * kHalf};
}

uint32_t key_model() { return checksums::hash_from_text("model"); }

// A node's model= (empty: none).
std::string model_of(uint32_t name)
{
    const int index = nodes::get_find(name);
    script::Struct* structure = index >= 0 ? nodes::struct_of(core::at(nodes::get_all(), index)) : nullptr;
    const char* model = nullptr;
    if (!structure || !structure->fetch_text(key_model(), &model, 0) || !model) {
        return {};
    }
    return model;
}

bool replace_scenery(const edit::Selected& selection, uint32_t source)
{
    if (std::ranges::find(selection.ids, source) != selection.ids.end()) {
        hud::get_message("Replace: the replacement is part of the selection", hud::kShortMessageSeconds);
        return false;
    }
    const std::vector<Unit> units = units_of(selection);
    if (units.empty()) {
        return false;
    }
    Placement source_at = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
    scenery::placement_now(source, &source_at);
    const size_t depth = edit::deepness();
    // (the pieces hidden first: undone last, they come back selected)
    edit::push_snapshot();
    carry::disarm(); // (nothing follows: what stood on the pieces stays where it is)
    scenery::hide_selection();
    std::vector<uint32_t> made;
    for (const Unit& unit : units) {
        Aabb was;
        if (!sectors_bounds(unit.sectors, &was)) {
            continue;
        }
        Placement placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
        scenery::placement_now(unit.sectors.front(), &placement);
        const uint32_t copy = scenery::get_duplicate(source);
        if (copy == 0U) {
            continue;
        }
        scenery::set_selection({copy});
        // (its turns and scale as the piece's own: the copy is the source as it is now)
        const int quarters = (((placement.quarters - source_at.quarters) % 4) + 4) % 4;
        if (quarters != 0) {
            scenery::get_turn(quarters);
        }
        const V3 ratio = {.x = placement.scale.x / source_at.scale.x, .y = placement.scale.y / source_at.scale.y,
                          .z = placement.scale.z / source_at.scale.z};
        if (std::fabs(ratio.x - 1.0F) > kRatioEpsilon || std::fabs(ratio.y - 1.0F) > kRatioEpsilon ||
            std::fabs(ratio.z - 1.0F) > kRatioEpsilon) {
            scenery::scale_selection(turned_scale(ratio, placement.quarters), is_uniform_scale(ratio));
        }
        Aabb now;
        if (sectors_bounds({copy}, &now)) {
            scenery::move_to(scenery::get_pivot() + (bottom_centre(was) - bottom_centre(now)));
        }
        scenery::finalise();
        made.push_back(copy);
    }
    if (made.empty()) {
        edit::take_back();
        hud::get_message("Replace: that piece couldn't be copied", hud::kShortMessageSeconds);
        return false;
    }
    edit::push_made({.objects = {}, .sectors = made});
    edit::fold(depth);
    edit::choose_one({.kind = edit::Kind::kScenery, .ids = made});
    hud::get_message("Replaced " + std::to_string(made.size()) + " piece(s) with " + object_edit::caption(source) +
                         " - Ctrl+Z puts them back",
                     hud::kShortMessageSeconds);
    get_log("level editor: replaced %u scenery piece(s) with %s", static_cast<unsigned>(made.size()),
            object_edit::caption(source).c_str());
    return true;
}

bool replace_objects(const edit::Selected& selection, uint32_t source)
{
    const std::string model = model_of(source);
    std::vector<object_edit::State> copies; // (object copies: replaced by copies of the source where they were)
    std::vector<uint32_t> copy_ids;
    std::vector<uint32_t> swapped;
    for (const uint32_t id : selection.ids) {
        objects::GameEntity* object = live_object(id);
        if (!object || id == source) {
            continue;
        }
        if (object_edit::is_copy(id)) {
            copies.push_back(object_edit::store_away(object));
            copy_ids.push_back(id);
        } else {
            swapped.push_back(id);
        }
    }
    if (copies.empty() && (swapped.empty() || model.empty())) {
        hud::get_message(model.empty() ? "Replace: original object has no model to swap" : "Replace: nothing selected",
                         hud::kMessageSeconds);
        return false;
    }
    const size_t depth = edit::deepness();
    std::vector<uint32_t> made;
    if (!copies.empty()) {
        edit::remove_copies(copy_ids);
        for (const object_edit::State& was : copies) {
            const V3 pos = {.x = was.pos.x, .y = was.pos.y, .z = was.pos.z};
            const objects::GameEntity* copy = object_edit::make_copy(source, pos);
            if (!copy) {
                continue;
            }
            object_edit::State state = was;
            state.id = copy->ident();
            object_edit::get_restore(state);
            made.push_back(copy->ident());
        }
        if (!made.empty()) {
            edit::push_made({.objects = made, .sectors = {}});
        }
    }
    // (a level's own object: the source's model given to it - the Inspector's model swap)
    int swaps = 0;
    for (const uint32_t id : model.empty() ? std::vector<uint32_t>{} : swapped) {
        const std::string now = model_of(id);
        if (now.empty() || _stricmp(now.c_str(), model.c_str()) == 0) {
            continue;
        }
        entities::get_queue([id, now] { entities::detail::watch_model(id, now); });
        if (edit::set_field({.node = id, .key = key_model(), .kind = static_cast<int>(fields::Kind::kChars)}, model)) {
            ++swaps;
        }
    }
    edit::fold(depth);
    if (!made.empty()) {
        edit::choose_one({.kind = edit::Kind::kObject, .ids = made});
    }
    std::array<char, kMessageTextSize> text{};
    text::format_to(text, "Replaced %d copies; %d level objects re-modelled", static_cast<int>(made.size()), swaps);
    hud::get_message(text.data(), hud::kMessageSeconds);
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
        hud::get_message("Replace: highlight an item in Add Piece (B)", hud::kMessageSeconds);
        return false;
    }
    const edit::Selected selection = edit::get_selection();
    if (selection.kind == edit::Kind::kScenery && tab == 0) {
        return replace_scenery(selection, source);
    }
    if (selection.kind == edit::Kind::kObject && tab == 1) {
        return replace_objects(selection, source);
    }
    const char* message = "Replace: nothing selected";
    if (selection.kind == edit::Kind::kScenery) {
        message = "Replace: choose a scenery piece";
    } else if (selection.kind == edit::Kind::kObject) {
        message = "Replace: choose an object";
    }
    hud::get_message(message, hud::kMessageSeconds);
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
    Aabb nearby;
    std::vector<Aabb>* out{};
};

void gather_sector(void* context, uint32_t checksum, render::WorldCell* /*unused*/)
{
    auto* gather = static_cast<GatherContext*>(context);
    if (std::ranges::find(*gather->skip, checksum) != gather->skip->end() || scenery::get_hidden(checksum)) {
        return;
    }
    OrientedBox box{};
    if (!scenery::cuboid(checksum, &box)) {
        return;
    }
    Aabb aabb;
    bounds_of(box, &aabb.min, &aabb.max);
    // (not the ground or a whole street; only near the piece)
    if (!sector_size_movable(aabb.min, aabb.max, kMaxMovableSize)) {
        return;
    }
    if (aabb.max.x < gather->nearby.min.x || aabb.min.x > gather->nearby.max.x || aabb.max.y < gather->nearby.min.y ||
        aabb.min.y > gather->nearby.max.y || aabb.max.z < gather->nearby.min.z || aabb.min.z > gather->nearby.max.z) {
        return;
    }
    gather->out->push_back(aabb);
}

} // namespace

V3 edge_snapped(V3 pos, V3 wanted, int axes)
{
    if (!at_hand().edge_snap_) {
        return wanted;
    }
    const edit::Selected selection = edit::get_selection();
    if (selection.kind != edit::Kind::kScenery) {
        return wanted;
    }
    Aabb moving;
    if (!sectors_bounds(selection.ids, &moving)) {
        return wanted;
    }
    const uint32_t now = SDL_GetTicks();
    if (stored_snap().selection != selection.ids || now - stored_snap().last_used_ > kSnapRefreshMilliseconds) {
        // (the neighbours, once a drag: within 1500 of where the piece is - they don't move meanwhile)
        stored_snap().selection = selection.ids;
        stored_snap().boxes.clear();
        GatherContext context = {
            .skip = &selection.ids, .nearby = {.min = moving.min - V3{.x = kSnapNeighbourhood, .y = kSnapNeighbourhood, .z = kSnapNeighbourhood}, .max = moving.max + V3{.x = kSnapNeighbourhood, .y = kSnapNeighbourhood, .z = kSnapNeighbourhood}}, .out = &stored_snap().boxes};
        scenery::for_each_level_sector(gather_sector, &context);
    }
    stored_snap().last_used_ = now;
    float distance = 0.0F;
    ViewNow view{};
    if (current_view(&view)) {
        const V3 delta = pos - view.pos;
        distance = std::sqrt(get_dot(delta, delta));
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
    const render::Geom* geom = scenery::sector_geometry(checksum);
    if (!geom) {
        return;
    }
    const scenery::GeomMeshes meshes = scenery::meshes_of(geom);
    for (uint32_t i = 0; i < meshes.count && i < kMaxMeshesRead && out->size() < kMaxTrianglesRead; ++i) {
        render::MeshData* mesh = meshes.at(i);
        if (!mesh || mesh->billboard() || ((mesh->flags() & kMeshSharedFlags) != 0U)) {
            continue;
        }
        render::VertexBufferData* vertices =
            (mesh->vertex_buffer_count() != 0U) ? core::at(mesh->vertex_buffers(), 0) : nullptr;
        const render::IndexBufferData* indices = core::at(mesh->index_buffers(), 0);
        if (!vertices || !vertices->data() || !indices || !indices->indices() || mesh->stride() < kPositionBytes) {
            continue;
        }
        auto at = [&](uint16_t corner) {
            const auto view = gpu::VertexView(vertices->data(), mesh->stride(), mesh->vertex_count()).float3(corner);
            return V3{.x = core::at(view, 0), .y = core::at(view, 1), .z = core::at(view, 2)};
        };
        const std::span<const uint16_t> index(indices->indices(), core::at(mesh->index_counts(), 0));
        for (uint32_t k = 2; k < index.size(); ++k) {
            const uint16_t index_a = core::at(index, k - 2);
            const uint16_t index_b = core::at(index, k - 1);
            const uint16_t index_c = core::at(index, k);
            if (index_a == index_b || index_b == index_c || index_a == index_c || index_a >= mesh->vertex_count() ||
                index_b >= mesh->vertex_count() || index_c >= mesh->vertex_count()) {
                continue;
            }
            out->push_back(at(index_a));
            out->push_back(at(index_b));
            out->push_back(at(index_c));
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
            for (const uint32_t sector : selection.ids) {
                if (!scenery::is_shadow(sector)) {
                    sector_triangles(sector, &triangles);
                }
            }
            level_editor_arrange_state().edges_.runs = top_edges(triangles, {.weld = kDefaultWeld, .min_length_ = kMinEdgeLength});
            if (level_editor_arrange_state().edges_.runs.size() > kMaxEdgeRuns) {
                level_editor_arrange_state().edges_.runs.resize(kMaxEdgeRuns);
            }
        }
    }
    return level_editor_arrange_state().edges_.runs;
}

bool pixel_of(const ViewNow& view, V3 point, float* x, float* y)
{
    const TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    float nx = 0.0F;
    float ny = 0.0F;
    if (!cast_onto(view.pos, frame, view.lens, point, view.near_z_, &nx, &ny)) {
        return false;
    }
    ndc_to_pixel(view.viewport, nx, ny, x, y);
    return true;
}

} // namespace

bool edge_picking() { return stored_edge_picking(); }

void set_edge_picking(bool on)
{
    if (on && edit::get_selection().kind != edit::Kind::kScenery) {
        hud::get_message("Rail Along Edge: select a scenery piece", hud::kShortMessageSeconds);
        on = false;
    }
    stored_edge_picking() = on;
    level_editor_arrange_state().edge_hover_ = -1;
    if (on) {
        const size_t runs = edge_runs().size();
        hud::get_message((runs != 0U) ? "Rail Along Edge: click a top edge (Shift: more)"
                                      : "Rail Along Edge: no top edges found",
                         hud::kMessageSeconds);
        if (runs == 0U) {
            stored_edge_picking() = false;
        }
    }
}

bool edge_click(maths::Vec2 at)
{
    const float x = at.x;
    const float y = at.y;
    if (!stored_edge_picking()) {
        return false;
    }
    ViewNow view{};
    if (!current_view(&view)) {
        return false;
    }
    const std::vector<Run>& runs = edge_runs();
    const int pick = run_at(
        runs, [&](V3 point, float* px, float* py) { return pixel_of(view, point, px, py); }, {.x = x, .y = y},
        12.0F * static_cast<float>(core::at(view.viewport, 3)) / 720.0F);
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
        hud::get_message(message.empty() ? std::string("Rail along an edge: couldn't add the rail") : message, hud::kShortMessageSeconds);
        return true;
    }
    const input::KeyState keys = input::keys();
    const bool more = (core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U);
    std::array<char, kReportTextSize> text{};
    text::format_to(text, "Rail added: %d points, %.0f long", static_cast<int>(run.points.size()), run.length);
    // (Shift: the piece stays selected for the next edge; else the rail's first point is selected)
    edit::chain_changed(&before, more ? 0 : select, text.data());
    if (more) {
        edit::choose_one(selection);
    } else {
        stored_edge_picking() = false;
    }
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
    std::vector<Segment> segments_;
    uint32_t segments_nodes_ = ~0U;
    uint32_t segments_rails_ = ~0U;
};

Segments& segments()
{
    static core::NoDestroy<Segments> state;
    return state.get();
}

const std::vector<Segment>& current_segments()
{
    if (segments().segments_nodes_ == nodes::get_generation() &&
        segments().segments_rails_ == scenery::rail_generation()) {
        return segments().segments_;
    }
    segments().segments_nodes_ = nodes::get_generation();
    segments().segments_rails_ = scenery::rail_generation();
    segments().segments_.clear();
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    auto place = [&](const nodes::Elem& elem, V3* point) {
        if (scenery::get_rail_point(elem.name, point, true)) {
            return true;
        }
        *point = elem.pos;
        return elem.has_pos_;
    };
    for (const nodes::Elem& elem : nodes) {
        const bool ledge = elem.family == nodes::Family::kLadder && chains::is_ledge_node(elem.name);
        if (elem.hidden || (elem.family != nodes::Family::kRail && !ledge)) {
            continue;
        }
        V3 point_a{};
        if (!place(elem, &point_a)) {
            continue;
        }
        for (const int link : elem.links) {
            if (link < 0 || std::cmp_greater_equal(link, nodes.size())) {
                continue;
            }
            V3 point_b{};
            if (place(core::at(nodes, link), &point_b)) {
                segments().segments_.push_back(
                    {.a = point_a, .b = point_b, .from = elem.name, .to = core::at(nodes, link).name, .ledge = ledge});
            }
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
            if (!object || !object_edit::model_box(object, &oriented)) {
                continue;
            }
            V3 point_a{};
            V3 point_b{};
            bounds_of(oriented, &point_a, &point_b);
            if (!have && selection.ids.size() == 1) {
                *size = box_size(oriented);
            }
            grow_box(&box->min, &box->max, have, point_a, point_b);
            have = true;
        }
        if (have && selection.ids.size() != 1) {
            *size = box->max - box->min;
        }
        return have;
    }
    if (selection.kind == edit::Kind::kScenery && sectors_bounds(selection.ids, box)) {
        *size = box->max - box->min;
        return true;
    }
    return false;
}

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
    actions::ActStep act_step;
    act_step.id = id;
    act_step.name = name;
    act_step.category = category;
    act_step.tip = tip;
    act_step.kind = checked ? actions::Kind::kFlipOver : actions::Kind::kPress;
    act_step.defaults = act_step.keys = std::move(keys);
    act_step.run = std::move(run);
    act_step.checked = std::move(checked);
    return actions::enrol(act_step);
}

void flip_over(bool* setting) { *setting = !*setting; }

void register_actions()
{
    if (arrange_actions().act_window_ >= 0) {
        return;
    }
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
        if (actions::squeezed(arrange_actions().act_window_) && !ui::underway()) {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        } else if (actions::squeezed(arrange_actions().act_window_)) {
            flip_over(&at_hand().window);
        }
        if (actions::squeezed(arrange_actions().act_adjust_)) {
            flip_over(&adjust::get_shown());
        }
        if (actions::squeezed(arrange_actions().act_edge_rail_)) {
            set_edge_picking(!stored_edge_picking());
        }
        if (actions::squeezed(arrange_actions().act_linked_)) {
            links::linked_duplicate();
        }
    }
    if (stored_edge_picking() && edit::get_selection().kind != edit::Kind::kScenery) {
        stored_edge_picking() = false; // (deselected: Esc stops it)
    }
    const bool dragging = edit::get_dragging();
    if (dragging && !arrange_drag().was_dragging_) {
        arrange_drag().drag_from_ = edit::get_pos();
    }
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
    bool get_pixel(V3 point, float* x, float* y) const
    {
        float nx = 0.0F;
        float ny = 0.0F;
        if (!cast_onto(camera, frame, lens, point, near_z_, &nx, &ny)) {
            return false;
        }
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    }
    void get_seg(V3 a, V3 b, uint32_t argb, float thickness = 1.0F) const
    {
        if (!clip_to_near(camera, frame, near_z_ + kNearPlaneMargin, &a, &b)) {
            return;
        }
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        if (get_pixel(a, &ax, &ay) && get_pixel(b, &bx, &by)) {
            overlay->lines.push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
        }
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
        std::array<V3, kBoxCornerCount> corner{};
        box_corners(box.origin, box.axes, box.min, box.max, corner);
        for (int i = 0; i < kBoxCorners; ++i) {
            for (int bit = 1; bit < kBoxCorners; bit <<= 1) {
                if ((i & bit) == 0) {
                    get_seg(core::at(corner, i), core::at(corner, i | bit), argb, kBoxLineWidth);
                }
            }
        }
        if (fill == 0U) {
            return;
        }
        // (its six faces, see-through: two triangles each - only when all four corners are in front)
        static const std::array<std::array<int, 4>, 6> kFaces = {{{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                         {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}}};
        for (const auto& face : kFaces) {
            std::array<float, 4> x{};
            std::array<float, 4> y{};
            bool ok = true;
            for (int k = 0; k < 4 && ok; ++k) {
                ok = get_pixel(core::at(corner, core::at(face, k)), &core::at(x, k), &core::at(y, k));
            }
            if (!ok) {
                continue;
            }
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
void get_marker(const Painter& painter, V3 at, MarkerLook marker_look)
{
    const auto yaw = marker_look.yaw;
    const auto colour = marker_look.colour;
    const uint32_t fill_colour = (colour & 0x00FFFFFF) | 0xC0000000;
    painter.get_seg(at, at + V3{.x = 0.0F, .y = kHeadHeight, .z = 0.0F}, fill_colour, kMarkerWidth);
    constexpr int kSegments = 24;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = 6.2831853F * static_cast<float>(i) / kSegments;
        const float a1 = 6.2831853F * static_cast<float>(i + 1) / kSegments;
        painter.get_seg(at + V3{.x = maths::runtime32::sin(a0) * kHeadingRadius, .y = 1.0F, .z = maths::runtime32::cos(a0) * kHeadingRadius},
                        at + V3{.x = maths::runtime32::sin(a1) * kHeadingRadius, .y = 1.0F, .z = maths::runtime32::cos(a1) * kHeadingRadius},
                        fill_colour, kBoxLineWidth);
    }
    const V3 heading = {.x = maths::runtime32::sin(yaw) * 50.0F, .y = 1.0F, .z = maths::runtime32::cos(yaw) * 50.0F};
    painter.get_seg(at + V3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, at + heading, fill_colour, kMarkerWidth);
}

// The surface's normal under the cursor (false: none).
bool surface_normal(V3* normal)
{
    V3 origin{};
    V3 dir{};
    if (!edit::get_ray({.origin = &origin, .dir = &dir})) {
        return false;
    }
    snap::SurfaceHit hit;
    if (!snap::get_surface(origin, make_unit(dir), kSurfaceReach, {}, &hit)) {
        return false;
    }
    *normal = hit.normal;
    return true;
}

// The box turned so its up axis lies along `normal` (its heading kept) about the bottom-centre point `at`.
void tilt_box(OrientedBox* box, SurfaceHit surface)
{
    const auto at = surface.point;
    const auto normal = surface.normal;
    std::array<V3, 3> rows = {{core::at(box->axes, 0), core::at(box->axes, 1), core::at(box->axes, 2)}};
    if (!snap::align_rows(rows, normal)) {
        return;
    }
    // (each corner's offset from `at`, in the box's frame, put back along the new rows)
    const V3 origin = box->origin - at;
    std::array<float, 3> lengths{};
    for (int i = 0; i < 3; ++i) {
        core::at(lengths, i) = std::max(kMinAxisLength, get_dot(core::at(box->axes, i), core::at(box->axes, i)));
    }
    const V3 local = {.x = get_dot(origin, core::at(box->axes, 0)) / core::at(lengths, 0), .y = get_dot(origin, core::at(box->axes, 1)) / core::at(lengths, 1),
                      .z = get_dot(origin, core::at(box->axes, 2)) / core::at(lengths, 2)};
    for (int i = 0; i < 3; ++i) {
        core::at(box->axes, i) = core::at(rows, i);
    }
    box->origin = at + core::at(rows, 0) * local.x + core::at(rows, 1) * local.y + core::at(rows, 2) * local.z;
}

void add_ghost(const Painter& painter)
{
    if (!at_hand().ghost || edit::get_dragging()) {
        return;
    }
    V3 origin{};
    V3 dir{};
    if (!edit::get_ray({.origin = &origin, .dir = &dir})) {
        return;
    }
    int tab = 0;
    uint32_t id = 0;
    uint32_t colour = 0;
    const int armed = edit::place_armed(&colour);
    const V3 at = edit::cursor_point();
    constexpr uint32_t kLine = 0xD080E0FF;
    constexpr uint32_t kFill = 0x2880E0FF;
    if (armed >= 0) {
        get_marker(painter, at, {.yaw = edit::get_yaw(), .colour = (colour != 0U) ? colour : kMarkerColour});
        return;
    }
    if (!edit::browser_item(&tab, &id)) {
        return;
    }
    OrientedBox box{};
    if (tab == 0) {
        if (!scenery::cuboid(id, &box)) {
            return;
        }
        Aabb bounds;
        bounds_of(box, &bounds.min, &bounds.max);
        box.origin = box.origin + (at - bottom_centre(bounds));
        painter.cuboid(box, {.argb = kLine, .fill = kFill});
        return;
    }
    objects::GameEntity* object = live_object(id);
    if (!object || !object_edit::model_box(object, &box)) {
        get_marker(painter, at, {.yaw = edit::get_yaw(), .colour = kMarkerColour});
        return;
    }
    box.origin = box.origin + (at - object_edit::pos_of(object));
    V3 normal{};
    if (at_hand().ghost_slope_ && surface_normal(&normal)) {
        tilt_box(&box, {.point = at, .normal = normal});
    }
    painter.cuboid(box, {.argb = kLine, .fill = kFill});
}

void add_readout(const Painter& painter)
{
    if (!at_hand().measure || !edit::get_dragging()) {
        return;
    }
    const edit::Selected selection = edit::get_selection();
    Readout readout;
    Aabb box;
    const bool have_box = selection_box(selection, &box, &readout.size);
    const V3 moved = edit::get_pos() - arrange_drag().drag_from_;
    readout.moved = std::sqrt(get_dot(moved, moved));
    V3 base{};
    V3 ground{};
    if (edit::ground_mark({.base = &base, .ground = &ground})) {
        readout.have_ground_ = true;
        readout.above = base.y - ground.y;
    }
    V3 nearest = {};
    V3 from = {};
    if (have_box) {
        float best = kNearestReach;
        for (const Segment& segment : current_segments()) {
            if (selection.kind == edit::Kind::kScenery && scenery::carries_rail(segment.from) &&
                scenery::carries_rail(segment.to)) {
                continue; // (its own rails move with it)
            }
            // (quickly: a segment far off its box passed)
            const Aabb grown = {.min = box.min - V3{.x = best, .y = best, .z = best}, .max = box.max + V3{.x = best, .y = best, .z = best}};
            const V3 lo = {.x = std::min(segment.a.x, segment.b.x),
                           .y = std::min(segment.a.y, segment.b.y),
                           .z = std::min(segment.a.z, segment.b.z)};
            const V3 hi = {.x = std::max(segment.a.x, segment.b.x),
                           .y = std::max(segment.a.y, segment.b.y),
                           .z = std::max(segment.a.z, segment.b.z)};
            if (hi.x < grown.min.x || lo.x > grown.max.x || hi.y < grown.min.y || lo.y > grown.max.y ||
                hi.z < grown.min.z || lo.z > grown.max.z) {
                continue;
            }
            V3 on{};
            const float distance = box_segment_distance(box, segment.a, segment.b, &on);
            if (distance < best) {
                best = distance;
                nearest = on;
                readout.have_rail_ = true;
                readout.rail = distance;
                readout.ledge = segment.ledge;
            }
        }
        if (readout.have_rail_) {
            from = {.x = std::clamp(nearest.x, box.min.x, box.max.x), .y = std::clamp(nearest.y, box.min.y, box.max.y),
                    .z = std::clamp(nearest.z, box.min.z, box.max.z)};
            painter.get_seg(from, nearest, kContactColour, 1.0F);
        }
    }
    const auto [x, y] = edit::get_cursor();
    const float line = 17.0F * static_cast<float>(core::at(painter.viewport, 3)) / 720.0F;
    std::vector<std::string> lines = readout_lines(readout);
    if (!have_box) {
        lines.erase(lines.begin()); // (a node: no size)
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        painter.draw_label(x + (kReadoutX * line / kReadoutLineHeight),
                           y + (kReadoutY * line / kReadoutLineHeight) + (line * static_cast<float>(i)), kReadoutColour,
                           core::at(lines, i));
    }
}

void add_contacts(const Painter& painter)
{
    if (!edit::get_dragging() || SDL_GetTicks() - stored_snap().contacts_at_ > kContactMilliseconds) {
        return;
    }
    for (const Contact& contact : stored_snap().contacts) {
        if (contact.other < 0 || std::cmp_greater_equal(contact.other, stored_snap().boxes.size())) {
            continue;
        }
        // (the face met: the neighbour's rectangle on that plane where the two boxes meet)
        const Aabb& aabb = core::at(stored_snap().boxes, contact.other);
        const Aabb& moving_box = stored_snap().moving;
        const int u = contact.axis == 0 ? 1 : 0;
        const int v = contact.axis == 2 ? 1 : 2;
        float u0 = std::max(axis_value(aabb.min, u), axis_value(moving_box.min, u));
        float u1 = std::min(axis_value(aabb.max, u), axis_value(moving_box.max, u));
        float v0 = std::max(axis_value(aabb.min, v), axis_value(moving_box.min, v));
        float v1 = std::min(axis_value(aabb.max, v), axis_value(moving_box.max, v));
        if (u1 < u0) { // (beside it, not over it: the neighbour's whole face)
            u0 = axis_value(aabb.min, u), u1 = axis_value(aabb.max, u);
        }
        if (v1 < v0) {
            v0 = axis_value(aabb.min, v), v1 = axis_value(aabb.max, v);
        }
        std::array<V3, 4> corner{};
        const std::array<float, 4> us = {{u0, u1, u1, u0}};
        const std::array<float, 4> vs = {{v0, v0, v1, v1}};
        for (int k = 0; k < 4; ++k) {
            axis_ref(core::at(corner, k), contact.axis) = contact.at;
            axis_ref(core::at(corner, k), u) = core::at(us, k);
            axis_ref(core::at(corner, k), v) = core::at(vs, k);
        }
        for (int k = 0; k < 4; ++k) {
            painter.get_seg(core::at(corner, k), core::at(corner, (k + 1) % 4), kSnapBoxColour, kSnapBoxWidth);
        }
    }
}

void add_edges(const Painter& painter)
{
    if (!stored_edge_picking()) {
        return;
    }
    const std::vector<Run>& runs = edge_runs();
    const auto [x, y] = edit::get_cursor();
    level_editor_arrange_state().edge_hover_ = run_at(
        runs, [&](V3 world_point, float* px, float* py) { return painter.get_pixel(world_point, px, py); },
        {.x = x, .y = y}, kEdgePickReach * static_cast<float>(core::at(painter.viewport, 3)) / kReferenceViewHeight);
    for (size_t run_index = 0; run_index < runs.size(); ++run_index) {
        const bool lit = ints::as_signed(run_index) == level_editor_arrange_state().edge_hover_;
        for (size_t i = 1; i < core::at(runs, run_index).points.size(); ++i) {
            painter.get_seg(core::at(core::at(runs, run_index).points, i - 1),
                            core::at(core::at(runs, run_index).points, i), lit ? kLitEdgeColour : kUnlitEdgeColour,
                            lit ? kLitEdgeWidth : kUnlitEdgeWidth);
        }
        if (lit) {
            std::array<char, kEdgeTextSize> text{};
            text::format_to(text, "rail here: %.0f long", core::at(runs, run_index).length);
            float px = 0.0F;
            float py = 0.0F;
            if (painter.get_pixel(core::at(runs, run_index).points.front(), &px, &py)) {
                painter.draw_label(px + kEdgeLabelOffsetX, py - kEdgeLabelRise, kLitEdgeColour, text.data());
            }
        }
    }
}

} // namespace

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 ui::Overlay* overlay)
{
    const Painter painter = {
        .camera = camera, .frame = frame, .lens = lens, .viewport = viewport, .near_z_ = near_z, .overlay = overlay};
    add_ghost(painter);
    add_contacts(painter);
    add_readout(painter);
    add_edges(painter);
}

bool place_click(bool more)
{
    int tab = 0;
    uint32_t id = 0;
    if (!at_hand().click_places_ || !edit::browser_item(&tab, &id)) {
        return false;
    }
    const V3 at = edit::cursor_point();
    edit::place_browser_item(at, more);
    // (an object stood square to the surface's slope - the ghost showed it so)
    V3 normal{};
    const edit::Selected placed = edit::get_selection();
    if (tab == 1 && at_hand().ghost_slope_ && placed.kind == edit::Kind::kObject && surface_normal(&normal)) {
        for (const uint32_t pid : placed.ids) {
            objects::GameEntity* object = live_object(pid);
            if (!object) {
                continue;
            }
            for (maths::Mat4* matrix : {&object->basis_frame(), &object->display_matrix()}) {
                std::array<V3, 3> rows{};
                for (int i = 0; i < 3; ++i) {
                    core::at(rows, i) = {.x = core::at(matrix->row, i).x,
                                         .y = core::at(matrix->row, i).y,
                                         .z = core::at(matrix->row, i).z};
                }
                if (!snap::align_rows(rows, normal)) {
                    continue;
                }
                for (int i = 0; i < 3; ++i) {
                    core::at(matrix->row, i).x = core::at(rows, i).x;
                    core::at(matrix->row, i).y = core::at(rows, i).y;
                    core::at(matrix->row, i).z = core::at(rows, i).z;
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
    segments().segments_nodes_ = segments().segments_rails_ = ~0U;
    segments().segments_.clear();
}

} // namespace frontend::level_editor::arrange
