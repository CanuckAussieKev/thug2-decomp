#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's two-pick gap wizard (ours; see level_editor.h - the UX research's skate idea #1,
// after THPS2's park editor: pick the two pieces a gap joins, then name it, score it and give it a type).
//
// The Gap wizard (Alt+J, or "Gap wizard" in the command search): click where the gap starts - a
// rail (the rail manager's segment nearest the cursor, within 14 pixels), a lip (a sloped or vert face's top), a wall
// (wallride) or an area (a floor) - then where it ends; a small form asks for its name, its score (50 - 5000) and its
// type, and makes the gap with phase 5's trigger code (level_editor_triggers.h: Triggers::AddGapPair - a start volume
// with StartGap GapID= flags=[...] and an end volume with EndGap GapID= score= text=, both nodes of the editor's overlay
// array, saved as `trigger` records, baked by apply_layout.ts like any). Each end is a gate (a thin box the skater
// passes through) facing the other end, sized for what was picked: a rail's 120 x 120 across the rail, along it; a
// lip's 300 wide, 400 high above it; a wall's 300 x 300 a little off it; an area's 300 x 300 on the ground. The start is
// selected afterwards - its Inspector section and resize handles adjust it, "Select the other end" the end.
//
// Types (THUG2's StartGap flags - Triggers::GapFlags): Air [CANCEL_GROUND] (the editor's default gap), Grind
// [REQUIRE_RAIL, CANCEL_GROUND], Lip [REQUIRE_LIP], Manual [PURE_MANUAL], Wallride [REQUIRE_WALLRIDE, CANCEL_GROUND].
// Skitch: no gap flag of THUG2's means a skitch - not offered. The flags can be changed in the Inspector afterwards.
namespace Front::LevelEditor::GapWizard {

// --- the pure parts --------------------------------------------------------------------------------------------------

enum class SpotKind : uint8_t { Area, Lip, Wall, Rail };
const char* SpotName(SpotKind kind);
// What a clicked face is: a floor (normal y >= 0.7, not vert) an area, a near-vertical one (normal y < 0.2, not vert) a
// wall, else (a slope, or a vert face) a lip.
SpotKind KindOfFace(float normalY, uint16_t faceFlags);

struct Spot {
    SpotKind kind = SpotKind::Area;
    V3 point = {};
    V3 normal = {0.0f, 1.0f, 0.0f}; // (the face's)
    V3 along = {};                  // (a rail's direction)
};

enum class Type : uint8_t { Air, Grind, Lip, Manual, Wallride };
constexpr int kTypes = 5;
const char* TypeName(Type type);
std::vector<std::string> FlagsFor(Type type);
// The type the two spots suggest: two rails a grind, a wall in it a wallride, else air.
Type Suggest(const Spot& start, const Spot& end);

// A gate at a spot, facing the other end (`toward`): its centre, heading (radians about up: 0 along +z, as a node's)
// and size (across, up, through).
struct Gate {
    V3 pos = {};
    float yaw = 0.0f;
    V3 size = {};
};
Gate GateFor(const Spot& spot, V3 toward);
// A heading (radians, 0 = +z, positive towards +x) of a direction's horizontal part (0 when it has none).
float HeadingOf(V3 direction);

constexpr int kMinScore = 50, kMaxScore = 5000;
int ClampScore(int score);

// --- the game side ----------------------------------------------------------------------------------------------

// Armed: the next clicks pick the ends; then the form until made or cancelled.
bool Armed();
void SetArmed(bool on);
// Picking its ends (not the form).
bool Picking();
// Escape: what it was doing given up (false: nothing to give up).
bool Cancel();
// The mouse ray each frame (the cursor's pixel in the back buffer) and a click (true: taken).
void Hover(V3 origin, V3 unitDir, float x, float y);
bool Click(V3 origin, V3 unitDir, float x, float y);
// The ends picked and the gates they'd make, over the view.
void AddOverlay(const Overlays::Painter& painter);
// The form (ImGui; level_editor_ux9_ui.cpp).
void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale);
// Once a frame while the editor runs: what the form asked for.
void Update();

} // namespace Front::LevelEditor::GapWizard
