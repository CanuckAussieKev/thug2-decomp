#pragma once

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

// The per-frame 3D render (Nx engine, PC/D3D9): the camera's matrices, the world passes per viewport and the
// motion blur pass. PartyMod changes all three (widescreen viewports, black bars, a further far plane); the
// source here is what the game runs with PartyMod, with the original's code given in comments.
namespace Nx {

// 0x004DB200: the camera's view and projection. `orientation`'s rows 1 and 2 are the camera's up and backward
// directions; `angle` is the horizontal field of view in degrees; `zoom` scales the projection (1 = none);
// `infinite` pushes the far plane to infinity.
void SetCamera(const Mth::Matrix* orientation, const Mth::Vector* position, float angle, float aspect, float zoom,
               bool infinite);

// 0x004DF390: the motion blur pass - blends the previous frame (one of two textures) over the screen.
void DrawScreenBlur();

} // namespace Nx
