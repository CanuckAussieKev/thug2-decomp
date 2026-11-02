#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

// The per-frame 3D render (Nx engine, PC/D3D9): the camera's matrices, the world passes per viewport and the
// motion blur pass. PartyMod (whose code this contains: MIT, (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md) changes
// all three (widescreen viewports, black bars, a further far plane); the original's code is given in comments.
namespace render {

// The camera's state (globals, 0x00786CA8).
struct RenderCamera {
    float near_z_;                // 0x00786CA8
    float far_z_;                 // 0x00786CAC
    float angle;                  // 0x00786CB0: horizontal field of view, degrees
    float near_width_;            // 0x00786CB4: the view's size at the near plane
    float near_height_;           // 0x00786CB8
    uint8_t custom_transforms_;   // 0x00786CBC: the view / projection set by hand, not the camera's (shadow_base.cpp)
    bool clear_colour_and_depth_; // 0x00786CBD (a global the camera's lie around)
    uint8_t letterbox_active_;    // 0x00786CBE
    uint8_t unknown17;
    uint32_t background_colour_; // 0x00786CC0 (another)
    std::array<float, 3> position;           // 0x00786CC4
    std::array<uint8_t, 0x0C> unknown28;
    std::array<float, 3> backward; // 0x00786CDC: the orientation's row 2
    std::array<float, 3> up;       // 0x00786CE8: row 1
};
static_assert(offsetof(RenderCamera, position) == 0x1C, "0x00786CC4");
static_assert(offsetof(RenderCamera, backward) == 0x34, "0x00786CDC");

// The view's half-angle shape, for culling (0x00786E18).
struct ViewShape {
    float tan_x_; // tan(half the horizontal angle)
    float tan_y_; // -tanX / (aspect / 1.1): the vertical one, negated
    float sin_x_; // sin and cos of the two half-angles
    float sin_y_;
    float cos_x_;
    float cos_y_;
};

// The device's viewport (D3DVIEWPORT9, 0x00786C90).
struct DeviceViewport {
    uint32_t x, y, width, height;
    float min_z_, max_z_;
};

// While letterboxed: the viewport (0x00786C90) set to the 640x480 layout's fitted area on the back buffer
// (set_letterbox's scales), centred.
void set_display_region();
// The 2D pass (0x004DFB90), then black bars over what was drawn outside the area fitted to the aspect wanted
// (left and right on wider back buffers, top and bottom on narrower ones).
void draw_2d_with_black_bars();

// 0x004DB200: the camera's view and projection. `orientation`'s rows 1 and 2 are the camera's up and backward
// directions; `angle` is the horizontal field of view in degrees; `zoom` scales the projection (1 = none);
// `infinite` pushes the far plane to infinity.
void choose_camera(const maths::Mat4* orientation, const maths::Vec4* position, float angle, float aspect, float zoom,
                   bool infinite);

// 0x004DF390: the motion blur pass - blends the previous frame (one of two textures) over the screen.
void draw_screen_blur();

// 0x004B2D80: the frame's 3D: per viewport with a camera, the world, the sky, the later passes; then the blur and
// the 2D over the whole screen.
void draw_world();

// The frame's camera, matrices, viewport and counters (draw_frame.cpp's record; the original's globals).
DeviceViewport& device_viewport();
RenderCamera& render_camera();
ViewShape& view_shape();
bool radeon8500();
void set_radeon8500(bool value);
int frames_rendered();
maths::Mat4& world_matrix();
maths::Mat4& view_matrix();
maths::Mat4& projection_matrix();
maths::Mat4& heading_matrix();
int step_seconds();
int screen_copy_index();
void set_screen_copy_index(int value);
int back_buffer_width();
void set_back_buffer_width(int value);
int back_buffer_height();
void set_back_buffer_height(int value);
uint32_t& draw_calls();
uint32_t& drawn_triangles();
float fog_begin_distance();
void set_fog_begin_distance(float value);
float fog_end();
void set_fog_end(float value);

} // namespace render
