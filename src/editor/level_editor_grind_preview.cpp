// NOT ORIGINAL - the level editor's grind-line preview (ours). See level_editor_grind_preview.h.

#include "editor/level_editor_grind_preview.h"

#include <algorithm>
#include <cmath>
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

// --- the pieces -------------------------------------------------------------------------------------------------

Rail rail_of(const Graph& graph, int start)
{
    Rail rail;
    if (start < 0 || start >= graph.count)
        return rail;
    std::vector<std::vector<int>> around(graph.count);
    for (const auto& link : graph.links) {
        if (link.first < 0 || link.second < 0 || link.first >= graph.count || link.second >= graph.count)
            continue;
        core::at(around, link.first).push_back(link.second);
        core::at(around, link.second).push_back(link.first);
    }
    std::vector<uint8_t> seen(graph.count, 0);
    std::vector<int> stack = {start};
    core::at(seen, start) = 1;
    while (!stack.empty()) {
        const int p = stack.back();
        stack.pop_back();
        rail.points.push_back(p);
        for (const int q : core::at(around, p)) {
            if (!core::at(seen, q)) {
                core::at(seen, q) = 1;
                stack.push_back(q);
            }
        }
    }
    std::ranges::sort(rail.points);
    for (const auto& link : graph.links) {
        if (link.first >= 0 && link.first < graph.count && core::at(seen, link.first) && link.second >= 0 &&
            link.second < graph.count && link.first != link.second)
            rail.links.push_back(link);
    }
    return rail;
}

namespace {

std::unordered_map<int, int> get_degrees(const Rail& rail)
{
    std::unordered_map<int, int> degree;
    for (const int p : rail.points)
        degree[p] = 0;
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
    for (const int p : rail.points)
        if (degree[p] == 1)
            ends.push_back(p);
    return ends;
}

std::vector<int> get_branches(const Rail& rail)
{
    std::unordered_map<int, int> degree = get_degrees(rail);
    std::vector<int> branches;
    for (const int p : rail.points)
        if (degree[p] >= 3)
            branches.push_back(p);
    return branches;
}

std::vector<V3> chevron_tips(V3 a, V3 b, float spacing)
{
    std::vector<V3> tips;
    const V3 d = b - a;
    const float length = std::sqrt(get_dot(d, d));
    if (length < 1e-3f || !(spacing > 0.0f))
        return tips;
    const int count = std::max(1, static_cast<int>(length / spacing));
    for (int i = 0; i < count && i < 64; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        tips.push_back(a + d * t);
    }
    return tips;
}

float len_of(const Rail& rail, const std::vector<V3>& positions)
{
    float total = 0.0f;
    for (const auto& link : rail.links) {
        if (link.first < 0 || link.second < 0 || std::cmp_greater_equal(link.first, positions.size()) ||
            std::cmp_greater_equal(link.second, positions.size()))
            continue;
        const V3 d = core::at(positions, link.second) - core::at(positions, link.first);
        total += std::sqrt(get_dot(d, d));
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
    std::string describe_{};
    // (the terrain's name, asked once per rail shown)
    uint32_t terrain_for_{};
    std::string terrain_{};
};

LevelEditorGrindPreviewState& level_editor_grind_preview_state()
{
    static core::NoDestroy<LevelEditorGrindPreviewState> state;
    return state.get();
}

} // namespace

void add_overlay(const overlays::Painter& painter, int hovered, int selected)
{
    level_editor_grind_preview_state().describe_.clear();
    if (!overlays::get_shown(overlays::kGrindPreview))
        return;
    const std::vector<nodes::Elem>& all = nodes::get_all();
    auto rail_node = [&](int index) {
        return index >= 0 && std::cmp_less(index, all.size()) && core::at(all, index).family == nodes::Family::kRail;
    };
    const int index = rail_node(hovered) ? hovered : rail_node(selected) ? selected : -1;
    const objects::GrindRailHub* rails = get_rails();
    if (index < 0 || !rails)
        return;
    const uint32_t name = core::at(all, index).name;
    // The rail manager's graph (a point to its next), and the point named.
    Graph graph;
    graph.count = rails->num();
    std::vector<V3> positions(graph.count);
    int start = -1;
    for (int i = 0; i < rails->num(); ++i) {
        const objects::GrindPoint& node = core::at(rails->node_run(), i);
        core::at(positions, i) = {.x = node.at_point().x, .y = node.at_point().y, .z = node.at_point().z};
        if (node.label() == name && !(node.flag_bits() & objects::kRailFlagClimbing))
            start = i;
        if (node.succ() && !(node.flag_bits() & objects::kRailFlagClimbing)) {
            const int next = static_cast<int>(node.succ() - rails->nodes());
            if (next >= 0 && next < rails->num())
                graph.links.emplace_back(i, next);
        }
    }
    if (start < 0)
        return;
    const Rail rail = rail_of(graph, start);
    const bool active = (core::at(rails->node_run(), start).flag_bits() & objects::kRailFlagActive) != 0;
    // The segments with their direction; the points; the ends; branches.
    constexpr uint32_t kSegment = 0xF0FFFFFF, kChevron = 0xFF40E0FF, kInactive = 0xA0909090;
    const uint32_t segment_colour = active ? kSegment : kInactive;
    for (const auto& link : rail.links) {
        const V3 a = core::at(positions, link.first);
        const V3 b = core::at(positions, link.second);
        (void)painter.get_seg(a, b, segment_colour, 3.0f);
        const V3 d = b - a;
        const float length = std::sqrt(get_dot(d, d));
        if (length < 1e-3f)
            continue;
        const V3 along = d * (1.0f / length);
        // (a chevron: two short strokes back from the tip, either side across the rail - in the horizontal plane, or
        // about x for a vertical rail)
        V3 side = perpendicular(along, {.x = 0.0f, .y = 1.0f, .z = 0.0f});
        if (get_dot(side, side) < 1e-4f)
            side = {.x = 1.0f, .y = 0.0f, .z = 0.0f};
        side = make_unit(side);
        const float size = std::clamp(painter.span_to(a + d * 0.5f) * 0.012f, 4.0f, 24.0f);
        for (const V3 tip : chevron_tips(a, b, std::max(kChevronSpacing, size * 4.0f))) {
            (void)painter.get_seg(tip, tip - along * size + side * size * 0.6f, kChevron, 2.0f);
            (void)painter.get_seg(tip, tip - along * size - side * size * 0.6f, kChevron, 2.0f);
        }
    }
    for (const int p : rail.points)
        painter.get_dot(core::at(positions, p), p == start ? 6.0f : 3.5f, p == start ? 0xFFFFF060 : 0xFFE0F0FF);
    const std::vector<int> ends = get_ends(rail);
    for (const int e : ends)
        painter.get_dot(core::at(positions, e), 8.0f, 0xFFFFFFFF, false);
    for (const int b : get_branches(rail)) {
        painter.get_dot(core::at(positions, b), 9.0f, 0xFFFFC040, false);
        painter.draw_label(core::at(positions, b), 0xFFFFC040, "branch");
    }
    // Speed-ups on it (the level's and the editor's).
    int speed_ups = 0;
    const V3 near = core::at(positions, start);
    for (const chains::SpeedSegment& s : chains::speed_segments(near, 50000.0f)) {
        for (const auto& link : rail.links) {
            const V3 da = s.a - core::at(positions, link.first);
            const V3 db = s.b - core::at(positions, link.second);
            if (get_dot(da, da) > 1.0f || get_dot(db, db) > 1.0f)
                continue;
            ++speed_ups;
            (void)painter.get_seg(s.a, s.b, 0xFF40FF70, 5.0f);
            std::array<char, 48> text{};
            text::format_to(text, "speed-up %.1f%%%s", s.rate * 100.0f, s.level ? " (level)" : "");
            painter.draw_label((s.a + s.b) * 0.5f, 0xFF40FF70, text.data());
            break;
        }
    }
    // Transfers: other rails' ends near this one's ends.
    std::vector<uint8_t> in_rail(graph.count, 0);
    for (const int p : rail.points)
        core::at(in_rail, p) = 1;
    std::vector<int> degree(graph.count, 0);
    for (const auto& link : graph.links) {
        ++core::at(degree, link.first);
        ++core::at(degree, link.second);
    }
    int transfers = 0;
    for (const int e : ends) {
        for (int q = 0; q < graph.count && transfers < 24; ++q) {
            if (core::at(in_rail, q) || core::at(degree, q) != 1 || (core::at(rails->node_run(), q).flag_bits() & objects::kRailFlagClimbing))
                continue;
            const V3 d = core::at(positions, q) - core::at(positions, e);
            const float distance = std::sqrt(get_dot(d, d));
            if (distance > kTransferReach)
                continue;
            ++transfers;
            (void)painter.get_seg(core::at(positions, e), core::at(positions, q), 0xC0FF80FF, 1.5f);
            painter.get_dot(core::at(positions, q), 6.0f, 0xFFFF80FF, false);
            std::array<char, 48> text{};
            text::format_to(text, "transfer %.0f", distance);
            painter.draw_label((core::at(positions, e) + core::at(positions, q)) * 0.5f, 0xFFFF80FF, text.data());
        }
    }
    // The label at the point: the rail, its size, terrain, speed-ups, connections.
    if (level_editor_grind_preview_state().terrain_for_ != name) {
        level_editor_grind_preview_state().terrain_for_ = name;
        chains::View view = chains::view_of(name);
        level_editor_grind_preview_state().terrain_ = view.terrain >= 0 && std::cmp_less(view.terrain, view.terrains.size()) ? core::at(view.terrains, view.terrain)
                                                                                           : std::string("terrain ?");
    }
    std::array<char, 256> text{};
    text::format_to(text, "%s: %d points, %d segments, %.0f long, %s%s%s%s", nodes::caption(name).c_str(),
                    static_cast<int>(rail.points.size()), static_cast<int>(rail.links.size()), len_of(rail, positions),
                    level_editor_grind_preview_state().terrain_.c_str(), active ? "" : ", off", speed_ups ? ", speed-ups" : "",
                    rail.links.empty() ? ", a single point (not grindable)" : "");
    painter.draw_label(core::at(positions, start), 0xFFFFF060, text.data(), 10.0f, -24.0f);
    level_editor_grind_preview_state().describe_ = text.data();
    if (transfers || !get_branches(rail).empty()) {
        text::format_to(text, "%d transfers within %.0f, %d branches", transfers, kTransferReach,
                        static_cast<int>(get_branches(rail).size()));
        painter.draw_label(core::at(positions, start), 0xFFFF80FF, text.data(), 10.0f, -8.0f);
        level_editor_grind_preview_state().describe_ += std::string("; ") + text.data();
    }
}

std::string describe() { return level_editor_grind_preview_state().describe_; }

} // namespace frontend::level_editor::grind_preview
