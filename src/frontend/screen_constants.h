#pragma once

// The front end's (and the animation controller's) float constants from the original's read-only data, by name.
// The first group is defined in screen_element.cpp; the second are defined where their other users are.

namespace frontend {
extern float g_aspect_to_layout;  // 0x00649FEC
extern float g_wide_screen;       // 0x00649FE4
extern float g_minus_inv448;      // 0x00649FE0
extern float g_inv448;            // 0x00649FDC
extern float g_inv896;            // 0x00649FD0
extern float g_inv1280;           // 0x00649FD8
extern float g_inv_two_pi;        // 0x0064A044
extern float g_two_pi;            // 0x0064545C
extern float g_pi;                // 0x00649FC8
extern float g_layout_top;        // 0x0064A258
extern float g_layout_bottom;     // 0x0064A254
extern float g_shadow_behind;     // 0x0064541C
extern float g_sixteenth;         // 0x00645A84
extern float g_frames_per_second; // 0x00645EA0
} // namespace frontend

namespace commands {
extern float g_degree_to_radian_ratio;    // 0x00645464, defined with its other users
extern float g_script_radians_to_degrees; // 0x00645468, defined with its other users
} // namespace commands
namespace goals {
extern float g_goal_life_seconds_per_millisecond; // 0x006461D8, defined with its other users
} // namespace goals
namespace objects {
extern float g_float_zero;                    // 0x0066D520, defined with its other users
extern float g_anim_movies_seconds_per_frame; // 0x006463B8, defined with its other users
extern float g_anim_movies_one;               // 0x006453A8, defined with its other users
} // namespace objects
namespace render {
extern float g_one_percent; // 0x0064544C, defined with its other users
} // namespace render
namespace modules {
extern float g_competition_tiny; // 0x006453D4, defined with its other users
} // namespace modules
namespace gfx {
extern float g_half;     // 0x006453BC, defined with its other users
extern float g_thousand; // 0x006453C0, defined with its other users
extern float g_two_to32; // 0x006453B4, defined with its other users
} // namespace gfx
