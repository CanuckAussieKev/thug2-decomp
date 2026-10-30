#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/game_global.h"

// The per-frame 3D render (Nx engine, PC/D3D9): the camera's matrices, the world passes per viewport and the
// motion blur pass. PartyMod (whose code this contains: MIT, (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md) changes
// all three (widescreen viewports, black bars, a further far plane); the original's code is given in comments.
namespace Nx {

// The camera's state (globals, 0x00786CA8).
struct RenderCamera {
    float nearZ;      // 0x00786CA8
    float farZ;       // 0x00786CAC
    float angle;      // 0x00786CB0: horizontal field of view, degrees
    float nearWidth;  // 0x00786CB4: the view's size at the near plane
    float nearHeight; // 0x00786CB8
    uint8_t customTransforms; // 0x00786CBC: the view / projection set by hand, not the camera's (shadow.cpp)
    bool clearColourAndDepth;  // 0x00786CBD (a global the camera's lie around)
    uint8_t letterboxActive; // 0x00786CBE
    uint8_t unknown17;
    uint32_t backgroundColour; // 0x00786CC0 (another)
    float position[3]; // 0x00786CC4
    uint8_t unknown28[0x0C];
    float backward[3]; // 0x00786CDC: the orientation's row 2
    float up[3];       // 0x00786CE8: row 1
};
static_assert(offsetof(RenderCamera, position) == 0x1C, "0x00786CC4");
static_assert(offsetof(RenderCamera, backward) == 0x34, "0x00786CDC");

// The view's half-angle shape, for culling (0x00786E18).
struct ViewShape {
    float tanX;  // tan(half the horizontal angle)
    float tanY;  // -tanX / (aspect / 1.1): the vertical one, negated
    float sinX;  // sin and cos of the two half-angles
    float sinY;
    float cosX;
    float cosY;
};

// The device's viewport (D3DVIEWPORT9, 0x00786C90).
struct DeviceViewport {
    uint32_t x, y, width, height;
    float minZ, maxZ;
};

// While letterboxed: the viewport (0x00786C90) set to the 640x480 layout's fitted area on the back buffer
// (SetLetterbox's scales), centred.
void SetDisplayRegion();
// The 2D pass (0x004DFB90), then black bars over what was drawn outside the area fitted to the aspect wanted
// (left and right on wider back buffers, top and bottom on narrower ones).
void Draw2DWithBlackBars();

// 0x004DB200: the camera's view and projection. `orientation`'s rows 1 and 2 are the camera's up and backward
// directions; `angle` is the horizontal field of view in degrees; `zoom` scales the projection (1 = none);
// `infinite` pushes the far plane to infinity.
void SetCamera(const Mth::Matrix* orientation, const Mth::Vector* position, float angle, float aspect, float zoom,
               bool infinite);

// 0x004DF390: the motion blur pass - blends the previous frame (one of two textures) over the screen.
void DrawScreenBlur();

// 0x004B2D80: the frame's 3D: per viewport with a camera, the world, the sky, the later passes; then the blur and
// the 2D over the whole screen.
void RenderWorld();

GAME_GLOBAL_EXTERN(bool, g_radeon8500);
GAME_GLOBAL_EXTERN(int, g_framesRendered);
GAME_GLOBAL_EXTERN(Mth::Matrix, g_worldMatrix);
GAME_GLOBAL_EXTERN(Mth::Matrix, g_viewMatrix);
GAME_GLOBAL_EXTERN(Mth::Matrix, g_projectionMatrix);
GAME_GLOBAL_EXTERN(Mth::Matrix, g_headingMatrix);
GAME_GLOBAL_EXTERN(int, g_frameTime);
GAME_GLOBAL_EXTERN(int, g_screenCopyIndex);
GAME_GLOBAL_EXTERN(int, g_backBufferWidth);
GAME_GLOBAL_EXTERN(int, g_backBufferHeight);
GAME_GLOBAL_EXTERN(int, g_drawCalls);
GAME_GLOBAL_EXTERN(int, g_drawnTriangles);
GAME_GLOBAL_EXTERN(float, g_fogStart);
GAME_GLOBAL_EXTERN(float, g_fogEnd);

GAME_GLOBAL_EXTERN(RenderCamera, g_camera);
GAME_GLOBAL_EXTERN(ViewShape, g_viewShape);

} // namespace Nx
