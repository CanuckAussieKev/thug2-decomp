#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Ui {
struct Overlay;
} // namespace Front::LevelEditor::Ui

// NOT ORIGINAL - the level editor's arrange tools (ours; see level_editor.h; the record: docs/pending/editor-ux7.md):
//   - Align / distribute (Roblox Studio's Align tool): the selection's pieces (each object, each scenery piece with
//     its shadows) lined up by their min / centre / max on X / Y / Z - along the world's axes or the first one's own
//     (Local) - on the selection's bounds or on the first-selected piece; or spread evenly along an axis (their centres
//     evenly apart, the first and last staying; or equal gaps between them). One undo step; adjustable afterwards
//     (level_editor_adjust.h).
//   - Replace selection with (Unreal's Replace Selected Actors): the selected scenery pieces / object copies replaced by
//     copies of the Add palette's highlighted piece / object, each keeping its place, quarter turns / turn and scale
//     (scenery: the copy's box bottom-centred where the piece's was; the pieces hidden); a level's own object (it can't be
//     removed) gets the object's model instead (its model= - the Inspector's model swap). One undo step.
//   - Edge-to-edge snapping (THPS Create-a-Park): while a scenery piece is moved (no other snapping held), its box's
//     faces snap flush to a neighbouring piece's box faces within a few units - side by side along X / Z, stacked on
//     its top or with its top level with the neighbour's along Y; the faces met are highlighted.
//   - Live measurements while dragging: the selection's size, how far it moved, its height above the ground below it and
//     the distance to the nearest grind rail / ledge, next to the cursor (and a line to that rail's nearest point).
//   - The placement ghost: while the Add palette (B) or an armed Place palette (N) entry is up, a translucent preview of
//     what a click places - the piece's / object's box (turned to the surface's slope for objects with "Align to
//     slope"), a node's marker - follows the cursor on the surface; with "Click places" a click in the view places the
//     Add palette's piece there (Shift: more).
//   - Auto-rail along this edge (Alt+R, a scenery piece selected): the piece's top outline - the edges of its upward
//     faces where the surface falls away (convex lips), from its meshes' triangles - drawn; a click on one lays a grind
//     rail exactly along it (level_editor_chains.h's points: one undo step).
// The window: F2 (the command search: "Arrange"); the keys are in the action registry (level_editor_actions.h). Pure parts self-tested: src/hook/selftests_editor_arrange.cpp.
namespace Front::LevelEditor::Arrange {

// --- the pure parts ---------------------------------------------------------------------------------------------

enum class Edge : uint8_t { Min, Centre, Max };
enum class Reference : uint8_t { Bounds, First }; // the selection's bounds, or the first-selected piece
// A piece's extent along an axis.
struct Span {
    float lo = 0.0f, hi = 0.0f;
};
// The span of points (a box's corners) along a direction (unit).
Span SpanAlong(const std::vector<V3>& points, V3 axis);
// How far each piece moves along the axis so that its `edge` lies on the reference's: the edge of all the spans' bounds,
// or the first span's own.
std::vector<float> AlignMoves(const std::vector<Span>& spans, Edge edge, Reference reference);
// Spread along the axis (the pieces in the order of their centres; the first and last stay): `gaps` false - the
// centres evenly apart; true - the gaps between neighbours equal (sizes differ). Fewer than three pieces: none move.
std::vector<float> DistributeMoves(const std::vector<Span>& spans, bool gaps);

// An axis-aligned box.
struct Aabb {
    V3 min = {}, max = {};
};
// A face met by edge snapping: on `axis`, at the coordinate `at`, of the neighbour `other` (its index).
struct Contact {
    int axis = 0;
    float at = 0.0f;
    int other = -1;
};
// The move `delta` of the box `moving` changed so that its faces lie flush with the nearest neighbour's (within
// `reach`): along X / Z side by side (its min onto a neighbour's max or the other way - the neighbour overlapping it
// on the other two axes, within reach), along Y stacked (its bottom onto a neighbour's top - overlapping it across)
// or its top level with a neighbour's top (next to it - within reach across). `axes`: the axes it may snap on (bits:
// 1 X, 2 Y, 4 Z). The faces met added to `contacts` (may be null).
V3 EdgeSnap(const Aabb& moving, V3 delta, const std::vector<Aabb>& others, float reach, int axes,
            std::vector<Contact>* contacts);
// The reach in units for a camera `distance` away: 6 near, up to 24 far.
float SnapReach(float distance);

// The distance from a box to a segment (0 when it crosses the box).
float BoxSegmentDistance(const Aabb& box, V3 a, V3 b, V3* nearest = nullptr);
// The readout next to the cursor: "120 x 40 x 300", "moved 125", "above ground 32", "rail 85".
struct Readout {
    V3 size = {};
    float moved = 0.0f;
    bool haveGround = false;
    float above = 0.0f;
    bool haveRail = false;
    float rail = 0.0f;
    bool ledge = false; // (the nearest is a ledge, not a rail)
};
std::vector<std::string> ReadoutLines(const Readout& readout);

// The top outline (auto-rail): the edges of the upward-facing triangles (|normal.y| >= kTopNormalY, either winding)
// that no other upward triangle shares and where the surface falls away (the edge's other triangles - walls - go down
// from it; none: an open edge), joined into runs where they meet end to end turning less than kJoinDegrees, straight
// stretches made one segment; runs shorter than `minLength` left out. `triangles`: three points each; points within
// `weld` are one.
constexpr float kTopNormalY = 0.7f;
constexpr float kJoinDegrees = 30.0f;
struct Run {
    std::vector<V3> points;
    float length = 0.0f;
};
std::vector<Run> TopEdges(const std::vector<V3>& triangles, float weld, float minLength);
// The run nearest (x, y) in pixels within `grab` (its index; -1 none) - `project` as the gizmos'.
int RunAt(const std::vector<Run>& runs, const std::function<bool(V3, float*, float*)>& project, float x, float y,
          float grab);

// --- the settings (this session) ----------------------------------------------------------------------------------

struct Settings {
    bool window = false;       // the Arrange window (M)
    int axis = 0;              // 0 / 1 / 2: X / Y / Z
    Edge edge = Edge::Min;
    Reference reference = Reference::Bounds;
    bool local = false;        // along the first piece's own axes
    bool gaps = false;         // distribute: equal gaps (else evenly spaced centres)
    bool edgeSnap = true;      // edge-to-edge snapping while moving scenery
    bool measure = true;       // the readout while dragging
    bool ghost = true;         // the placement ghost
    bool clickPlaces = true;   // a click in the view places the Add palette's piece
    bool ghostSlope = false;   // the ghost (and the object placed) turned to the surface's slope
};
Settings& Current();

// --- the game side ------------------------------------------------------------------------------------------------

// Align / distribute the selection now (the settings' axis, edge, reference, space). False: nothing to do (said).
bool Align(int axis, Edge edge, Reference reference, bool local);
bool Distribute(int axis, bool gaps, bool local);
// Replace the selection with the Add palette's highlighted piece / object. False: refused (said).
bool Replace();
// Edge snapping while moving (level_editor.cpp's drag): the selection's scenery wanted at `wanted` (from `pos`, where
// it is), `axes` as EdgeSnap's - the place it snaps to.
V3 EdgeSnapped(V3 pos, V3 wanted, int axes);
// Auto-rail (Alt+R): picking an edge of the selected scenery's top outline; a click lays the rail. Click: whether the click
// was taken.
bool EdgePicking();
void SetEdgePicking(bool on);
bool EdgeClick(float x, float y);
// A click in the view while the Add palette is open (with Click places): its piece placed at the cursor (`more`:
// Shift - the palette stays open). False: not taken.
bool PlaceClick(bool more);
// Once a frame while the editor runs (after the editor's keys): the keys M, Alt+R, Ctrl+Alt+D handled by the editor.
void Update();
// What's drawn over the view: the ghost, the readout, the edges met by snapping, the top outline.
void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                Ui::Overlay* overlay);
// The UI (level_editor_arrange_ui.cpp): the window, and the Add palette's extra row (Replace, Click places).
void Window(float uiScale);
void BrowserExtras(float uiScale);
// What the window's buttons asked for, done (Update calls it).
void RunQueued();
// A new level / the editor stopping: caches dropped.
void Forget();

} // namespace Front::LevelEditor::Arrange
