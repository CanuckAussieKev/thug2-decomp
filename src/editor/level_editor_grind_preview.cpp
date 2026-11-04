// NOT ORIGINAL - the level editor's grind-line preview (ours). See level_editor_grind_preview.h.

#include "editor/level_editor_grind_preview.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_overlays.h"
#include "engine/objects/rail_manager.h"
#include "game/skate/skate_module.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::grind_preview {

namespace {

constexpr float kMinSegmentLength = 1e-3F; // a link shorter than this has no direction
constexpr int kMaxChevronsPerLink = 64;
constexpr float kSegmentWidth = 3.0F;
constexpr float kMinSideLength = 1e-4F;      // a chevron's side vector shorter than this is replaced
constexpr float kChevronSideFraction = 0.6F; // a chevron's strokes spread this share of their length
constexpr float kChevronWidth = 2.0F;
constexpr float kStartDotRadius = 6.0F;
constexpr float kPointDotRadius = 3.5F;
constexpr uint32_t kStartColour = 0xFFFFF060; // ARGB: the point the rail was picked at
constexpr uint32_t kPointColour = 0xFFE0F0FF; // ARGB
constexpr float kEndDotRadius = 8.0F;
constexpr uint32_t kEndColour = 0xFFFFFFFF;   // ARGB
constexpr float kBranchDotRadius = 9.0F;
constexpr uint32_t kBranchColour = 0xFFFFC040; // ARGB
constexpr uint32_t kSpeedUpColour = 0xFF40FF70; // ARGB
constexpr float kSpeedUpWidth = 5.0F;
constexpr std::size_t kSpeedUpTextSize = 48;
constexpr int kMaxTransfers = 24;            // transfers drawn at most
constexpr uint32_t kTransferLineColour = 0xC0FF80FF; // ARGB
constexpr float kTransferLineWidth = 1.5F;
constexpr float kTransferDotRadius = 6.0F;
constexpr uint32_t kTransferColour = 0xFFFF80FF; // ARGB
constexpr std::size_t kTransferTextSize = 48;
constexpr std::size_t kRailTextSize = 256;
constexpr float kLabelOffsetX = 10.0F;  // the labels sit this far right of the point
constexpr float kRailLabelRise = 24.0F; // the rail label sits this far above the point
constexpr float kTransferLabelRise = 8.0F;

constexpr float kHalf = 0.5F; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

Rail rail_of(const Graph& graph, int start)
{
    Rail rail;
    if (start < 0 || start >= graph.count) {
        return rail;
    }
    std::vector<std::vector<int>> around(graph.count);
    for (const auto& link : graph.links) {
        if (link.first < 0 || link.second < 0 || link.first >= graph.count || link.second >= graph.count) {
            continue;
        }
        core::at(around, link.first).push_back(link.second);
        core::at(around, link.second).push_back(link.first);
    }
    std::vector<uint8_t> seen(graph.count, 0);
    std::vector<int> stack = {start};
    core::at(seen, start) = 1;
    while (!stack.empty()) {
        const int point = stack.back();
        stack.pop_back();
        rail.points.push_back(point);
        for (const int neighbour : core::at(around, point)) {
            if (core::at(seen, neighbour) == 0U) {
                core::at(seen, neighbour) = 1;
                stack.push_back(neighbour);
            }
        }
    }
    std::ranges::sort(rail.points);
    for (const auto& link : graph.links) {
        if (link.first >= 0 && link.first < graph.count && (core::at(seen, link.first) != 0U) && link.second >= 0 &&
            link.second < graph.count && link.first != link.second) {
            rail.links.push_back(link);
        }
    }
    return rail;
}

namespace {

std::unordered_map<int, int> get_degrees(const Rail& rail)
{
    std::unordered_map<int, int> degree;
    for (const int point : rail.points) {
        degree[point] = 0;
    }
    for (const auto& link : rail.links) {
        ++degree[link.first];
        ++degree[link.second];
    }
    return degree;
}

} // namespace

std::vector<int> get_ends(const Rail& rail)
{
    std::unordered_map<int, int> degree = get_degrees(rail);
    std::vector<int> ends;
    for (const int point : rail.points) {
        if (degree[point] == 1) {
            ends.push_back(point);
        }
    }
    return ends;
}

std::vector<int> get_branches(const Rail& rail)
{
    std::unordered_map<int, int> degree = get_degrees(rail);
    std::vector<int> branches;
    for (const int point : rail.points) {
        if (degree[point] >= 3) {
            branches.push_back(point);
        }
    }
    return branches;
}

std::vector<V3> chevron_tips(V3 a, V3 b, float spacing)
{
    std::vector<V3> tips;
    const V3 delta = b - a;
    const float length = std::sqrt(get_dot(delta, delta));
    if (length < kMinSegmentLength || !(spacing > 0.0F)) {
        return tips;
    }
    const int count = std::max(1, static_cast<int>(length / spacing));
    for (int i = 0; i < count && i < kMaxChevronsPerLink; ++i) {
        const float fraction = (static_cast<float>(i) + 0.5F) / static_cast<float>(count);
        tips.push_back(a + delta * fraction);
    }
    return tips;
}

float len_of(const Rail& rail, const std::vector<V3>& positions)
{
    float total = 0.0F;
    for (const auto& link : rail.links) {
        if (link.first < 0 || link.second < 0 || std::cmp_greater_equal(link.first, positions.size()) ||
            std::cmp_greater_equal(link.second, positions.size())) {
            continue;
        }
        const V3 delta = core::at(positions, link.second) - core::at(positions, link.first);
        total += std::sqrt(get_dot(delta, delta));
    }
    return total;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

// The rail manager (the skate module's +0x24 - as level_editor_chains.cpp reads it): the grindable points where they
// are now; not one on an object.
objects::GrindRailHub* get_rails()
{
    const modules::Skate* skate = modules::Skate::sole();
    auto* manager = skate ? skate->rail_manager().get() : nullptr;
    return manager && !manager->on_object() && manager->nodes() ? manager : nullptr;
}

// NOT ORIGINAL grouping - level_editor_grind_preview.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_grind_preview_state()).
struct LevelEditorGrindPreviewState {
    std::string describe_;
    // (the terrain's name, asked once per rail shown)
    uint32_t terrain_for_{};
    std::string terrain_;
};

LevelEditorGrindPreviewState& level_editor_grind_preview_state()
{
    static core::NoDestroy<LevelEditorGrindPreviewState> state;
    return state.get();
}

// The rail's segments, with chevrons showing their direction.
void draw_rail_segments(const overlays::Painter& painter, const Rail& rail, const std::vector<V3>& positions,
                        uint32_t segment_colour)
{
    constexpr uint32_t kChevron = 0xFF40E0FF;
    for (const auto& link : rail.links) {
        const V3 point_a = core::at(positions, link.first);
        const V3 point_b = core::at(positions, link.second);
        (void)painter.get_seg(point_a, point_b, segment_colour, kSegmentWidth);
        const V3 delta = point_b - point_a;
        const float length = std::sqrt(get_dot(delta, delta));
        if (length < kMinSegmentLength) {
            continue;
        }
        const V3 along = delta * (1.0F / length);
        // (a chevron: two short strokes back from the tip, either side across the rail - in the horizontal plane, or
        // about x for a vertical rail)
        V3 side = perpendicular(along, {.x = 0.0F, .y = 1.0F, .z = 0.0F});
        if (get_dot(side, side) < kMinSideLength) {
            side = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
        }
        side = make_unit(side);
        const float size = std::clamp(painter.span_to(point_a + delta * 0.5F) * 0.012F, 4.0F, 24.0F);
        for (const V3 tip : chevron_tips(point_a, point_b, std::max(kChevronSpacing, size * 4.0F))) {
            (void)painter.get_seg(tip, tip - along * size + side * size * kChevronSideFraction, kChevron, kChevronWidth);
            (void)painter.get_seg(tip, tip - along * size - side * size * kChevronSideFraction, kChevron, kChevronWidth);
        }
    }
}

// The speed-ups on the rail (the level's and the editor's), labelled; how many.
int draw_rail_speed_ups(const overlays::Painter& painter, const Rail& rail, const std::vector<V3>& positions, V3 near)
{
    int speed_ups = 0;
    for (const chains::SpeedSegment& speed_segment : chains::speed_segments(near, 50000.0F)) {
        for (const auto& link : rail.links) {
            const V3 da = speed_segment.a - core::at(positions, link.first);
            const V3 db = speed_segment.b - core::at(positions, link.second);
            if (get_dot(da, da) > 1.0F || get_dot(db, db) > 1.0F) {
                continue;
            }
            ++speed_ups;
            (void)painter.get_seg(speed_segment.a, speed_segment.b, kSpeedUpColour, kSpeedUpWidth);
            std::array<char, kSpeedUpTextSize> text{};
            text::format_to(text, "speed-up %.1f%%%s", speed_segment.rate * 100.0F,
                            speed_segment.level ? " (level)" : "");
            painter.draw_label((speed_segment.a + speed_segment.b) * kHalf, kSpeedUpColour, text.data());
            break;
        }
    }
    return speed_ups;
}

// Transfers: other rails' ends near this one's ends, with their distances; how many.
int draw_transfers(const overlays::Painter& painter, const Rail& rail, const Graph& graph,
                   const std::vector<V3>& positions, const std::vector<int>& ends, const objects::GrindRailHub* rails)
{
    std::vector<uint8_t> in_rail(graph.count, 0);
    for (const int point : rail.points) {
        core::at(in_rail, point) = 1;
    }
    std::vector<int> degree(graph.count, 0);
    for (const auto& link : graph.links) {
        ++core::at(degree, link.first);
        ++core::at(degree, link.second);
    }
    int transfers = 0;
    for (const int end_point : ends) {
        for (int candidate = 0; candidate < graph.count && transfers < kMaxTransfers; ++candidate) {
            if ((core::at(in_rail, candidate) != 0U) || core::at(degree, candidate) != 1 ||
                ((core::at(rails->node_run(), candidate).flag_bits() & objects::kRailFlagClimbing) != 0U)) {
                continue;
            }
            const V3 delta = core::at(positions, candidate) - core::at(positions, end_point);
            const float distance = std::sqrt(get_dot(delta, delta));
            if (distance > kTransferReach) {
                continue;
            }
            ++transfers;
            (void)painter.get_seg(core::at(positions, end_point), core::at(positions, candidate), kTransferLineColour,
                                  kTransferLineWidth);
            painter.get_dot(core::at(positions, candidate), kTransferDotRadius, kTransferColour, false);
            std::array<char, kTransferTextSize> text{};
            text::format_to(text, "transfer %.0f", distance);
            painter.draw_label((core::at(positions, end_point) + core::at(positions, candidate)) * kHalf,
                               kTransferColour, text.data());
        }
    }
    return transfers;
}

} // namespace

void add_overlay(const overlays::Painter& painter, int hovered, int selected)
{
    level_editor_grind_preview_state().describe_.clear();
    if (!overlays::get_shown(overlays::kGrindPreview)) {
        return;
    }
    const std::vector<nodes::Elem>& all = nodes::get_all();
    auto rail_node = [&](int index) {
        return index >= 0 && std::cmp_less(index, all.size()) && core::at(all, index).family == nodes::Family::kRail;
    };
    int index = -1; // (the hovered rail point, else the selected one)
    if (rail_node(hovered)) {
        index = hovered;
    } else if (rail_node(selected)) {
        index = selected;
    }
    const objects::GrindRailHub* rails = get_rails();
    if (index < 0 || !rails) {
        return;
    }
    const uint32_t name = core::at(all, index).name;
    // The rail manager's graph (a point to its next), and the point named.
    Graph graph;
    graph.count = rails->num();
    std::vector<V3> positions(graph.count);
    int start = -1;
    for (int i = 0; i < rails->num(); ++i) {
        const objects::GrindPoint& node = core::at(rails->node_run(), i);
        core::at(positions, i) = {.x = node.at_point().x, .y = node.at_point().y, .z = node.at_point().z};
        if (node.label() == name && ((node.flag_bits() & objects::kRailFlagClimbing) == 0U)) {
            start = i;
        }
        if (node.succ() && ((node.flag_bits() & objects::kRailFlagClimbing) == 0U)) {
            const int next = static_cast<int>(node.succ() - rails->nodes());
            if (next >= 0 && next < rails->num()) {
                graph.links.emplace_back(i, next);
            }
        }
    }
    if (start < 0) {
        return;
    }
    const Rail rail = rail_of(graph, start);
    const bool active = (core::at(rails->node_run(), start).flag_bits() & objects::kRailFlagActive) != 0;
    // The segments with their direction; the points; the ends; branches.
    constexpr uint32_t kSegment = 0xF0FFFFFF;
    constexpr uint32_t kInactive = 0xA0909090;
    const uint32_t segment_colour = active ? kSegment : kInactive;
    draw_rail_segments(painter, rail, positions, segment_colour);
    for (const int point : rail.points) {
        painter.get_dot(core::at(positions, point), point == start ? kStartDotRadius : kPointDotRadius,
                        point == start ? kStartColour : kPointColour);
    }
    const std::vector<int> ends = get_ends(rail);
    for (const int end_point : ends) {
        painter.get_dot(core::at(positions, end_point), kEndDotRadius, kEndColour, false);
    }
    for (const int branch : get_branches(rail)) {
        painter.get_dot(core::at(positions, branch), kBranchDotRadius, kBranchColour, false);
        painter.draw_label(core::at(positions, branch), kBranchColour, "branch");
    }
    // Speed-ups on it (the level's and the editor's).
    const V3 near = core::at(positions, start);
    const int speed_ups = draw_rail_speed_ups(painter, rail, positions, near);
    // Transfers: other rails' ends near this one's ends.
    const int transfers = draw_transfers(painter, rail, graph, positions, ends, rails);
    // The label at the point: the rail, its size, terrain, speed-ups, connections.
    if (level_editor_grind_preview_state().terrain_for_ != name) {
        level_editor_grind_preview_state().terrain_for_ = name;
        chains::View view = chains::view_of(name);
        level_editor_grind_preview_state().terrain_ = view.terrain >= 0 && std::cmp_less(view.terrain, view.terrains.size()) ? core::at(view.terrains, view.terrain)
                                                                                           : std::string("terrain ?");
    }
    std::array<char, kRailTextSize> text{};
    text::format_to(text, "%s: %d points, %d segments, %.0f long, %s%s%s%s", nodes::caption(name).c_str(),
                    static_cast<int>(rail.points.size()), static_cast<int>(rail.links.size()), len_of(rail, positions),
                    level_editor_grind_preview_state().terrain_.c_str(), active ? "" : ", off",
                    (speed_ups != 0) ? ", speed-ups" : "",
                    rail.links.empty() ? ", a single point (not grindable)" : "");
    painter.draw_label(core::at(positions, start), kStartColour, text.data(), kLabelOffsetX, -kRailLabelRise);
    level_editor_grind_preview_state().describe_ = text.data();
    if ((transfers != 0) || !get_branches(rail).empty()) {
        text::format_to(text, "%d transfers within %.0f, %d branches", transfers, kTransferReach,
                        static_cast<int>(get_branches(rail).size()));
        painter.draw_label(core::at(positions, start), kTransferColour, text.data(), kLabelOffsetX, -kTransferLabelRise);
        level_editor_grind_preview_state().describe_ += std::string("; ") + text.data();
    }
}

std::string describe() { return level_editor_grind_preview_state().describe_; }

} // namespace frontend::level_editor::grind_preview
