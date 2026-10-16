#pragma once

namespace Nx {

// 0x0044FA80: draw a movie frame's texture at (x, y), (width, height) in screen units, faded by alpha (0-1),
// with PartyMod's 4:3 fit inside the back buffer.
void DrawMovieFrame(void* texture, float x, float y, float width, float height, float alpha);

} // namespace Nx
