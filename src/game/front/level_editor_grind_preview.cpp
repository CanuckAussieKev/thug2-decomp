// NOT ORIGINAL - the level editor's grind-line preview (ours). See level_editor_grind_preview.h.

#include "game/front/level_editor_grind_preview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_overlays.h"
#include "game/obj/rail_manager.h"
#include "hook/game.h"

namespace Front::LevelEditor::GrindPreview {

// --- the pieces -------------------------------------------------------------------------------------------------

Rail RailOf(const Graph& graph, int start)
{
    Rail rail;
    if (start < 0 || start >= graph.count)
        return rail;
    std::vector<std::vector<int>> around(graph.count);
    for (const auto& link : graph.links) {
        if (link.first < 0 || link.second < 0 || link.first >= graph.count || link.second >= graph.count)
            continue;
        around[link.first].push_back(link.second);
        around[link.second].push_back(link.first);
    }
    std::vector<uint8_t> seen(graph.count, 0);
    std::vector<int> stack = {start};
    seen[start] = 1;
    while (!stack.empty()) {
        int p = stack.back();
        stack.pop_back();
        rail.points.push_back(p);
        for (int q : around[p]) {
            if (!seen[q]) {
                seen[q] = 1;
                stack.push_back(q);
            }
        }
    }
    std::sort(rail.points.begin(), rail.points.end());
    for (const auto& link : graph.links) {
        if (link.first >= 0 && link.first < graph.count && seen[link.first] && link.second >= 0 &&
            link.second < graph.count && link.first != link.second)
            rail.links.push_back(link);
    }
    return rail;
}

namespace {

std::unordered_map<int, int> Degrees(const Rail& rail)
{
    std::unordered_map<int, int> degree;
    for (int p : rail.points)
        degree[p] = 0;
    for (const auto& link : rail.links) {
        ++degree[link.first];
        ++degree[link.second];
    }
    return degree;
}

} // namespace

std::vector<int> Ends(const Rail& rail)
{
    std::unordered_map<int, int> degree = Degrees(rail);
    std::vector<int> ends;
    for (int p : rail.points)
        if (degree[p] == 1)
            ends.push_back(p);
    return ends;
}

std::vector<int> Branches(const Rail& rail)
{
    std::unordered_map<int, int> degree = Degrees(rail);
    std::vector<int> branches;
    for (int p : rail.points)
        if (degree[p] >= 3)
            branches.push_back(p);
    return branches;
}

std::vector<V3> ChevronTips(V3 a, V3 b, float spacing)
{
    std::vector<V3> tips;
    V3 d = b - a;
    float length = std::sqrt(Dot(d, d));
    if (length < 1e-3f || !(spacing > 0.0f))
        return tips;
    int count = std::max(1, static_cast<int>(length / spacing));
    for (int i = 0; i < count && i < 64; ++i) {
        float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        tips.push_back(a + d * t);
    }
    return tips;
}

float Length(const Rail& rail, const std::vector<V3>& positions)
{
    float total = 0.0f;
    for (const auto& link : rail.links) {
        if (link.first < 0 || link.second < 0 || link.first >= static_cast<int>(positions.size()) ||
            link.second >= static_cast<int>(positions.size()))
            continue;
        V3 d = positions[link.second] - positions[link.first];
        total += std::sqrt(Dot(d, d));
    }
    return total;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

// The rail manager (the skate module's +0x24 - as level_editor_chains.cpp reads it): the grindable points where they
// are now; not one on an object.
Obj::CRailManager* Rails()
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    auto* manager = skate ? *reinterpret_cast<Obj::CRailManager**>(skate + 0x24) : nullptr;
    return manager && !manager->m_onObject && manager->m_nodes ? manager : nullptr;
}

std::string g_describe;

// (the terrain's name, asked once per rail shown)
uint32_t g_terrainFor;
std::string g_terrain;

} // namespace

void AddOverlay(const Overlays::Painter& painter, int hovered, int selected)
{
    g_describe.clear();
    if (!Overlays::Shown(Overlays::kGrindPreview))
        return;
    const std::vector<Nodes::Node>& all = Nodes::All();
    auto railNode = [&](int index) {
        return index >= 0 && index < static_cast<int>(all.size()) && all[index].family == Nodes::Family::Rail;
    };
    int index = railNode(hovered) ? hovered : railNode(selected) ? selected : -1;
    Obj::CRailManager* rails = Rails();
    if (index < 0 || !rails)
        return;
    uint32_t name = all[index].name;
    // The rail manager's graph (a point to its next), and the point named.
    Graph graph;
    graph.count = rails->m_count;
    std::vector<V3> positions(graph.count);
    int start = -1;
    for (int i = 0; i < rails->m_count; ++i) {
        const Obj::CRailNode& node = rails->m_nodes[i];
        positions[i] = {node.m_pos.x, node.m_pos.y, node.m_pos.z};
        if (node.m_name == name && !(node.m_flags & Obj::RAIL_FLAG_CLIMBING))
            start = i;
        if (node.m_next && !(node.m_flags & Obj::RAIL_FLAG_CLIMBING)) {
            int next = static_cast<int>(node.m_next - rails->m_nodes);
            if (next >= 0 && next < rails->m_count)
                graph.links.push_back({i, next});
        }
    }
    if (start < 0)
        return;
    Rail rail = RailOf(graph, start);
    bool active = (rails->m_nodes[start].m_flags & Obj::RAIL_FLAG_ACTIVE) != 0;
    // The segments with their direction; the points; the ends; branches.
    constexpr uint32_t kSegment = 0xF0FFFFFF, kChevron = 0xFF40E0FF, kInactive = 0xA0909090;
    uint32_t segmentColour = active ? kSegment : kInactive;
    for (const auto& link : rail.links) {
        V3 a = positions[link.first], b = positions[link.second];
        painter.Line(a, b, segmentColour, 3.0f);
        V3 d = b - a;
        float length = std::sqrt(Dot(d, d));
        if (length < 1e-3f)
            continue;
        V3 along = d * (1.0f / length);
        // (a chevron: two short strokes back from the tip, either side across the rail - in the horizontal plane, or
        // about x for a vertical rail)
        V3 side = Cross(along, {0.0f, 1.0f, 0.0f});
        if (Dot(side, side) < 1e-4f)
            side = {1.0f, 0.0f, 0.0f};
        side = Normalize(side);
        float size = std::clamp(painter.Distance(a + d * 0.5f) * 0.012f, 4.0f, 24.0f);
        for (V3 tip : ChevronTips(a, b, std::max(kChevronSpacing, size * 4.0f))) {
            painter.Line(tip, tip - along * size + side * size * 0.6f, kChevron, 2.0f);
            painter.Line(tip, tip - along * size - side * size * 0.6f, kChevron, 2.0f);
        }
    }
    for (int p : rail.points)
        painter.Dot(positions[p], p == start ? 6.0f : 3.5f, p == start ? 0xFFFFF060 : 0xFFE0F0FF);
    std::vector<int> ends = Ends(rail);
    for (int e : ends)
        painter.Dot(positions[e], 8.0f, 0xFFFFFFFF, false);
    for (int b : Branches(rail)) {
        painter.Dot(positions[b], 9.0f, 0xFFFFC040, false);
        painter.Text(positions[b], 0xFFFFC040, "branch");
    }
    // Speed-ups on it (the level's and the editor's).
    int speedUps = 0;
    V3 near = positions[start];
    for (const Chains::SpeedSegment& s : Chains::SpeedSegments(near, 50000.0f)) {
        for (const auto& link : rail.links) {
            V3 da = s.a - positions[link.first], db = s.b - positions[link.second];
            if (Dot(da, da) > 1.0f || Dot(db, db) > 1.0f)
                continue;
            ++speedUps;
            painter.Line(s.a, s.b, 0xFF40FF70, 5.0f);
            char text[48];
            std::snprintf(text, sizeof(text), "speed-up %.1f%%%s", s.rate * 100.0f, s.level ? " (level)" : "");
            painter.Text((s.a + s.b) * 0.5f, 0xFF40FF70, text);
            break;
        }
    }
    // Transfers: other rails' ends near this one's ends.
    std::vector<uint8_t> inRail(graph.count, 0);
    for (int p : rail.points)
        inRail[p] = 1;
    std::vector<int> degree(graph.count, 0);
    for (const auto& link : graph.links) {
        ++degree[link.first];
        ++degree[link.second];
    }
    int transfers = 0;
    for (int e : ends) {
        for (int q = 0; q < graph.count && transfers < 24; ++q) {
            if (inRail[q] || degree[q] != 1 || (rails->m_nodes[q].m_flags & Obj::RAIL_FLAG_CLIMBING))
                continue;
            V3 d = positions[q] - positions[e];
            float distance = std::sqrt(Dot(d, d));
            if (distance > kTransferReach)
                continue;
            ++transfers;
            painter.Line(positions[e], positions[q], 0xC0FF80FF, 1.5f);
            painter.Dot(positions[q], 6.0f, 0xFFFF80FF, false);
            char text[48];
            std::snprintf(text, sizeof(text), "transfer %.0f", distance);
            painter.Text((positions[e] + positions[q]) * 0.5f, 0xFFFF80FF, text);
        }
    }
    // The label at the point: the rail, its size, terrain, speed-ups, connections.
    if (g_terrainFor != name) {
        g_terrainFor = name;
        Chains::View view = Chains::ViewOf(name);
        g_terrain = view.terrain >= 0 && view.terrain < static_cast<int>(view.terrains.size()) ? view.terrains[view.terrain]
                                                                                                 : std::string("terrain ?");
    }
    char text[256];
    std::snprintf(text, sizeof(text), "%s: %d points, %d segments, %.0f long, %s%s%s%s", Nodes::Label(name).c_str(),
                  static_cast<int>(rail.points.size()), static_cast<int>(rail.links.size()), Length(rail, positions),
                  g_terrain.c_str(), active ? "" : ", off", speedUps ? ", speed-ups" : "",
                  rail.links.empty() ? ", a single point (not grindable)" : "");
    painter.Text(positions[start], 0xFFFFF060, text, 10.0f, -24.0f);
    g_describe = text;
    if (transfers || !Branches(rail).empty()) {
        std::snprintf(text, sizeof(text), "%d transfers within %.0f, %d branches", transfers, kTransferReach,
                      static_cast<int>(Branches(rail).size()));
        painter.Text(positions[start], 0xFFFF80FF, text, 10.0f, -8.0f);
        g_describe += std::string("; ") + text;
    }
}

std::string Describe() { return g_describe; }

} // namespace Front::LevelEditor::GrindPreview
