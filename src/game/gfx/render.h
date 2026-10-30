#pragma once

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/game_global.h"

// The per-frame 3D render (Nx engine, PC/D3D9): the camera's matrices, the world passes per viewport and the
// motion blur pass. PartyMod (whose code this contains: MIT, (c) 2024 PARTYMANX - see THIRD_PARTY_NOTICES.md) changes
// all three (widescreen viewports, black bars, a further far plane); the original's code is given in comments.
namespace Nx {

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

} // namespace Nx
