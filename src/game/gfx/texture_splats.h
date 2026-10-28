#pragma once

#include <cstddef>
#include <cstdint>

// Texture splats on PC (Nx engine): textures projected onto the world (skid marks, splashes) - per texture a set of
// up to 256 triangles kept in a table (0x007004B0, Lst::HashTable keyed by the set's address), drawn with a
// perspective correction of their texture coordinates, lit, killed by box or all at once - and the geometry helpers
// the projection uses: 2D segment and triangle tests against the screen square, and splitting triangles that are too
// big (a stack of vertices at 0x006FC4A0).
namespace Nx {

// A splat set (laid out as the original: arrays of 256).
struct sTextureSplats {
    uint32_t alive[0x100];      // +0x0000: 0 once a triangle is killed
    int correctionSteps[0x100]; // +0x0400 (the depth correction's, see DrawTextureSplats)
    float correctionA[0x100];   // +0x0800
    float correctionB[0x100];   // +0x0C00
    uint32_t baseColors[0x100][3]; // +0x1000: each triangle's vertex colours before lighting (RGBA)
    int last;                   // +0x1C00: the last vertex / triangle in use (-1: none)
    uint32_t texture;           // +0x1C04: the texture's checksum (set by the engine's splat maker)
    uint8_t unknown1C08[4];
    uint8_t* material;          // +0x1C0C
    float uvs[0x300][2];        // +0x1C10: each vertex's texture coordinates before the correction
    uint8_t vertices[0x100][3][0x18]; // +0x3410: position, ARGB colour, u, v
};
static_assert(offsetof(sTextureSplats, last) == 0x1C00 && offsetof(sTextureSplats, material) == 0x1C0C &&
                  offsetof(sTextureSplats, uvs) == 0x1C10 && offsetof(sTextureSplats, vertices) == 0x3410,
              "layout must match the original");

// 0x004B7F30: triangle `index` of a set collapsed onto its first vertex (killed).
void __cdecl CollapseSplatTriangle(sTextureSplats* splats, int index);
// 0x004B87B0: each triangle's three vertex colours lit (0x0048F620: from its base colours, at its first vertex's
// position for all three; RGBA to ARGB).
void __cdecl LightTextureSplats(sTextureSplats* splats);
// 0x004B9200: every set freed (its material too) and taken out of the table.
void __cdecl KillAllTextureSplats();
// 0x004B92A0: the triangles whose first vertex is in the box (min, then max at +0x10) killed.
void __cdecl KillTextureSplatsInBox(const float* box);
// 0x004B9380: every set drawn - each vertex's texture coordinates corrected first about 0.5 by 1 / (r + t (1 - r)),
// r = A / B and t = (steps - alive) / steps (1 when A - B is 0) - with linear
// minification (put back after).
void __cdecl DrawTextureSplats();

// 0x004B7FA0: whether segments (a, b) and (c, d) meet (the parameters along each within 0-1 of the denominator; NaN
// passes, as the original's flag tests).
bool __cdecl SegmentsIntersect(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy);
// 0x004B88D0 (and its jump 0x004B8E30): whether a 2D triangle overlaps the square -1..1 - not wholly to one side,
// an edge crossing the square's, a vertex inside it, or a corner inside the triangle.
bool __cdecl TriangleOverlapsScreen(float x0, float y0, float x1, float y1, float x2, float y2);

// The vertex format the splitter works on: the colour's and the texture coordinates' offsets (0: none).
struct sSplitFormat {
    uint8_t unknown0[0x1C];
    uint8_t colorOffset; // +0x1C
    uint8_t unknown1D;
    uint8_t uvOffset;    // +0x1E
};
// 0x004B8220 (with its body Ghidra split off, 0x004B8232): the next triangle off the vertex stack - split into four
// (pushed back) when the cross product of its edges, squared, is above the limit (0x0067DDF0: 82944 as loaded; the
// splat maker sets it for each triangle), else copied to *out (moved past it). False when the stack is empty.
bool __cdecl SplitNextTriangle(uint8_t** out, const sSplitFormat* format);

// What a splat is made from (CreateTextureSplat's): a ray from `start` to `end` (the texture projected along it,
// `size` wide, a square or the distance from `up` high), how far its faces may turn from it (`coneAngle`), how long
// it lasts (`lifetime` in seconds; below 0: for ever), the texture (+0x10 of `texture`, through one more pointer with
// flag 1 of its +0x08).
struct sSplatParams {
    float start[4];
    float end[4];
    float size;        // +0x20
    float depth;       // +0x24 (each triangle's correction A)
    float angle;       // +0x28: the texture's turn about the ray (0: a random one) when the ray is steep
    float coneAngle;   // +0x2C
    float lifetime;    // +0x30
    uint8_t* texture;  // +0x34
    uint32_t unknown38;
    const float* up;   // +0x3C: a point the projection's up comes from, or null
    uint8_t unknown40; // +0x40
    uint8_t pad41[3];

    // 0x00490E40: construct: the start and end (0, 0, 0, 1), everything else zero but +0x38 (0x54628ED7). Returns
    // this.
    sSplatParams* Construct();
};
static_assert(offsetof(sSplatParams, texture) == 0x34 && offsetof(sSplatParams, up) == 0x3C &&
                  sizeof(sSplatParams) == 0x44,
              "layout must match the original");

// 0x004B9A60: the splat set for a texture: the table's, or a new one (its material: one pass of the texture, a random
// checksum - the game's rand() twice -, blend mode 5) put in it.
sTextureSplats* __cdecl GetTextureSplats(uint32_t texture);
// 0x004B9C30: a splat made on the sectors (a null-terminated array) along the ray: their meshes' strip triangles
// that face it (within the cone) and fall inside the projection get the texture - split finer where they're big
// (the pieces inside kept) - into the texture's set's oldest triangles (0x00490CD0). The set, or null if no
// triangle was hit. `unused` isn't.
sTextureSplats* __cdecl CreateTextureSplat(void** sectors, int unused, sSplatParams* params);

} // namespace Nx
