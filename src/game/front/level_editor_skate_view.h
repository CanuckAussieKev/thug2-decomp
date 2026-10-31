#pragma once

#include <cstdint>
#include <string>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's skateability overlay (ours; see level_editor.h - the UX research's skate idea #5):
// the Overlays menu's "Skateability" (Alt+K) colours the level's surfaces near the camera by what a skater can do on
// them, with a legend (counts per kind). From the collision's face flags and the face's slope, in the order the
// skater's own test takes them (0x005003A0, read for this): 0x01 skatable whatever its slope; else 0x02 not skatable;
// else 0x08 vert (skatable, launches straight up); else 0x04 wall-ridable (not skatable); else skatable only when its
// normal's y is at least sin(the physics angle) - the script float 0x3EEDE4D3 times the degrees-to-radians constant at
// 0x00645464, read from the game (as 0x005003A0 reads them). Skatable faces are split into floor (flat - normal y
// 0.97 or more) and bank / ramp. Faces the skater passes through (0x10) aren't drawn, except the level's trigger
// volumes that kill: a kill zone red, water blue (level_editor_triggers.h's kinds) - and the editor's own kill / water
// triggers outlined. Grind rails (the rail manager's active segments - THUG2 grinds rails only, no collision edge
// grinds by itself) drawn as thick cyan lines.
// Cheap enough for a frame: each sector's faces classified once (cached, made again when its collision's box or first /
// last vertex changes, as the collision view's cache), the nearest sectors within 3000 units first, at most 12000
// faces drawn, under the lines (ImGui's background list) - and the collision view (C) can be on with it.
namespace Front::LevelEditor::SkateView {

// --- the pure parts --------------------------------------------------------------------------------------------------

enum class Kind : uint8_t { None, Floor, Bank, Vert, Wallride, NotSkatable, Kill, Water, kCount };
constexpr int kKinds = static_cast<int>(Kind::kCount);
// A face's kind: its flags, its unit normal's y, the skatable threshold (sin of the physics angle); `trigger`: the kind
// of the level trigger it is a face of (0 none, 1 kill, 2 water).
Kind Classify(uint16_t flags, float normalY, float threshold, int trigger);
uint32_t Colour(Kind kind); // ARGB, see-through
const char* Name(Kind kind);
// The game's threshold from its two numbers (the angle and the factor to radians): sin(angle * factor), within 0..1
// (a missing angle: 45 degrees).
float Threshold(float angle, float factor);

// --- the game side ----------------------------------------------------------------------------------------------

bool Shown(); // (the Overlays menu's bit)
void SetShown(bool on);
void AddOverlay(const Overlays::Painter& painter);
// The legend (ImGui; bottom right of the view) while it's shown.
void Legend(float viewX, float viewY, float viewWidth, float viewHeight, float scale);
void Stop(); // (the caches dropped)

} // namespace Front::LevelEditor::SkateView
