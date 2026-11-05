#pragma once

#include <cstdint>
#include <string>

#include "editor/level_editor.h"
#include "engine/render/coll_obj.h"

namespace frontend::level_editor::overlays {
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
namespace frontend::level_editor::skate_view {

// --- the pure parts --------------------------------------------------------------------------------------------------

enum class Kind : uint8_t { kNone, kFloor, kBank, kVert, kWallride, kNotSkatable, kKill, kWater, kCount };
constexpr int kKinds = static_cast<int>(Kind::kCount);
// What a face is classified by: its flags, its normal's y, the steepness threshold and the trigger flag.
struct FaceClass {
    render::FaceFlag flags{};
    float normal_y_{};
    float threshold{};
    int trigger{};
};

// A face's kind: its flags, its unit normal's y, the skatable threshold (sin of the physics angle); `trigger`: the kind
// of the level trigger it is a face of (0 none, 1 kill, 2 water).
Kind get_classify(FaceClass face_class);
uint32_t get_colour(Kind kind); // ARGB, see-through
const char* get_name(Kind kind);
// The game's threshold from its two numbers (the angle and the factor to radians): sin(angle * factor), within 0..1
// (a missing angle: 45 degrees).
float get_threshold(float angle, float factor);

// --- the game side ----------------------------------------------------------------------------------------------

bool get_shown(); // (the Overlays menu's bit)
void set_shown(bool on);
void add_overlay(const overlays::Painter& painter);
// The legend (ImGui; bottom right of the view) while it's shown.
void get_legend(float view_x, float view_y, float view_width, float view_height, float scale);
void halt(); // (the caches dropped)

} // namespace frontend::level_editor::skate_view
