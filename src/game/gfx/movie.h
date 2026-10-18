#pragma once

#include <cstdint>

namespace Nx {

// 0x0044FA80: draw a movie frame's texture at (x, y), (width, height) in screen units, faded by alpha (0-1),
// with PartyMod's 4:3 fit inside the back buffer (contains code from PartyMod
// (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).
void DrawMovieFrame(void* texture, float x, float y, float width, float height, float alpha);

// PartyMod's two calls in DrawMovieFrame. SetMovieTexture (cdecl): the renderer's texture set (0x004DA730),
// noting the texture's size first. DrawMovieQuad (stdcall, as the device's DrawPrimitiveUP it stands in for):
// the quad's corners moved to the largest 4:3 area centred in the back buffer, then drawn.
void SetMovieTexture(int stage, void* texture, int unknown);
void __stdcall DrawMovieQuad(void* device, int type, uint32_t count, void* vertices, uint32_t stride);

} // namespace Nx

namespace Script {
class CStruct;
}

namespace Nx {

// 0x0059F0C0: the PlayMovie script command. With thug2-decomp.ini [features] skip_intro_movies=1 the
// start-up logo and intro movies (ATVI, Beenox_Shift, Dolby_Digital, Intel_intro, NSlogo, intro, introf) are
// skipped; other movies play as usual.
bool __cdecl PlayMovie(Script::CStruct* params, void* script);
// The skip_intro_movies flag (read from the ini once; self-tests set it for their run).
bool& SkipIntroMovies();

// A Bink movie's header (binkw32's BINK: the fields the game reads).
struct Bink {
    uint32_t width;
    uint32_t height;
    uint32_t frames;
    uint32_t frameNum; // the frame decoded last, from 1
    uint32_t lastFrameNum;
    uint32_t frameRate;    // frames per frameRateDiv seconds
    uint32_t frameRateDiv;
};

// 0x0044FDC0 (also reached by a jump at 0x0044F830): play data\movies\bik\<name>.bik (a path before the last
// backslash in `name` is dropped) to its end, until the quit request, or until start or X is pressed on a pad
// (pressed after being seen up). The music is paused and the streams stopped first. Originally from PartyMod Fixes: the
// pads are read every frame, not every fifth.
void __cdecl PlayMovieFile(const char* name);

// 0x0044F8C0: the render states for movies (the old ones saved for EndMovie) and a width x height texture for
// the frames (null if it can't be made). Also notes the movie's and the back buffer's sizes as floats.
void* __cdecl BeginMovie(uint32_t width, uint32_t height);
// 0x0044F9F0: the render states back; the texture released.
void __cdecl EndMovie(void* texture);
// 0x0044FCC0: one frame: clear, draw the texture over the screen, present; then Bink on to the next frame.
void __cdecl DrawMovie(Bink* bink, void* texture);

// 0x0044F840 (the control data in EAX, the button mask in ECX, the pad's seen-up bits in ESI; returns AL):
// whether a button of `mask` is pressed now after having been seen up (control data bits are set while up).
bool ButtonPressedAgain(const uint8_t* controlData, uint16_t* seenUp, uint32_t mask);
void ButtonPressedAgainStub();

} // namespace Nx
