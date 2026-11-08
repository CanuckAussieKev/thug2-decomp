// NOT ORIGINAL - the level editor's Auto Rails: the scan, the preview and its window (ours). See
// level_editor_auto_rails.h.

#include "editor/level_editor_auto_rails.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/enum_ops.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "editor/level_editor.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_rail_tools.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_ui_app.h"
#include "engine/gfx/sector.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "imgui.h"
#include "platform/run_log.h"

namespace frontend::level_editor::rail_tools::auto_rails {

namespace {

constexpr float kDegreesPerRadian = 57.29578F;
constexpr float kTiny = 1e-6F;
constexpr float kOpenEdgeDegrees = 90.0F; // an edge with one face counts as this sharp
constexpr float kConcaveRise = 0.5F;      // the other face's corner this far above the upper face's plane: concave
constexpr float kProbeLift = 1.0F;        // the probes start this far above the edge
constexpr float kDropOut = 3.0F;          // the drop probe goes down this far out from the edge
constexpr float kParallelDot = 0.9F;      // a rail this parallel to an edge runs along it
constexpr float kLeastDegrees = 10.0F;    // the scan keeps edges at least this sharp (the slider's least)
constexpr float kMostSlope = 60.0F;       // ... and at most this steep (the slider's most)
constexpr float kRailGridCell = 64.0F;    // the rails' grid for the railed check
constexpr int kMaxProbedEdges = 60000;    // edges probed at most (the rest left out)
constexpr float kDashOn = 8.0F;           // the preview's dashes and gaps (units)
constexpr float kDashOff = 6.0F;
constexpr int kMaxDashes = 8000;          // dashes drawn a frame at most
constexpr float kPreviewReach = 6000.0F;  // runs farther than this from the camera aren't drawn
constexpr float kPreviewThickness = 2.0F;
constexpr uint32_t kPreviewColour = 0xFFFFC040;
constexpr float kPanelRightInset = 12.0F;
constexpr float kPanelTopOffset = 60.0F;
constexpr float kPanelWidth = 340.0F;
constexpr float kRadiusLeast = 250.0F;
constexpr float kRadiusMost = 8000.0F;
constexpr float kAngleMost = 150.0F;
constexpr float kLengthLeast = 8.0F;
constexpr float kLengthMost = 1000.0F;
constexpr float kHeightProbe = 24.0F; // the level rails' height check looks down this far
constexpr size_t kTextSize = 160;
constexpr uint32_t kTerrainByte = 0xFFU; // (a face's terrain: its low byte, as the game reads it)
constexpr unsigned kHighHalf = 32U;     // a key's high 32 bits

enum class Area : uint8_t { kCamera, kSelection, kLevel };

struct Face {
    std::array<V3, 3> v{};
    V3 normal{};
    uint8_t terrain = 0;
    uint32_t sector = 0;
};

// NOT ORIGINAL grouping - Auto Rails' window and scan. Made on first use, never destroyed (auto_state()).
struct AutoState {
    bool open = false;
    int area = 0; // (Area)
    float radius = 1500.0F;
    Limits limits{.degrees = 45.0F, .min_length_ = 48.0F, .max_slope_ = 30.0F};
    bool scanned = false;
    int faces = 0;
    bool capped = false;
    std::vector<AutoEdge> edges;
    std::vector<Run> runs;
    int points = 0;
    float length = 0.0F;
    std::string message;
    std::vector<std::function<void()>> queue;
};

AutoState& auto_state()
{
    static core::NoDestroy<AutoState> state;
    return state.get();
}

V3 to_v3(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }
float length_of(V3 v) { return std::sqrt(get_dot(v, v)); }
V3 unit_or_zero(V3 v)
{
    const float length = length_of(v);
    return length > kTiny ? v * (1.0F / length) : V3{};
}

bool ray_hits(V3 from, V3 to)
{
    render::ProbeRay ray;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    return ray.get_collision_along(&start, &end, false) != 0;
}

bool camera_pos(V3* pos)
{
    ViewNow view{};
    if (!current_view(&view)) {
        return false;
    }
    *pos = view.pos;
    return true;
}

// What the scan gathers faces from: the area, the camera, the selection; the faces found.
struct Gather {
    Area area;
    V3 centre;
    float radius;
    std::vector<uint32_t> selection;
    std::vector<Face>* faces;
    bool capped;
};

void gather_sector(void* context, uint32_t checksum, render::WorldCell* sector)
{
    auto* gather = static_cast<Gather*>(context);
    const render::HitMesh* coll = sector->hit_body();
    if (!render::is_static_collider(coll) || (coll->flag_bits() & 4U) != 0U || coll->geometry() == nullptr) {
        return; // (none, or switched off)
    }
    if (gather->area == Area::kSelection && std::ranges::find(gather->selection, checksum) == gather->selection.end()) {
        return;
    }
    const render::TriMeshSource* tri = coll->geometry();
    if (tri->faces() == nullptr || tri->vertices() == nullptr) {
        return;
    }
    if (gather->area == Area::kCamera) {
        const V3 min = to_v3(tri->bounds().min);
        const V3 max = to_v3(tri->bounds().max);
        const V3 nearest = {.x = std::clamp(gather->centre.x, min.x, max.x),
                            .y = std::clamp(gather->centre.y, min.y, max.y),
                            .z = std::clamp(gather->centre.z, min.z, max.z)};
        if (length_of(nearest - gather->centre) > gather->radius) {
            return;
        }
    }
    for (int face = 0; std::cmp_less(face, tri->tri_count()); ++face) {
        if (static_cast<int>(gather->faces->size()) >= kMaxFaces) {
            gather->capped = true;
            return;
        }
        if (core::any(tri->face_flags(face) & render::kTriggerVolumeFaceFlags)) {
            continue;
        }
        Face out{};
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            const uint32_t index = tri->face_vertex(face, k);
            ok = index < tri->num_vertices();
            if (ok) {
                core::at(out.v, k) = to_v3(tri->read_stored_vertex(static_cast<int>(index)));
            }
        }
        const V3 cross = perpendicular(core::at(out.v, 1) - core::at(out.v, 0), core::at(out.v, 2) - core::at(out.v, 0));
        if (!ok || length_of(cross) <= kTiny) {
            continue;
        }
        if (gather->area == Area::kCamera) {
            const V3 middle = (core::at(out.v, 0) + core::at(out.v, 1) + core::at(out.v, 2)) * (1.0F / 3.0F);
            if (length_of(middle - gather->centre) > gather->radius) {
                continue;
            }
        }
        out.normal = unit_or_zero(cross);
        out.terrain = static_cast<uint8_t>(tri->face_terrain(face) & kTerrainByte);
        out.sector = checksum;
        gather->faces->push_back(out);
    }
}

// The rails there are, in a grid of the ground (x, z) for the railed check.
class RailGrid {
public:
    explicit RailGrid(std::vector<chains::RailSegment> segments) : segments_(std::move(segments))
    {
        for (size_t i = 0; i < segments_.size(); ++i) {
            const chains::RailSegment& segment = core::at(segments_, i);
            const int x0 = cell(std::min(segment.a.x, segment.b.x) - kRailedReach);
            const int x1 = cell(std::max(segment.a.x, segment.b.x) + kRailedReach);
            const int z0 = cell(std::min(segment.a.z, segment.b.z) - kRailedReach);
            const int z1 = cell(std::max(segment.a.z, segment.b.z) + kRailedReach);
            for (int cx = x0; cx <= x1; ++cx) {
                for (int cz = z0; cz <= z1; ++cz) {
                    cells_[key(cx, cz)].push_back(static_cast<int>(i));
                }
            }
        }
    }
    // A rail within kRailedReach of the edge's middle, parallel to it.
    [[nodiscard]] bool railed(V3 middle, V3 along) const
    {
        const auto found = cells_.find(key(cell(middle.x), cell(middle.z)));
        if (found == cells_.end()) {
            return false;
        }
        return std::ranges::any_of(found->second, [&](int index) {
            const chains::RailSegment& segment = core::at(segments_, index);
            return segment_distance(middle, segment.a, segment.b) <= kRailedReach &&
                   std::fabs(get_dot(unit_or_zero(segment.b - segment.a), along)) >= kParallelDot;
        });
    }

private:
    static int cell(float value) { return static_cast<int>(std::floor(value / kRailGridCell)); }
    static uint64_t key(int x, int z)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << kHighHalf) | static_cast<uint32_t>(z);
    }

    std::vector<chains::RailSegment> segments_;
    std::unordered_map<uint64_t, std::vector<int>> cells_;
};

// What one edge of the scan is (false: not a candidate at the loosest limits).
struct EdgeFaces {
    int a = 0;
    int b = 0;
    std::vector<int> faces;
};

bool classify(const std::vector<Face>& faces, const std::vector<V3>& points, const EdgeFaces& edge_faces,
              AutoEdge* out)
{
    // (the face looking up most: the edge's upper face)
    int upper = -1;
    for (const int face : edge_faces.faces) {
        if (upper < 0 || core::at(faces, face).normal.y > core::at(faces, upper).normal.y) {
            upper = face;
        }
    }
    if (upper < 0 || core::at(faces, upper).normal.y < kTopNormalY) {
        return false;
    }
    const Face& top = core::at(faces, upper);
    const V3 from = core::at(points, edge_faces.a);
    const V3 to = core::at(points, edge_faces.b);
    const V3 along = to - from;
    const float flat = std::sqrt((along.x * along.x) + (along.z * along.z));
    const float slope = std::atan2(std::fabs(along.y), flat) * kDegreesPerRadian;
    if (slope > kMostSlope) {
        return false;
    }
    float degrees = kOpenEdgeDegrees;
    if (edge_faces.faces.size() > 1) {
        // (the other face turning away most)
        int other = -1;
        for (const int face : edge_faces.faces) {
            if (face != upper && (other < 0 || get_dot(core::at(faces, face).normal, top.normal) <
                                                   get_dot(core::at(faces, other).normal, top.normal))) {
                other = face;
            }
        }
        const Face& side = core::at(faces, other);
        degrees = std::acos(std::clamp(get_dot(side.normal, top.normal), -1.0F, 1.0F)) * kDegreesPerRadian;
        for (const V3 corner : side.v) {
            if (get_dot(top.normal, corner - from) > kConcaveRise) {
                return false; // (it rises above the upper face: a floor meeting a wall)
            }
        }
    }
    if (degrees < kLeastDegrees) {
        return false;
    }
    out->edge = {.a = from, .b = to, .terrain = top.terrain, .sector = top.sector};
    out->degrees = degrees;
    out->slope = slope;
    return true;
}

// The probes: nothing above, the ground falling away beside it (on the side away from the upper face), no rail along.
void probe(const Face& top, const RailGrid& rails, AutoEdge* edge)
{
    const V3 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const V3 middle = (edge->edge.a + edge->edge.b) * 0.5F;
    const V3 along = unit_or_zero(edge->edge.b - edge->edge.a);
    V3 out = unit_or_zero(perpendicular(along, up));
    const V3 centre = (core::at(top.v, 0) + core::at(top.v, 1) + core::at(top.v, 2)) * (1.0F / 3.0F);
    if (get_dot(out, centre - middle) > 0.0F) {
        out = out * -1.0F;
    }
    edge->clear = !ray_hits(middle + up * kProbeLift, middle + up * kClearance);
    const V3 beside = middle + out * kDropOut + up * kProbeLift;
    edge->drop = !ray_hits(beside, beside - up * (kProbeLift + kMinDrop));
    edge->railed = rails.railed(middle, along);
}

// The level's rails' height over the collision below them (logged once a scan: what kRailLift was chosen from).
void log_rail_heights()
{
    std::vector<float> heights;
    for (const chains::FreeEnd& end : chains::free_ends()) {
        render::ProbeRay ray;
        const maths::Vec4 start = {.x = end.pos.x, .y = end.pos.y, .z = end.pos.z, .w = 1.0F};
        const maths::Vec4 finish = {.x = end.pos.x, .y = end.pos.y - kHeightProbe, .z = end.pos.z, .w = 1.0F};
        if (ray.get_collision_along(&start, &finish, false) != 0) {
            heights.push_back(end.pos.y - ray.hit().point.y);
        }
    }
    if (heights.empty()) {
        return;
    }
    std::ranges::sort(heights);
    get_log("level editor: auto rails - rail ends over the collision below: median %.2f (%d of them)",
            static_cast<double>(core::at(heights, heights.size() / 2)), static_cast<int>(heights.size()));
}

void refresh_runs()
{
    AutoState& state = auto_state();
    state.runs = auto_runs(state.edges, state.limits);
    state.points = 0;
    state.length = 0.0F;
    for (const Run& run : state.runs) {
        state.points += static_cast<int>(drop_straight(run.points, run.closed, kStraightDegrees).size());
        state.length += run.length;
    }
}

void scan()
{
    AutoState& state = auto_state();
    state.edges.clear();
    state.runs.clear();
    state.scanned = false;
    std::vector<Face> faces;
    Gather gather{.area = static_cast<Area>(state.area),
                  .centre = {},
                  .radius = state.radius,
                  .selection = scenery::get_selection(),
                  .faces = &faces,
                  .capped = false};
    if (gather.area == Area::kCamera && !camera_pos(&gather.centre)) {
        return;
    }
    if (gather.area == Area::kSelection && gather.selection.empty()) {
        state.message = "Select scenery first";
        return;
    }
    scenery::for_each_level_sector(gather_sector, &gather);
    state.faces = static_cast<int>(faces.size());
    state.capped = gather.capped;
    // (the corners welded, each edge with its faces)
    std::vector<V3> corners;
    corners.reserve(faces.size() * 3);
    for (const Face& face : faces) {
        corners.insert(corners.end(), face.v.begin(), face.v.end());
    }
    std::vector<V3> points;
    const std::vector<int> index = weld_points(corners, kWeld, &points);
    std::unordered_map<uint64_t, EdgeFaces> edges;
    for (size_t face = 0; face < faces.size(); ++face) {
        for (size_t k = 0; k < 3; ++k) {
            const int from = core::at(index, (face * 3) + k);
            const int to = core::at(index, (face * 3) + ((k + 1) % 3));
            if (from == to) {
                continue;
            }
            const auto [low, high] = std::minmax(from, to);
            const uint64_t key = (static_cast<uint64_t>(low) << kHighHalf) | static_cast<uint32_t>(high);
            EdgeFaces& entry = edges[key];
            entry.a = low;
            entry.b = high;
            entry.faces.push_back(static_cast<int>(face));
        }
    }
    const RailGrid rails(chains::rail_segments());
    int probed = 0;
    for (const auto& entry : edges) {
        AutoEdge edge;
        if (probed >= kMaxProbedEdges || !classify(faces, points, entry.second, &edge)) {
            continue;
        }
        int upper = entry.second.faces.front();
        for (const int face : entry.second.faces) {
            if (core::at(faces, face).normal.y > core::at(faces, upper).normal.y) {
                upper = face;
            }
        }
        probe(core::at(faces, upper), rails, &edge);
        ++probed;
        state.edges.push_back(edge);
    }
    state.capped = state.capped || probed >= kMaxProbedEdges;
    state.scanned = true;
    refresh_runs();
    std::array<char, kTextSize> text{};
    text::format_to(text, "%d faces, %d edges looked at", state.faces, static_cast<int>(state.edges.size()));
    state.message = text.data();
    const char* area = "whole level";
    if (state.area == static_cast<int>(Area::kCamera)) {
        area = "around the camera";
    } else if (state.area == static_cast<int>(Area::kSelection)) {
        area = "selection";
    }
    get_log("level editor: auto rails - scanned %d faces (%s), %d candidate edges, %d rails at the limits (%.0f deg, "
            "%.0f long, %.0f slope)%s",
            state.faces, area,
            static_cast<int>(state.edges.size()), static_cast<int>(state.runs.size()),
            static_cast<double>(state.limits.degrees), static_cast<double>(state.limits.min_length_),
            static_cast<double>(state.limits.max_slope_), state.capped ? " - capped" : "");
    log_rail_heights();
}

void create()
{
    AutoState& state = auto_state();
    if (state.runs.empty()) {
        state.message = "Nothing to make";
        return;
    }
    if (static_cast<int>(state.runs.size()) > kMaxRails) {
        state.message = "Too many rails: narrow the area or raise the limits";
        return;
    }
    std::vector<Edge> edges;
    edges.reserve(state.edges.size());
    for (const AutoEdge& edge : state.edges) {
        edges.push_back(edge.edge);
    }
    const std::vector<chains::NewRail> rails = rails_from(edges, state.runs);
    const chains::State before = chains::at_hand();
    chains::RailsMade made;
    std::string message;
    if (!chains::add_rails(rails, kNewRailJoinReach, &made, &message)) {
        state.message = message;
        return;
    }
    std::array<char, kTextSize> text{};
    text::format_to(text, "Auto Rails: %d rails, %d points", made.rails, made.points);
    edit::chain_changed(&before, 0, text.data());
    get_log("level editor: auto rails - made %d rails, %d points, %d ends joined to rails there", made.rails,
            made.points, made.joined);
    state.message = text.data();
    // (the new rails are rails now: scanned again, they are left out)
    scan();
}

void area_controls(AutoState& state)
{
    ImGui::RadioButton("Around camera", &state.area, static_cast<int>(Area::kCamera));
    ImGui::SameLine();
    ImGui::RadioButton("Selection", &state.area, static_cast<int>(Area::kSelection));
    ImGui::SameLine();
    ImGui::RadioButton("Whole level", &state.area, static_cast<int>(Area::kLevel));
    if (state.area == static_cast<int>(Area::kCamera)) {
        ImGui::SliderFloat("Radius", &state.radius, kRadiusLeast, kRadiusMost, "%.0f");
    }
    if (ImGui::Button("Scan")) {
        state.queue.emplace_back([] { scan(); });
    }
}

void limit_controls(AutoState& state)
{
    bool changed = ImGui::SliderFloat("Edge angle", &state.limits.degrees, kLeastDegrees, kAngleMost, "%.0f deg");
    if (ImGui::IsItemHovered()) {
        gui::tooltip("How sharply the faces must turn at the edge");
    }
    changed = ImGui::SliderFloat("Min length", &state.limits.min_length_, kLengthLeast, kLengthMost, "%.0f") || changed;
    changed = ImGui::SliderFloat("Max slope", &state.limits.max_slope_, 0.0F, kMostSlope, "%.0f deg") || changed;
    if (changed) {
        state.queue.emplace_back([] { refresh_runs(); });
    }
}

} // namespace

bool get_open() { return auto_state().open; }

void set_open(bool open)
{
    AutoState& state = auto_state();
    state.open = open;
    if (!open) {
        state.edges.clear();
        state.runs.clear();
        state.scanned = false;
        state.message.clear();
    }
}

void halt()
{
    set_open(false);
    auto_state().queue.clear();
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(auto_state().queue);
    for (auto& action : queue) {
        action();
    }
}

void add_overlay(const overlays::Painter& painter)
{
    const AutoState& state = auto_state();
    if (!state.open || state.runs.empty()) {
        return;
    }
    int dashes = 0;
    const V3 lift = {.x = 0.0F, .y = kRailLift, .z = 0.0F};
    for (const Run& run : state.runs) {
        const std::vector<V3> points = drop_straight(run.points, run.closed, kStraightDegrees);
        if (points.empty() || length_of(points.front() - painter.camera()) > kPreviewReach) {
            continue;
        }
        const size_t segments = run.closed ? points.size() : points.size() - 1;
        for (size_t i = 0; i < segments && dashes < kMaxDashes; ++i) {
            const V3 from = core::at(points, i) + lift;
            const V3 to = core::at(points, (i + 1) % points.size()) + lift;
            const float length = length_of(to - from);
            const V3 along = unit_or_zero(to - from);
            const auto count = static_cast<int>(std::ceil(length / (kDashOn + kDashOff)));
            for (int dash = 0; dash < count && dashes < kMaxDashes; ++dash) {
                const float at = static_cast<float>(dash) * (kDashOn + kDashOff);
                (void)painter.get_seg(from + along * at, from + along * std::min(at + kDashOn, length), kPreviewColour,
                                      kPreviewThickness);
                ++dashes;
            }
        }
    }
}

void window(float view_x, float view_y, float view_width, float scale)
{
    AutoState& state = auto_state();
    if (!state.open) {
        return;
    }
    bool open = true;
    const char* title = "Auto Rails (Experimental)###autoRails";
    if (ui::app::place_floating(title, false)) {
        ImGui::SetNextWindowPos(
            ImVec2(view_x + view_width - (kPanelRightInset * scale), view_y + (kPanelTopOffset * scale)),
            ImGuiCond_Appearing, ImVec2(1.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth * scale, 0.0F), ImGuiCond_Appearing);
    }
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        gui::text_wrapped("Experimental. Finds ledges and edges to grind. Check every rail before you save.");
        ImGui::Separator();
        area_controls(state);
        limit_controls(state);
        ImGui::Separator();
        if (state.scanned) {
            gui::text("%d rails, %d points, %.0f long", static_cast<int>(state.runs.size()), state.points,
                      static_cast<double>(state.length));
        } else {
            gui::text_disabled("Scan to find rails");
        }
        if (!state.message.empty()) {
            gui::text_disabled("%s%s", state.message.c_str(), state.capped ? " (capped)" : "");
        }
        if (ImGui::Button("Create")) {
            state.queue.emplace_back([] { create(); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            state.queue.emplace_back([] { set_open(false); });
        }
    }
    ImGui::End();
    if (!open) {
        state.queue.emplace_back([] { set_open(false); });
    }
}

std::string describe()
{
    const AutoState& state = auto_state();
    if (!state.open) {
        return "";
    }
    return "auto rails: " + std::to_string(state.runs.size()) + " candidates";
}

} // namespace frontend::level_editor::rail_tools::auto_rails
