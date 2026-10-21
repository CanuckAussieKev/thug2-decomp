#pragma once

#include <string>
#include <utility>
#include <vector>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's ruler (ours; see level_editor.h). M arms it: the next two clicks in the view (on
// collision - the game's collision ray from the camera through the cursor - snapped to the nearest corner of the face's
// piece within kSnapPixels, Alt: no snap) are its ends; the distance, the horizontal gap and the height difference are
// drawn and stay until Shift+M clears them (a third click starts a new measurement; M again disarms it, the measurement
// stays). While armed, clicks measure instead of selecting. Shown with the Overlays menu's Ruler.
namespace Front::LevelEditor::Ruler {

// --- the pieces (pure, self-tested: src/hook/selftests_editor_ux4.cpp) -------------------------------------------

struct Measure {
    float distance = 0.0f;   // straight
    float horizontal = 0.0f; // in x / z
    float height = 0.0f;     // b.y - a.y (up positive)
};
Measure MeasureOf(V3 a, V3 b);
// The candidate nearest the cursor (x, y) within `radius` pixels (-1: none).
int NearestWithin(const std::vector<std::pair<float, float>>& pixels, float x, float y, float radius);
constexpr float kSnapPixels = 12.0f;
// The text of a measurement ("312 (26.0 ft) - across 300 (25.0 ft), up 86 (7.2 ft)").
std::string Describe(const Measure& m);

// --- the game side ----------------------------------------------------------------------------------------------

bool Armed();
void SetArmed(bool on);
// The measurement dropped (Shift+M).
void Clear();
// Once a frame while armed: where a click would put an end - the collision under the cursor (`origin`, `unitDir`: the
// mouse ray; `x`, `y` the cursor in back buffer pixels), snapped unless `noSnap`.
void Hover(V3 origin, V3 unitDir, float x, float y, bool noSnap);
// A click while armed: an end put where Hover found (nothing when it found no collision). Returns what it did.
std::string Click();
void AddOverlay(const Overlays::Painter& painter);
// The measurement now ("" when there's none) - the status bar, the screenshot sidecar.
std::string Describe();

} // namespace Front::LevelEditor::Ruler
