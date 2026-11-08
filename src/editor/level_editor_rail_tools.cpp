// NOT ORIGINAL - the level editor's rail tools: the pure parts, Rail from Edges, snapping and joining rail ends (ours).
// See level_editor_rail_tools.h; Auto Rails is level_editor_auto_rails.cpp.

#include "editor/level_editor_rail_tools.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_auto_rails.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_ui_app.h"
#include "engine/pad_input/input_source.h"
#include "engine/render/coll_line.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "imgui.h"
#include "platform/run_log.h"

namespace frontend::level_editor::rail_tools {

// --- the pure parts ------------------------------------------------------------------------------------------------

namespace {

constexpr float kTiny = 1e-6F;
constexpr float kDegreesPerRadian = 57.29578F;
// The weld grid's cells packed into one key: 21 bits an axis, offset to keep them positive.
constexpr int kCellBits = 21;
constexpr int64_t kCellOffset = int64_t{1} << (kCellBits - 1);
constexpr uint64_t kCellMask = (uint64_t{1} << kCellBits) - 1;

// Points within `weld` made one (a grid of `weld` cells: a point's match is in its cell or one next to it).
class Welder {
public:
    explicit Welder(float weld) : weld_(std::max(weld, kTiny)) {}
    int index_of(V3 point)
    {
        const int64_t cx = cell(point.x);
        const int64_t cy = cell(point.y);
        const int64_t cz = cell(point.z);
        for (int64_t dx = -1; dx <= 1; ++dx) {
            for (int64_t dy = -1; dy <= 1; ++dy) {
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    const auto found = cells_.find(key(cx + dx, cy + dy, cz + dz));
                    if (found == cells_.end()) {
                        continue;
                    }
                    for (const int index : found->second) {
                        const V3 delta = core::at(points_, index) - point;
                        if (get_dot(delta, delta) <= weld_ * weld_) {
                            return index;
                        }
                    }
                }
            }
        }
        const int index = static_cast<int>(points_.size());
        points_.push_back(point);
        cells_[key(cx, cy, cz)].push_back(index);
        return index;
    }
    [[nodiscard]] const std::vector<V3>& points() const { return points_; }

private:
    [[nodiscard]] int64_t cell(float value) const { return static_cast<int64_t>(std::floor(value / weld_)); }
    static uint64_t key(int64_t x, int64_t y, int64_t z)
    {
        auto bits = [](int64_t value) { return static_cast<uint64_t>(value + kCellOffset) & kCellMask; };
        return (bits(x) << (2 * kCellBits)) | (bits(y) << kCellBits) | bits(z);
    }

    float weld_;
    std::unordered_map<uint64_t, std::vector<int>> cells_;
    std::vector<V3> points_;
};

float length_of(V3 v) { return std::sqrt(get_dot(v, v)); }

V3 unit_or_zero(V3 v)
{
    const float length = length_of(v);
    return length > kTiny ? v * (1.0F / length) : V3{};
}

} // namespace

std::vector<int> weld_points(const std::vector<V3>& points, float weld, std::vector<V3>* welded)
{
    Welder welder(weld);
    std::vector<int> index;
    index.reserve(points.size());
    for (const V3 point : points) {
        index.push_back(welder.index_of(point));
    }
    *welded = welder.points();
    return index;
}

float segment_distance(V3 point, V3 a, V3 b)
{
    const V3 along = b - a;
    const float span = get_dot(along, along);
    float fraction = span > kTiny ? get_dot(point - a, along) / span : 0.0F;
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    return length_of(point - (a + along * fraction));
}

bool same_edge(const Edge& a, const Edge& b, float weld)
{
    auto near = [weld](V3 first, V3 second) {
        const V3 delta = first - second;
        return get_dot(delta, delta) <= weld * weld;
    };
    return (near(a.a, b.a) && near(a.b, b.b)) || (near(a.a, b.b) && near(a.b, b.a));
}

int nearest_side(std::span<const V3, 3> corners, V3 point, unsigned allowed)
{
    int best = 0;
    float best_distance = -1.0F;
    for (int side = 0; side < 3; ++side) {
        if (allowed != 0U && (allowed & (1U << static_cast<unsigned>(side))) == 0U) {
            continue;
        }
        const float distance = segment_distance(point, core::at(corners, side), core::at(corners, (side + 1) % 3));
        if (best_distance < 0.0F || distance < best_distance) {
            best_distance = distance;
            best = side;
        }
    }
    return best;
}

std::vector<Run> join_edges(const std::vector<Edge>& edges, float weld)
{
    // The edges by their welded ends (each pair once), and the edges at each point.
    struct Joint {
        int a;
        int b;
        int edge;
    };
    Welder welder(weld);
    std::vector<Joint> joints;
    std::map<std::pair<int, int>, bool> seen;
    for (size_t i = 0; i < edges.size(); ++i) {
        const Edge& edge = core::at(edges, i);
        if (length_of(edge.b - edge.a) < weld) {
            continue;
        }
        const int from = welder.index_of(edge.a);
        const int to = welder.index_of(edge.b);
        if (from == to || !seen.emplace(std::minmax(from, to), true).second) {
            continue;
        }
        joints.push_back({.a = from, .b = to, .edge = static_cast<int>(i)});
    }
    const std::vector<V3>& points = welder.points();
    std::vector<std::vector<int>> at(points.size());
    for (size_t i = 0; i < joints.size(); ++i) {
        core::at(at, core::at(joints, i).a).push_back(static_cast<int>(i));
        core::at(at, core::at(joints, i).b).push_back(static_cast<int>(i));
    }
    std::vector<uint8_t> used(joints.size(), 0);
    // From a point along a joint, on through points with two joints, until an end, a junction or a joint used.
    auto walk = [&](int start, int first) {
        Run run;
        run.points.push_back(core::at(points, start));
        int point = start;
        int joint = first;
        while (true) {
            core::at(used, joint) = 1;
            const Joint& here = core::at(joints, joint);
            run.edges.push_back(here.edge);
            const int next = here.a == point ? here.b : here.a;
            run.length += length_of(core::at(points, next) - core::at(points, point));
            run.points.push_back(core::at(points, next));
            point = next;
            const std::vector<int>& around = core::at(at, point);
            if (around.size() != 2) {
                break;
            }
            const int onward = core::at(around, 0) == joint ? core::at(around, 1) : core::at(around, 0);
            if (core::at(used, onward) != 0U) {
                break;
            }
            joint = onward;
        }
        if (point == start && run.edges.size() >= 3) {
            run.points.pop_back(); // (back where it began: a loop)
            run.closed = true;
        }
        return run;
    };
    std::vector<Run> runs;
    for (size_t point = 0; point < points.size(); ++point) {
        if (core::at(at, point).size() == 2) {
            continue;
        }
        for (const int joint : core::at(at, point)) {
            if (core::at(used, joint) == 0U) {
                runs.push_back(walk(static_cast<int>(point), joint));
            }
        }
    }
    for (size_t joint = 0; joint < joints.size(); ++joint) {
        if (core::at(used, joint) == 0U) {
            runs.push_back(walk(core::at(joints, joint).a, static_cast<int>(joint)));
        }
    }
    return runs;
}

std::vector<V3> drop_straight(const std::vector<V3>& points, bool closed, float degrees)
{
    const size_t count = points.size();
    if (count < 3) {
        return points;
    }
    const float least_turn = std::cos(degrees / kDegreesPerRadian);
    std::vector<V3> kept = {points.front()};
    const size_t last = closed ? count : count - 1;
    for (size_t i = 1; i < last; ++i) {
        const V3 here = core::at(points, i);
        const V3 next = core::at(points, (i + 1) % count);
        if (get_dot(unit_or_zero(here - kept.back()), unit_or_zero(next - here)) < least_turn) {
            kept.push_back(here);
        }
    }
    if (!closed) {
        kept.push_back(points.back());
    } else if (kept.size() > 3) {
        // (a loop's first point: kept only where the loop turns)
        const V3 first = kept.front();
        if (get_dot(unit_or_zero(first - kept.back()), unit_or_zero(core::at(kept, 1) - first)) >= least_turn) {
            kept.erase(kept.begin());
        }
    }
    return kept;
}

std::span<const char* const> terrain_names()
{
    static constexpr auto kNames = std::to_array<const char*>({
        "TERRAIN_DEFAULT",          "TERRAIN_CONCSMOOTH",        "TERRAIN_CONCROUGH",
        "TERRAIN_METALSMOOTH",      "TERRAIN_METALROUGH",        "TERRAIN_METALCORRUGATED",
        "TERRAIN_METALGRATING",     "TERRAIN_METALTIN",          "TERRAIN_WOOD",
        "TERRAIN_WOODMASONITE",     "TERRAIN_WOODPLYWOOD",       "TERRAIN_WOODFLIMSY",
        "TERRAIN_WOODSHINGLE",      "TERRAIN_WOODPIER",          "TERRAIN_BRICK",
        "TERRAIN_TILE",             "TERRAIN_ASPHALT",           "TERRAIN_ROCK",
        "TERRAIN_GRAVEL",           "TERRAIN_SIDEWALK",          "TERRAIN_GRASS",
        "TERRAIN_GRASSDRIED",       "TERRAIN_DIRT",              "TERRAIN_DIRTPACKED",
        "TERRAIN_WATER",            "TERRAIN_ICE",               "TERRAIN_SNOW",
        "TERRAIN_SAND",             "TERRAIN_PLEXIGLASS",        "TERRAIN_FIBERGLASS",
        "TERRAIN_CARPET",           "TERRAIN_CONVEYOR",          "TERRAIN_CHAINLINK",
        "TERRAIN_METALFUTURE",      "TERRAIN_GENERIC1",          "TERRAIN_GENERIC2",
        "TERRAIN_WHEELS",           "TERRAIN_WETCONC",           "TERRAIN_METALFENCE",
        "TERRAIN_GRINDTRAIN",       "TERRAIN_GRINDROPE",         "TERRAIN_GRINDWIRE",
        "TERRAIN_GRINDCONC",        "TERRAIN_GRINDROUNDMETALPOLE", "TERRAIN_GRINDCHAINLINK",
        "TERRAIN_GRINDMETAL",       "TERRAIN_GRINDWOODRAILING",  "TERRAIN_GRINDWOODLOG",
        "TERRAIN_GRINDWOOD",        "TERRAIN_GRINDPLASTIC",      "TERRAIN_GRINDELECTRICWIRE",
        "TERRAIN_GRINDCABLE",       "TERRAIN_GRINDCHAIN",        "TERRAIN_GRINDPLASTICBARRIER",
        "TERRAIN_GRINDNEONLIGHT",   "TERRAIN_GRINDGLASSMONSTER", "TERRAIN_GRINDBANYONTREE",
        "TERRAIN_GRINDBRASSRAIL",   "TERRAIN_GRINDCATWALK",      "TERRAIN_GRINDTANKTURRET",
    });
    return kNames;
}

const char* grind_terrain_for(const std::string& terrain)
{
    const std::string_view name = terrain;
    if (name.starts_with("TERRAIN_GRIND")) {
        for (const char* known : terrain_names()) {
            if (name == known) {
                return known;
            }
        }
    }
    if (name.find("CHAINLINK") != std::string_view::npos) {
        return "TERRAIN_GRINDCHAINLINK";
    }
    if (name.starts_with("TERRAIN_METAL")) {
        return "TERRAIN_GRINDMETAL";
    }
    if (name.starts_with("TERRAIN_WOOD")) {
        return "TERRAIN_GRINDWOOD";
    }
    if (name == "TERRAIN_PLEXIGLASS" || name == "TERRAIN_FIBERGLASS") {
        return "TERRAIN_GRINDPLASTIC";
    }
    return "TERRAIN_GRINDCONC";
}

uint32_t most_common(const std::vector<uint32_t>& values)
{
    std::map<uint32_t, int> counts;
    uint32_t best = 0;
    int best_count = 0;
    for (const uint32_t value : values) {
        const int count = ++counts[value];
        if (count > best_count) {
            best_count = count;
            best = value;
        }
    }
    return best;
}

bool passes(const AutoEdge& auto_edge, const Limits& limits)
{
    return auto_edge.degrees >= limits.degrees && auto_edge.slope <= limits.max_slope_ && auto_edge.clear &&
           auto_edge.drop && !auto_edge.railed;
}

std::vector<Run> auto_runs(const std::vector<AutoEdge>& edges, const Limits& limits)
{
    std::vector<Edge> kept;
    for (const AutoEdge& auto_edge : edges) {
        if (passes(auto_edge, limits)) {
            kept.push_back(auto_edge.edge);
        }
    }
    std::vector<Run> runs = join_edges(kept, kWeld);
    std::erase_if(runs, [&](const Run& run) { return run.length < limits.min_length_; });
    // (the edges' indices into `edges`, not into the passing ones)
    std::vector<int> index;
    for (size_t i = 0; i < edges.size(); ++i) {
        if (passes(core::at(edges, i), limits)) {
            index.push_back(static_cast<int>(i));
        }
    }
    for (Run& run : runs) {
        for (int& edge : run.edges) {
            edge = core::at(index, edge);
        }
    }
    return runs;
}

// --- the game side ---------------------------------------------------------------------------------------------------

namespace {

constexpr float kRayLength = 20000.0F;       // how far the mouse ray reaches
constexpr float kFlatNormalDot = 0.999F;      // two faces this alike lie in one plane (a quad's diagonal between them)
constexpr float kNearestEndSquared = 0.0001F; // a dragged point this near the snap ring is on it
constexpr double kHintSeconds = 6.0;          // how long a tool's hint stays
constexpr float kRingPixels = 9.0F;           // the snap ring's radius
constexpr float kEndDotPixels = 3.0F;         // a hovered edge's ends
constexpr float kLineThickness = 3.0F;        // a hovered / picked edge's line
constexpr float kPanelRightInset = 12.0F;     // the panel's gap from the view's right edge, at scale 1
constexpr float kPanelTopOffset = 60.0F;      // its gap from the view's top, at scale 1
constexpr float kPanelWidth = 300.0F;         // its width, at scale 1
constexpr size_t kTextSize = 160;
constexpr uint32_t kHoverColour = 0xFFFFFFFF;
constexpr uint32_t kPickedColour = 0xFF40E0FF;
constexpr uint32_t kRingColour = 0xFF60FF60;

struct EdgesTool {
    bool on = false;
    std::vector<Edge> picked;
    bool have_hover_ = false;
    Edge hover;
    bool join = true; // (the rails made join free ends near theirs)
    std::vector<std::function<void()>> queue;
};
// A dragged rail end over another's free end (target 0: none), and the undo depth before the drag's step.
struct EndSnap {
    uint32_t node = 0;
    uint32_t target = 0;
    V3 at = {};
    size_t depth = 0;
};
struct DrawRing {
    bool have = false;
    V3 at = {};
};
// NOT ORIGINAL grouping - the rail tools' state. Made on first use, never destroyed (tools()).
struct RailToolsState {
    EdgesTool edges;
    EndSnap snap;
    DrawRing ring;
    int act_edges_ = -1;
    int act_join_ = -1;
    int act_auto_ = -1;
};

RailToolsState& tools()
{
    static core::NoDestroy<RailToolsState> state;
    return state.get();
}

bool alt_held()
{
    const input::KeyState keys = input::keys();
    return keys.size() > SDL_SCANCODE_RALT &&
           ((core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U));
}

bool shift_held()
{
    const input::KeyState keys = input::keys();
    return keys.size() > SDL_SCANCODE_RSHIFT &&
           ((core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U));
}

V3 to_v3(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }

bool face_corners(const render::TriMeshSource* tri, int face, std::array<V3, 3>* corners)
{
    for (int k = 0; k < 3; ++k) {
        const uint32_t index = tri->face_vertex(face, k);
        if (index >= tri->num_vertices()) {
            return false;
        }
        core::at(*corners, k) = to_v3(tri->read_stored_vertex(static_cast<int>(index)));
    }
    return true;
}

V3 face_normal(const std::array<V3, 3>& corners)
{
    return unit_or_zero(perpendicular(core::at(corners, 1) - core::at(corners, 0), core::at(corners, 2) - core::at(corners, 0)));
}

// Whether side `side` of `face` lies flat between it and another face of the same plane (sharing its two corners).
bool flat_side(const render::TriMeshSource* tri, int face, int side, V3 normal)
{
    const uint32_t first = tri->face_vertex(face, side);
    const uint32_t second = tri->face_vertex(face, (side + 1) % 3);
    for (int other = 0; std::cmp_less(other, tri->tri_count()); ++other) {
        if (other == face) {
            continue;
        }
        bool has_first = false;
        bool has_second = false;
        for (int k = 0; k < 3; ++k) {
            const uint32_t index = tri->face_vertex(other, k);
            has_first = has_first || index == first;
            has_second = has_second || index == second;
        }
        std::array<V3, 3> corners{};
        if (has_first && has_second && face_corners(tri, other, &corners) &&
            get_dot(face_normal(corners), normal) > kFlatNormalDot) {
            return true;
        }
    }
    return false;
}

// The edge of the collision under the ray: the face the game's collision ray meets and its side nearest the hit.
bool edge_under(V3 origin, V3 unit_dir, Edge* edge)
{
    render::ProbeRay ray;
    const V3 to = origin + unit_dir * kRayLength;
    const maths::Vec4 start = {.x = origin.x, .y = origin.y, .z = origin.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray.get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const render::CollisionHit& hit = ray.hit();
    if (!render::is_static_collider(hit.object) || hit.object->geometry() == nullptr) {
        return false; // (a movable object's: not the level's)
    }
    const render::TriMeshSource* tri = hit.object->geometry();
    const int face = static_cast<int>(hit.face);
    std::array<V3, 3> corners{};
    if (tri->faces() == nullptr || tri->vertices() == nullptr || face < 0 || std::cmp_greater_equal(face, tri->tri_count()) ||
        !face_corners(tri, face, &corners)) {
        return false;
    }
    const V3 normal = face_normal(corners);
    unsigned allowed = 0;
    for (int side = 0; side < 3; ++side) {
        if (!flat_side(tri, face, side, normal)) {
            allowed |= 1U << static_cast<unsigned>(side);
        }
    }
    const int side = nearest_side(corners, to_v3(hit.point), allowed);
    edge->a = core::at(corners, side);
    edge->b = core::at(corners, (side + 1) % 3);
    edge->terrain = hit.terrain;
    edge->sector = hit.object_checksum_;
    return true;
}

// A terrain byte's name ("" when the level's terrains have none of that index).
std::string terrain_name(uint8_t index)
{
    for (const char* name : terrain_names()) {
        uint8_t value = 0;
        if (chains::terrain_index_of(name, &value) && value == index) {
            return name;
        }
    }
    return "";
}

void set_edges_on(bool on)
{
    EdgesTool& tool = tools().edges;
    tool.on = on;
    tool.have_hover_ = false;
    if (!on) {
        tool.picked.clear();
        return;
    }
    if (chains::get_drawing() != chains::Blit::kNone) {
        chains::stop_drawing();
    }
    show_message("Rail from Edges: click an edge (Shift: add more), Enter: make the rail", kHintSeconds);
}

} // namespace

// The rails the picked edges make (points lifted, terrain and cluster from their faces).
std::vector<chains::NewRail> rails_from(const std::vector<Edge>& edges, const std::vector<Run>& runs)
{
    std::vector<chains::NewRail> rails;
    std::map<uint8_t, std::string> names;
    for (const Run& run : runs) {
        chains::NewRail rail;
        rail.closed = run.closed;
        for (const V3 point : drop_straight(run.points, run.closed, kStraightDegrees)) {
            rail.points.push_back(point + V3{.x = 0.0F, .y = kRailLift, .z = 0.0F});
        }
        std::vector<uint32_t> terrains;
        std::vector<uint32_t> sectors;
        for (const int index : run.edges) {
            terrains.push_back(core::at(edges, index).terrain);
            sectors.push_back(core::at(edges, index).sector);
        }
        const auto terrain = static_cast<uint8_t>(most_common(terrains));
        if (!names.contains(terrain)) {
            names[terrain] = terrain_name(terrain);
        }
        rail.terrain = grind_terrain_for(names[terrain]);
        const clusters::Membership membership = clusters::get_now(most_common(sectors));
        rail.cluster = membership.trick ? membership.cluster : 0;
        rails.push_back(rail);
    }
    return rails;
}

namespace {

void create_from_edges()
{
    EdgesTool& tool = tools().edges;
    if (tool.picked.empty()) {
        show_message("Rail from Edges: pick an edge first");
        return;
    }
    const std::vector<Run> runs = join_edges(tool.picked, kWeld);
    const std::vector<chains::NewRail> rails = rails_from(tool.picked, runs);
    const chains::State before = chains::at_hand();
    chains::RailsMade made;
    std::string message;
    if (!chains::add_rails(rails, tool.join ? kNewRailJoinReach : 0.0F, &made, &message)) {
        show_message("Rail from Edges: " + message);
        return;
    }
    std::array<char, kTextSize> text{};
    text::format_to(text, "Rail from Edges: %d rail%s, %d points%s", made.rails, made.rails == 1 ? "" : "s",
                    made.points, made.joined > 0 ? ", joined to nearby rails" : "");
    edit::chain_changed(&before, made.first, text.data());
    get_log("level editor: rail from edges - %d edges, %d rails, %d points, %d ends joined (%s)",
            static_cast<int>(tool.picked.size()), made.rails, made.points, made.joined,
            rails.empty() ? "" : rails.front().terrain.c_str());
    tool.picked.clear();
}

bool edges_click(V3 origin, V3 unit_dir)
{
    EdgesTool& tool = tools().edges;
    Edge edge;
    if (!edge_under(origin, unit_dir, &edge)) {
        show_message("Rail from Edges: no collision under the cursor");
        return true;
    }
    const auto found =
        std::ranges::find_if(tool.picked, [&](const Edge& picked) { return same_edge(picked, edge, kWeld); });
    if (found != tool.picked.end()) {
        tool.picked.erase(found);
    } else if (shift_held()) {
        tool.picked.push_back(edge);
    } else {
        tool.picked = {edge};
    }
    std::array<char, kTextSize> text{};
    text::format_to(text, "Rail from Edges: %d edge%s picked (Enter: make)", static_cast<int>(tool.picked.size()),
                    tool.picked.size() == 1 ? "" : "s");
    show_message(text.data());
    return true;
}

// Join Rails: the selected rail's free end nearest another free end within reach, joined to it.
void join_selected()
{
    const edit::Selected selection = edit::get_selection();
    if (selection.kind != edit::Kind::kElem || selection.ids.empty() ||
        nodes::get_find(selection.ids.front()) < 0 ||
        core::at(nodes::get_all(), nodes::get_find(selection.ids.front())).family != nodes::Family::kRail) {
        show_message("Join Rails: select a rail point");
        return;
    }
    const std::vector<uint32_t> own = chains::chain_of(selection.ids.front());
    const std::vector<chains::FreeEnd> ends = chains::free_ends();
    float best = kJoinRailsReach * kJoinRailsReach;
    uint32_t moved = 0;
    uint32_t target = 0;
    for (const chains::FreeEnd& mine : ends) {
        if (std::ranges::find(own, mine.name) == own.end()) {
            continue;
        }
        for (const chains::FreeEnd& other : ends) {
            const bool same_rail = std::ranges::find(own, other.name) != own.end();
            if (other.name == mine.name || (same_rail && own.size() < 3)) {
                continue;
            }
            const V3 delta = other.pos - mine.pos;
            const float distance = get_dot(delta, delta);
            if (distance < best) {
                best = distance;
                moved = mine.name;
                target = other.name;
            }
        }
    }
    if (target == 0U) {
        show_message("Join Rails: no rail end within 64 of this rail's ends");
        return;
    }
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    if (!chains::join_ends(moved, target, best <= kMergeReach * kMergeReach, &message, &select)) {
        show_message("Join Rails: " + message);
        return;
    }
    edit::chain_changed(&before, select, message);
    get_log("level editor: join rails - %08X onto %08X (%.1f apart)", moved, target,
            static_cast<double>(std::sqrt(best)));
}

void edges_panel(float view_x, float view_y, float view_width, float scale)
{
    EdgesTool& tool = tools().edges;
    if (!tool.on) {
        return;
    }
    bool open = true;
    const char* title = "Rail from Edges###railFromEdges";
    if (ui::app::place_floating(title, false)) {
        ImGui::SetNextWindowPos(
            ImVec2(view_x + view_width - (kPanelRightInset * scale), view_y + (kPanelTopOffset * scale)),
            ImGuiCond_Appearing, ImVec2(1.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth * scale, 0.0F), ImGuiCond_Appearing);
    }
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        const std::vector<Run> runs = join_edges(tool.picked, kWeld);
        gui::text("%d edge%s picked - %d rail%s", static_cast<int>(tool.picked.size()),
                  tool.picked.size() == 1 ? "" : "s", static_cast<int>(runs.size()), runs.size() == 1 ? "" : "s");
        gui::text_disabled("Click: pick an edge. Shift+click: add.");
        gui::text_disabled("Click a picked edge to drop it.");
        ImGui::Checkbox("Join nearby rail ends", &tool.join);
        ImGui::Separator();
        if (ImGui::Button("Create Rail")) {
            tool.queue.emplace_back([] { create_from_edges(); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            tool.queue.emplace_back([] { tools().edges.picked.clear(); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Done")) {
            tool.queue.emplace_back([] { set_edges_on(false); });
        }
    }
    ImGui::End();
    if (!open) {
        tool.queue.emplace_back([] { set_edges_on(false); });
    }
}

int add_action(const char* id, const char* name, const char* tip, std::vector<actions::Chord> keys,
               std::function<void()> run)
{
    actions::ActStep act_step;
    act_step.id = id;
    act_step.name = name;
    act_step.category = "Chains";
    act_step.tip = tip;
    act_step.kind = actions::Kind::kPress;
    act_step.defaults = act_step.keys = std::move(keys);
    act_step.run = std::move(run);
    return actions::enrol(act_step);
}

} // namespace

void prepare_all()
{
    if (tools().act_edges_ >= 0) {
        return;
    }
    tools().act_edges_ =
        add_action("rail_edges", "Rail from Edges", "Click edges of the level to make a rail along them",
                   {actions::Chord{.scancode = SDL_SCANCODE_E, .mods = actions::kAlt}},
                   [] { set_edges_on(!tools().edges.on); });
    actions::set_checked(tools().act_edges_, [] { return tools().edges.on ? 1 : 0; });
    tools().act_join_ = add_action("join_rails", "Join Rails", "Join the selected rail to the nearest rail end",
                                   {actions::Chord{.scancode = SDL_SCANCODE_J, .mods = actions::kCtrl}},
                                   [] { join_selected(); });
    tools().act_auto_ = add_action("auto_rails", "Auto Rails...",
                                   "Experimental: find grindable edges and add rails along them", {},
                                   [] { auto_rails::set_open(!auto_rails::get_open()); });
    actions::set_checked(tools().act_auto_, [] { return auto_rails::get_open() ? 1 : 0; });
}

void kickoff()
{
    tools().edges = EdgesTool{};
    tools().snap = EndSnap{};
    tools().ring = DrawRing{};
    auto_rails::set_open(false);
}

void halt()
{
    kickoff();
    auto_rails::halt();
}

void tick_frame()
{
    // (the actions' keys run them through the registry: Alt+E, Ctrl+J)
    for (const int action : {tools().act_edges_, tools().act_join_}) {
        if (action >= 0 && actions::squeezed(action)) {
            if (const actions::ActStep* step = actions::get_pull(action); step && step->run) {
                step->run();
            }
        }
    }
    if (tools().edges.on && actions::squeezed(actions::kChainFinish) && !tools().edges.picked.empty()) {
        create_from_edges();
    }
    std::vector<std::function<void()>> queue;
    queue.swap(tools().edges.queue);
    for (auto& action : queue) {
        action();
    }
    auto_rails::tick_frame();
}

bool occupied() { return tools().edges.on || auto_rails::get_open(); }

bool get_cancel()
{
    if (tools().edges.on) {
        set_edges_on(false);
        show_message("Rail from Edges off");
        return true;
    }
    if (auto_rails::get_open()) {
        auto_rails::set_open(false);
        return true;
    }
    return false;
}

bool picking() { return tools().edges.on; }

void get_hover(V3 origin, V3 unit_dir, bool have_ray)
{
    EdgesTool& tool = tools().edges;
    tool.have_hover_ = tool.on && have_ray && edge_under(origin, unit_dir, &tool.hover);
    // (drawing a chain: the end a click would link onto - not with Alt)
    tools().ring.have = false;
    if (have_ray && chains::get_drawing() != chains::Blit::kNone && chains::get_drawing() != chains::Blit::kLadder &&
        !alt_held()) {
        V3 point{};
        uint32_t end = 0;
        render::ProbeRay ray;
        const V3 to = origin + unit_dir * kRayLength;
        const maths::Vec4 start = {.x = origin.x, .y = origin.y, .z = origin.z, .w = 1.0F};
        const maths::Vec4 finish = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
        if (ray.get_collision_along(&start, &finish, false) != 0) {
            point = to_v3(ray.hit().point);
            end = chains::end_near(point);
        }
        if (end != 0U) {
            tools().ring = {.have = true, .at = chains::point_of(end)};
        }
    }
}

bool get_click(V3 origin, V3 unit_dir)
{
    if (tools().edges.on) {
        return edges_click(origin, unit_dir);
    }
    return false;
}

void add_overlay(const overlays::Painter& painter)
{
    const EdgesTool& tool = tools().edges;
    if (tool.on) {
        for (const Edge& edge : tool.picked) {
            (void)painter.get_seg(edge.a, edge.b, kPickedColour, kLineThickness);
        }
        if (tool.have_hover_) {
            (void)painter.get_seg(tool.hover.a, tool.hover.b, kHoverColour, kLineThickness);
            painter.get_dot(tool.hover.a, kEndDotPixels, kHoverColour);
            painter.get_dot(tool.hover.b, kEndDotPixels, kHoverColour);
        }
    }
    if (tools().ring.have) {
        painter.get_dot(tools().ring.at, kRingPixels, kRingColour, false);
        painter.draw_label(tools().ring.at, kRingColour, "Join");
    }
    if (tools().snap.target != 0U && edit::get_dragging()) {
        painter.get_dot(tools().snap.at, kRingPixels, kRingColour, false);
        painter.draw_label(tools().snap.at, kRingColour, "Snap (Alt: off)");
    }
    auto_rails::add_overlay(painter);
}

void get_windows(float view_x, float view_y, float view_width, float scale)
{
    edges_panel(view_x, view_y, view_width, scale);
    auto_rails::window(view_x, view_y, view_width, scale);
}

std::string describe()
{
    std::string text;
    if (tools().edges.on) {
        text = "rail from edges: " + std::to_string(tools().edges.picked.size()) + " edges picked";
    }
    const std::string automatic = auto_rails::describe();
    if (!automatic.empty()) {
        text += std::string(text.empty() ? "" : ", ") + automatic;
    }
    return text;
}

V3 end_snap(uint32_t node, V3 wanted, bool entire)
{
    EndSnap& snap = tools().snap;
    snap.target = 0;
    if (entire || alt_held() || !chains::is_free_end(node)) {
        return wanted;
    }
    const std::vector<uint32_t> own = chains::chain_of(node);
    float best = kEndSnapReach * kEndSnapReach;
    for (const chains::FreeEnd& end : chains::free_ends()) {
        // (its own rail's other end closes a loop - of three points at least)
        if (end.name == node || (std::ranges::find(own, end.name) != own.end() && own.size() < 4)) {
            continue;
        }
        const V3 delta = end.pos - wanted;
        const float distance = get_dot(delta, delta);
        if (distance < best) {
            best = distance;
            snap.target = end.name;
            snap.at = end.pos;
        }
    }
    if (snap.target == 0U) {
        return wanted;
    }
    // (the undo stack's depth now: the drag's Move step goes on it when it is let go - constant while dragging)
    snap.depth = edit::deepness();
    snap.node = node;
    return snap.at;
}

void drag_done()
{
    const EndSnap snap = tools().snap;
    tools().snap = EndSnap{};
    node_edit::Values values;
    if (snap.target == 0U || !node_edit::get_take_in(snap.node, &values)) {
        return;
    }
    const V3 delta = values.pos - snap.at;
    if (get_dot(delta, delta) > kNearestEndSquared) {
        return; // (not where it snapped: the drag was put back)
    }
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    if (!chains::join_ends(snap.node, snap.target, true, &message, &select)) {
        show_message(message);
        return;
    }
    edit::chain_changed(&before, select, message);
    edit::fold(snap.depth); // (one Ctrl+Z: the join and the move)
    get_log("level editor: rail end %08X snapped onto %08X and joined", snap.node, snap.target);
}

} // namespace frontend::level_editor::rail_tools
