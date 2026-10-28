#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/texture_splats.h"
#include "game/mth/vector.h"

// The engine's effects (Nx, platform-independent; each passes on to the PC code): fog settings, the level colour,
// shattering, texture splats aged and killed, screen flashes, and the tables they're kept in - flashes 0x007004A8,
// spawned polygons 0x007004AC, texture splat sets 0x007004B0, recent splat positions 0x007004B4, shatters 0x007004B8
// (Lst::HashTables of 16 buckets, each keyed by its value's address) - plus the vertex stack the splats and shatters
// split triangles on (0x006FC4A0).
namespace Nx {

// The vertex stack (0x006FC4A0): how many bytes are on it, a vertex's size, the vertices.
struct sVertexStack {
    uint32_t used;
    uint32_t vertexSize;
    uint8_t data[0x4000];

    // 0x00490D80: the top vertex popped into `out`.
    void Pop(uint8_t* out);
};
static_assert(sizeof(sVertexStack) == 0x4008, "layout must match the original");

namespace Engine {

// 0x00490DE0 / 0x00490E00 / 0x00490E10 / 0x00490E20: the fog on or off (0x007004C8), its distance (0x0067DDE8),
// exponent (0x0067DDEC; nothing more on PC) and colour (0x0067DE04) kept and the platform told (pc_fog.h).
void __cdecl EnableFog(bool on);
void __cdecl SetFogDistance(float distance);
void __cdecl SetFogExponent(float exponent);
void __cdecl SetFogColor(uint32_t rgba);
// 0x00491290: the level colour kept (0x007004F0).
void __cdecl SetLevelColor(const float* color);

// 0x00491110 (the Shatter command's settings): the velocity the pieces get (0x007004D0) and a vector (0x007004E0,
// the command's 0x210EA9E1), 0x0067DDDC = `valueC483` squared, the triangle split limit (0x0067DDF0) `area` squared
// - 82944 for 0 -, the random speed (0x007004CC, 0 for 0), the velocity's scale (0x0067DDF4, 1 for 0), a shatter's
// lifetime (0x0067DDF8, 4 for 0), floor (0x0067DDFC, -10000 for 0) and bounce (0x0067DE00, 0.8 for 0); 0x0067DDD8,
// the texture (0x007004BC), 0x007004C0 and 0x007004C4 as given.
void __cdecl SetShatterParams(const Mth::Vector* velocity, const Mth::Vector* vector210E, float valueC483, float area,
                              float speed, float velocityScale, float lifetime, float floor, float bounce,
                              float value2E1C, uint32_t texture, uint32_t valueAF1A, int value33DF);
// 0x00490DC0 (not a Ghidra function; what a PC sector's SetActive calls): a geom shattered with the shatter texture
// (0x007004BC, from the particles' texture dictionary 0x00700F20).
void __cdecl ShatterGeom(void* geom);

// 0x00490EC0 (the elapsed time in EAX, the set on the stack - AgeTextureSplatsStub): each living triangle's time
// (ms) counted down - collapsed when it runs out -, the set's last one found again; once fewer than 0x0067DDE0 slots
// are free, the triangles sharing the time and lifetime of the one furthest through its life (of those with both)
// fade by 0x0067DDE4 in their six alphas (killed when all are 0).
void __cdecl AgeTextureSplats(sTextureSplats* splats, int elapsed);
void AgeTextureSplatsStub();
// 0x00491F70: every set aged by the frame's time (ms), unless paused (0x006FA7D4).
void __cdecl AgeAllTextureSplats();
// 0x00491F10: every set lit (LightTextureSplats).
void __cdecl LightAllTextureSplats();
// 0x004915A0: the recent splat positions forgotten (freed), then every set killed.
void __cdecl KillAllTextureSplats();
// 0x00491630: the recent splat positions in the box (min, then max at +0x10) forgotten (their time zeroed), then
// the triangles in it killed.
void __cdecl KillTextureSplatsInBox(const float* box);
// 0x004916F0: the sets of a texture freed (not their materials) and taken out of the table.
void __cdecl KillTextureSplatsOfTexture(uint32_t texture);

// 0x004928A0: a texture splat along a line (from `from` to `to`, `size` wide): the texture by name from the
// particles' dictionary unless given; the collision objects of the grids along the line whose boxes meet its box
// (grown by `size`) gathered with their sectors (0x00700710 / 0x00700510, unbounded) and the splat made on them
// (CreateTextureSplat), aged a frame and lit at once unless it follows something. With `follow` (a bone, an object):
// a recent splat position of the same id near `from` (within 66 - squared 4356 - and half a second) is the projection's
// up and is moved on (no splat when `from` is still at its x and z); with none, `from` is only remembered (true). The
// fade threshold (0x0067DDE0) read from the script integer 0x26FCB7BE each time. Whether a splat was made.
bool __cdecl CreateTextureSplatAt(const Mth::Vector* from, const Mth::Vector* to, float size, float depth, float angle,
                                  float coneAngle, float lifetime, const char* textureName, uint32_t unknown38,
                                  uint32_t follow, uint8_t unknown40, uint8_t* texture);

// 0x00492000: each shatter's lifetime counted down: run out, it's destroyed; else a step (UpdateShatter). Not while
// paused.
void __cdecl UpdateShatters();
// 0x004920E0: every shatter drawn.
void __cdecl DrawShatters();

// 0x004923E0: a flash added (0x24 bytes): from `start` to `end` over `duration` seconds at depth `z`. The texture
// name isn't used.
void __cdecl AddFlash(int viewport, uint32_t start, uint32_t end, float duration, float z, uint32_t flags,
                      const char* texture);
// 0x00491790: each flash's colour moved from its start to its end; run out, it's taken out and freed. Paused, only
// the flashes with flag 8 go on.
void __cdecl UpdateFlashes();
// 0x00491910: the flashes of a viewport whose bit 1 matches `pass` drawn.
void __cdecl DrawFlashes(int viewport, uint32_t pass);

// 0x00492150: the tables made (those not made yet) and the vertex stack emptied (its vertices 0x03030303).
void __cdecl InitEffects();
// 0x00492340: every flash freed, then the spawned polygons (0x00491E10), the splats and the shatters killed.
void __cdecl KillAllEffects();

} // namespace Engine

} // namespace Nx
