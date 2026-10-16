#pragma once

namespace Nx {

// 0x0044FA80: draw a movie frame's texture at (x, y), (width, height) in screen units, faded by alpha (0-1),
// with PartyMod's 4:3 fit inside the back buffer.
void DrawMovieFrame(void* texture, float x, float y, float width, float height, float alpha);

} // namespace Nx

namespace Script {
class CStruct;
}

namespace Nx {

// 0x0059F0C0: the PlayMovie script command. With thug2-decomp.ini [features] skip_intro_movies=1 the
// start-up logo and intro movies (ATVI, Beenox_Shift, Dolby_Digital, Intel_intro, NSlogo, intro, introf) are
// skipped; other movies play as usual.
bool __cdecl PlayMovie(Script::CStruct* params, void* script);

} // namespace Nx
