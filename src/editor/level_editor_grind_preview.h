#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor.h"

namespace frontend::level_editor::overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's grind-line preview (ours; see level_editor.h): with the overlay on
// (level_editor_overlays.h: Grind preview), the rail whose gizmo is under the cursor - else the selected rail point's -
// is shown whole: every segment highlighted with chevrons in its direction (a point's Links go to the next point: the
// rail's direction as the level's node array has it), its points, its ends, the terrain (the rail manager node's - the
// Inspector's Terrain, level_editor_chains.h), its speed-ups (Chains::speed_segments: the level's RailAccelerate and the
// editor's) and its connections: points where it branches (a point linked to two), and other rails' ends within
// kTransferReach of its ends (a transfer the skater can make by an ollie).
namespace frontend::level_editor::grind_preview {

// --- the pieces (pure) -------------------------------------------------------------------------------------------

// Rail points (indices 0..count-1) and their links (from -> to).
struct Graph {
    int count = 0;
    std::vector<std::pair<int, int>> links;
};
// The rail `start` is part of: its points (sorted) and its links - everything reached by links either way.
struct Rail {
    std::vector<int> points;
    std::vector<std::pair<int, int>> links;
};
Rail rail_of(const Graph& graph, int start);
// A rail's ends (points with one link in or out in all) and its branch points (three or more).
std::vector<int> get_ends(const Rail& rail);
std::vector<int> get_branches(const Rail& rail);
// Chevrons along a segment: their tips, `spacing` apart (at least one, at its middle, when it's shorter).
std::vector<V3> chevron_tips(V3 a, V3 b, float spacing);
// A rail's length (its links' lengths summed); `positions` by point index.
float len_of(const Rail& rail, const std::vector<V3>& positions);
constexpr float kTransferReach = 120.0f; // (units: other rails' ends this near an end are shown as transfers)
constexpr float kChevronSpacing = 60.0f;

// --- the game side ----------------------------------------------------------------------------------------------

// The preview over the view for the rail of node `hovered` (Nodes::All(); -1: none - then `selected`'s), when the
// overlay is on and the node is a rail point.
void add_overlay(const overlays::Painter& painter, int hovered, int selected);
// What it shows now ("" when nothing): the rail, its points, length, terrain.
std::string describe();

} // namespace frontend::level_editor::grind_preview
