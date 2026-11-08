#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "editor/level_editor.h"

namespace frontend::level_editor::overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's rail tools (ours; what a rail is, and how the editor adds and links its points:
// level_editor_chains.h):
//   - Rail from Edges (Alt+E; Tools > Rails and Paths): edges of the level's collision clicked (the game's collision
//     ray finds the face under the cursor, then the side of it nearest the hit - a side lying flat between two faces of
//     one plane only when the face has no other; the edge under the cursor drawn highlighted). A click picks that edge
//     alone, Shift+click adds one, a click on a picked edge drops it; Enter (or Create Rail) makes the rails, Esc stops.
//     Picked edges that share ends become one rail in order (walked from one end through the edges both ways; a run
//     whose ends meet is a loop - the last point linked back to the first), its points on the edge (kRailLift), its
//     terrain the grind terrain matching the faces' (grind_terrain_for), a trick object of the scenery piece's cluster
//     when the faces' piece is one (level_editor_clusters.h), else of its own (as a drawn rail). One undo step.
//   - Snapping rail ends: a rail's free end (no next, or no previous) dragged within kEndSnapReach of another rail's
//     free end goes onto it (a ring drawn there) and, let go, the two are joined - the dragged point taken out and the
//     point before it linked to the other end, links running one way (chains::join_ends; one undo step with the move).
//     Drawing a rail shows the same ring on the end a click would link onto. Alt while dragging or clicking: no
//     snapping. Rail from Edges' rails join free ends within kNewRailJoinReach of theirs.
//   - Join Rails (Ctrl+J): the selected rail's free end nearest another rail's free end (or its own other end: a loop)
//     within kJoinRailsReach - linked (merged when they meet).
//   - Auto Rails (the Experimental menu): the collision near the camera, of the selected scenery or of the whole level
//     scanned for edges a skater could grind - see AutoEdge -, chained as Rail from Edges chains them, shown dashed
//     with sliders; Create makes them all (one undo step).
namespace frontend::level_editor::rail_tools {

// --- the pure parts ------------------------------------------------------------------------------------------------

// How far above its edge a rail point sits: on it, as the levels' own rails sit (their points over the nearest
// collision edge, measured in four stock levels' nodes and collision: a median of 0.0 - 0.1 units).
constexpr float kRailLift = 0.0F;
// Edge ends this close are one point.
constexpr float kWeld = 0.5F;
// A dragged rail end snaps to a free end this near; Join Rails reaches this far; Rail from Edges' rails join this near.
constexpr float kEndSnapReach = 24.0F;
constexpr float kJoinRailsReach = 64.0F;
constexpr float kNewRailJoinReach = 12.0F;
// Ends this close are merged by Join Rails (one point), farther ones linked by a segment.
constexpr float kMergeReach = 2.0F;
// Turns smaller than this between edges in a line are left out of a rail (one segment).
constexpr float kStraightDegrees = 3.0F;

// An edge of the collision: its ends, the terrain byte of the face it was picked from (the upward one) and that face's
// collision object (the scenery piece's checksum).
struct Edge {
    V3 a = {};
    V3 b = {};
    uint8_t terrain = 0;
    uint32_t sector = 0;
};
// A run of edges joined end to end: its points in order, the edges in it (indices), whether it closes on itself, its
// length.
struct Run {
    std::vector<V3> points;
    std::vector<int> edges;
    bool closed = false;
    float length = 0.0F;
};
// The edges joined where their ends meet (within `weld`): from each end (a point with one edge, or a junction of three
// or more) along the edges until another, then what is left (loops). Edges shorter than `weld` are left out.
std::vector<Run> join_edges(const std::vector<Edge>& edges, float weld);
// Points within `weld` of one another made one: each point's index into `welded` (the first of each kept).
std::vector<int> weld_points(const std::vector<V3>& points, float weld, std::vector<V3>* welded);
// The points of a run with the ones where it turns less than `degrees` left out (the ends kept; a loop's first point
// too when it turns there).
std::vector<V3> drop_straight(const std::vector<V3>& points, bool closed, float degrees);
// The distance from a point to a segment.
float segment_distance(V3 point, V3 a, V3 b);
// Whether two edges are one (either way round, ends within `weld`).
bool same_edge(const Edge& a, const Edge& b, float weld);
// A face's side nearest a point (0: corners 0-1, 1: 1-2, 2: 2-0) among those `allowed` (bits; none allowed: all).
int nearest_side(std::span<const V3, 3> corners, V3 point, unsigned allowed);
// The grind terrain for a face's terrain (TERRAIN_WOODPLYWOOD -> TERRAIN_GRINDWOOD; a grind terrain itself; anything
// else TERRAIN_GRINDCONC).
const char* grind_terrain_for(const std::string& terrain);
// The game's terrain names (scripts\game\terrain.qb's Terrain_Types, in its order).
std::span<const char* const> terrain_names();
// The most common value (the first of them on a tie; 0 for none).
uint32_t most_common(const std::vector<uint32_t>& values);

// Auto Rails: an edge of the collision met by the scan, with what decides whether it is grindable.
struct AutoEdge {
    Edge edge;
    float degrees = 0.0F;  // the angle between its two faces' normals (an open edge: 90)
    float slope = 0.0F;    // degrees from level
    bool clear = false;    // nothing just above it (a skater's height)
    bool drop = false;     // the ground falls away beside it (not a seam between two pieces of floor)
    bool railed = false;   // a rail already runs along it
};
struct Limits {
    float degrees = 0.0F;   // the least angle between the faces
    float min_length_ = 0.0F; // the shortest rail kept
    float max_slope_ = 0.0F;  // the steepest edge kept
};
bool passes(const AutoEdge& auto_edge, const Limits& limits);
// The rails the scan's edges make: those passing joined, runs shorter than the least length left out.
std::vector<Run> auto_runs(const std::vector<AutoEdge>& edges, const Limits& limits);

// --- the game side ---------------------------------------------------------------------------------------------------

// The actions (once, from ux9::prepare_all).
void prepare_all();
// The editor starting / stopping.
void kickoff();
void halt();
// Once a frame while the editor runs (with the actions): the keys, the windows' requests.
void tick_frame();
// A tool armed or its window open (Escape goes to Cancel first), and Escape.
bool occupied();
bool get_cancel();
// Rail from Edges picking (the status bar's hints).
bool picking();
// The mouse ray each frame, and a left click (true: taken).
void get_hover(V3 origin, V3 unit_dir, bool have_ray);
bool get_click(V3 origin, V3 unit_dir);
// Over the view: the edges hovered and picked, the snap rings, Auto Rails' preview.
void add_overlay(const overlays::Painter& painter);
// The windows (Rail from Edges' panel, Auto Rails').
void get_windows(float view_x, float view_y, float view_width, float scale);
// For the screenshot sidecar ("" when nothing to say).
std::string describe();
// A rail point dragged (level_editor_drag.cpp): the place it is wanted at, moved onto another rail's free end within
// reach when it is a free end itself and moves alone (`entire`: the whole rail moves - no snapping; Alt: none).
V3 end_snap(uint32_t node, V3 wanted, bool entire);
// The drag let go (its Move step pushed): the end snapped to is joined - folded into the move's undo step.
void drag_done();

} // namespace frontend::level_editor::rail_tools
