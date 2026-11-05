#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

// The screen's shape and full-screen effects (Nx engine, PC): aspect ratio, field-of-view factor, letterbox and
// motion blur. PartyMod (whose code this contains: MIT, (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md) changes most
// of these: widescreen support, blur off by default.
namespace render {

// The 2D layouts things are placed on whatever the window's size: 640 x 480 (the display factors and 2D drawing
// scale from it) and the screen elements' (menus, HUD, projected points) 640 x 448.
constexpr float kLayoutWidth = 640.0F;
constexpr float kLayoutHeight = 480.0F;
constexpr float kElementLayoutHeight = 448.0F; // (the same width)
constexpr float kLayoutCentreX = kLayoutWidth / 2;
constexpr float kLayoutCentreY = kLayoutHeight / 2;
constexpr float kElementLayoutCentreY = kElementLayoutHeight / 2;
constexpr int kLayoutWidthPixels = 640; // the same as whole pixels
constexpr int kLayoutHeightPixels = 480;
constexpr int kElementLayoutHeightPixels = 448;

// The career's current level (0 = the front end); 0 without the skate module (0x007CE478, its career +0x20,
// that's +0x630).
uint8_t current_level();
// The aspect the screen is fitted to: 4:3 in the front end, the window's shape in a level. In the front end
// this is the 2D menu layout's shape (see get_front_end_widescreen).
float desired_aspect_ratio();

// thug2-decomp.ini [features] widescreen_menus (default 1): in the front end the 3D scene behind the menus fills
// the window (widescreen field of view, no pillarbox bars) while the 2D menu layout stays 4:3 and centred. With 0,
// the front end is pillarboxed 4:3 as in PartyMod. Read from the ini once; self-tests set it for their run.
bool& widescreen_menus();
// Whether that applies right now: the setting is on and we're in the front end.
bool get_front_end_widescreen();
// The aspect the 3D view is fitted to: the window's shape when get_front_end_widescreen(), else desired_aspect_ratio().
float desired_3d_aspect_ratio();

// 0x004A0780
void set_screen_aspect_ratio(float aspect);
// 0x004A0790: the screen's aspect as last set (0x00701340).
float fetch_view_ratio();
// 0x004A07A0 / 0x004A07B0: the cameras' default horizontal angle in degrees (0x00701348; set at start-up from the
// script global camera_fov).
void set_screen_angle(float angle);
// 0x004A07C0: the factor the horizontal field of view is scaled by.
float fetch_fov_ratio();
// 0x004B2330: the motion blur amount (0 = off, up to 255).
void set_screen_blur(uint32_t amount);
// 0x004B2440
void set_letterbox(bool letterbox);

// 0x0048C170: SetScreenBlur <amount>
bool cfunc_set_screen_blur(script::Struct* params, script::Context context);

} // namespace render
