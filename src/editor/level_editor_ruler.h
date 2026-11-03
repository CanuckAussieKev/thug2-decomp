#pragma once

#include <string>
#include <utility>
#include <vector>

#include "engine/maths/vec2.h"
#include "editor/level_editor.h"

namespace frontend::level_editor::overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's ruler (ours; see level_editor.h). M arms it: the next two clicks in the view (on
// collision - the game's collision ray from the camera through the cursor - snapped to the nearest corner of the face's
// piece within kSnapPixels, Alt: no snap) are its ends; the distance, the horizontal gap and the height difference are
// drawn and stay until Shift+M clears them (a third click starts a new measurement; M again disarms it, the measurement
// stays). While armed, clicks measure instead of selecting. Shown with the Overlays menu's Ruler.
namespace frontend::level_editor::ruler {

// --- the pieces (pure) -------------------------------------------------------------------------------------------

struct Measure {
    float distance = 0.0f;   // straight
    float horizontal = 0.0f; // in x / z
    float height = 0.0f;     // b.y - a.y (up positive)
};
Measure measure_of(V3 a, V3 b);
// The candidate nearest the cursor (x, y) within `radius` pixels (-1: none).
int nearest_within(const std::vector<std::pair<float, float>>& pixels, maths::Vec2 at, float radius);
constexpr float kSnapPixels = 12.0f;
// The text of a measurement ("312 (26.0 ft) - across 300 (25.0 ft), up 86 (7.2 ft)").
std::string describe(const Measure& m);

// --- the game side ----------------------------------------------------------------------------------------------

bool get_armed();
void set_armed(bool on);
// The measurement dropped (Shift+M).
void get_clear();
// Once a frame while armed: where a click would put an end - the collision under the cursor (`origin`, `unit_dir`: the
// mouse ray; `x`, `y` the cursor in back buffer pixels), snapped unless `no_snap`.
void get_hover(V3 origin, V3 unit_dir, float x, float y, bool no_snap);
// A click while armed: an end put where Hover found (nothing when it found no collision). Returns what it did.
std::string get_click();
void add_overlay(const overlays::Painter& painter);
// The measurement now ("" when there's none) - the status bar, the screenshot sidecar.
std::string describe();

} // namespace frontend::level_editor::ruler
