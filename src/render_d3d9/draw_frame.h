#pragma once

#include "engine/gfx/render_camera.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"

// The per-frame 3D render (Nx engine, PC/D3D9): the camera's matrices, the world passes per viewport and the
// motion blur pass. PartyMod (whose code this contains: MIT, (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md) changes
// all three (widescreen viewports, black bars, a further far plane); the original's code is given in comments.
namespace render {

// While letterboxed: the viewport (0x00786C90) set to the 640x480 layout's fitted area on the back buffer
// (set_letterbox's scales), centred.
void set_display_region();
// The 2D pass (0x004DFB90), then black bars over what was drawn outside the area fitted to the aspect wanted
// (left and right on wider back buffers, top and bottom on narrower ones).
void draw_2d_with_black_bars();

// 0x004DB200: the camera's view and projection. `orientation`'s rows 1 and 2 are the camera's up and backward
// directions; `angle` is the horizontal field of view in degrees; `zoom` scales the projection (1 = none);
// `infinite` pushes the far plane to infinity.
void choose_camera(const maths::Mat4* orientation, const maths::Vec4* position, CameraLens lens,
                   bool infinite);

// 0x004DF390: the motion blur pass - blends the previous frame (one of two textures) over the screen.
void draw_screen_blur();

// 0x004B2D80: the frame's 3D: per viewport with a camera, the world, the sky, the later passes; then the blur and
// the 2D over the whole screen.
void draw_world();

// The frame's camera, matrices, viewport and counters (draw_frame.cpp's record; the original's globals).

} // namespace render
