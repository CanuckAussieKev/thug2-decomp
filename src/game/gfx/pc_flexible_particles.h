#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/particle_system.h"
#include "game/mth/vector.h"

// The PC flexible particle systems (Nx engine; the particle manager's 0x0049AC60 makes them by type: the flexible
// particle component's systems): each type the engine's class of its own (its update, its particles) with a PC one
// over it that draws them - camera-facing quads, ribbons, stars, glows - through the dynamic vertex buffer with a
// material of its own (sSimpleMaterial), and emits the particles its rate asks for.
namespace Nx {

struct sSimpleMaterial;

// (sFlexibleParticle: particle_system.h.)

// What the PC classes share (the engine's particle system's fields they read; Nx::CParticleSystem in
// flexible_particle.h is the component's view of the same object). Two vtables: +0x00 and +0x0C.
struct CPcFlexibleParticles {
    void** vtable;
    uint8_t engine04[8];
    void** secondaryVtable;  // +0x0C
    uint8_t engine10[0x10];
    Mth::Vector position;    // +0x20: the emitter's (particles are relative to it unless emitterIndependent)
    uint8_t engine30[0x10];
    Mth::Vector alignTo;     // +0x40: the direction the aligned types face along
    uint8_t engine50[0x10];
    float colourScale[3];    // +0x60: red, green, blue
    uint8_t engine6C[4];
    float emitRate;          // +0x70: particles a second
    uint8_t engine74[8];
    int historyStride;       // +0x7C: between a particle's positions at successive history steps (in positions)
    uint8_t engine80[0xC];
    int count;               // +0x8C: live particles
    uint8_t engine90[0x5C];
    float brightness;        // +0xEC: scales colourScale
    uint8_t engineF0[8];
    sFlexibleParticle* particles; // +0xF8
    uint8_t engineFC[0xC];
    float colourMid;         // +0x108: when the mid colour is, a fraction of a particle's life (below 0: none)
    uint32_t* startColour;   // +0x10C (RGBA)
    uint32_t* midColour;     // +0x110
    uint32_t* endColour;     // +0x114
    int historyLength;       // +0x118: history steps (the glow ribbon trail's)
    float* positions;        // +0x11C: three floats a particle
    uint32_t flags;          // +0x120: 0x4000 UseRandomUVQuadrants
    uint8_t engine124[2];
    bool emitterIndependent; // +0x126
    bool aligned;            // +0x127: facing along alignTo, not the camera

    // 0x004BD470: the colour `t` of the way from `from` to `to` (RGBA bytes, each channel wrapping as a byte), its
    // red, green and blue then scaled by colourScale x brightness (clamped to 0-255) - into *out as a D3D colour
    // (BGRA bytes). Returns out.
    uint32_t* LerpColour(uint32_t* out, const uint8_t* from, const uint8_t* to, float t);
    // Which colours a particle `life` of the way through its life is between, and how far.
    struct sColourSpan {
        const uint32_t* from;
        const uint32_t* to;
        float t;
    };
    sColourSpan ColourSpan(float life) const;
    // LerpColour of two colours (copies).
    uint32_t Colour(uint32_t from, uint32_t to, float t);
    // The axes of a quad facing along alignTo: r = alignTo x (0, 1, 0) (x when alignTo is y itself), u = r x
    // alignTo, normalised.
    void AlignedAxes(Mth::Vector& r, Mth::Vector& u) const;
    // A destructor body: the vtables, the material destroyed and freed, the engine's destructor body.
    void DestroyParts(uint32_t primary, uint32_t secondary, sSimpleMaterial* material, uint32_t engineDestructor);
    // The rate's particles for this frame (1/60 s) emitted through the engine's 0x00496FE0, the fraction kept.
    void EmitForFrame(float& accumulator);
};
static_assert(offsetof(CPcFlexibleParticles, position) == 0x20 && offsetof(CPcFlexibleParticles, colourScale) == 0x60 &&
                  offsetof(CPcFlexibleParticles, emitRate) == 0x70 &&
                  offsetof(CPcFlexibleParticles, historyStride) == 0x7C && offsetof(CPcFlexibleParticles, count) == 0x8C &&
                  offsetof(CPcFlexibleParticles, brightness) == 0xEC &&
                  offsetof(CPcFlexibleParticles, particles) == 0xF8 &&
                  offsetof(CPcFlexibleParticles, colourMid) == 0x108 &&
                  offsetof(CPcFlexibleParticles, positions) == 0x11C &&
                  offsetof(CPcFlexibleParticles, historyLength) == 0x118 &&
                  offsetof(CPcFlexibleParticles, alignTo) == 0x40 && offsetof(CPcFlexibleParticles, flags) == 0x120 &&
                  offsetof(CPcFlexibleParticles, aligned) == 0x127 &&
                  offsetof(CPcFlexibleParticles, emitterIndependent) == 0x126 && sizeof(CPcFlexibleParticles) == 0x128,
              "layout must match the original");

// 0x004BCEF0: a new system of `type` (glow, ribbon, smoothstar, star, line, smooth, glowribbontrail, smoothribbon,
// ribbontrail, shaded, 0x3D56A7A6 - a flat ribbon -, newflat - and newflat for any other type -; 0x286A8D26 the engine's
// own class 0x0049A0C0, without a PC one, from `model`), each from the parameters it uses. Null if no memory.
void* __cdecl CreateFlexibleParticles(uint32_t name, uint32_t type, int maxParticles, int maxStreams, uint32_t texture,
                                      uint32_t blendMode, int fixedAlpha, int segments, float unknown,
                                      int history, const char* model);

// The "newflat" type (0x148 bytes, vtables 0x0064A514 and 0x0064A510 at +0x0C; the engine's class 0x00499010 -
// destructor body 0x00498FB0): each particle a quad, its size and colour through its life.
struct CPcNewFlatParticles : CPcFlexibleParticles {
    uint8_t engine128[0x18];
    sSimpleMaterial* material; // +0x140
    float emitted;             // +0x144: particles owed, a fraction

    // 0x004BD3A0: construct. Returns this.
    CPcNewFlatParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                   int fixedAlpha, int segments, float unknown);
    // 0x004BD610: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004BDC40 (virtual [0]) / 0x004BD390 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcNewFlatParticles* Destroy(unsigned int freeFlags);
    // 0x004BD680 (virtual [1]): the particles drawn, then emitted for the frame. While a draw fills the vertex
    // buffer to within a quad of its room, the original stops - those particles and the rest aren't drawn.
    void Render();
};
static_assert(offsetof(CPcNewFlatParticles, material) == 0x140 && sizeof(CPcNewFlatParticles) == 0x148,
              "layout must match the original");

// The type 0x3D56A7A6 (its name unknown - a flat ribbon trail; 0x144 bytes, vtables 0x0064A528 and 0x0064A524 at
// +0x0C; the engine's class 0x004992C0 - destructor body 0x00499200): each particle's history of `segments` positions
// drawn as a ribbon (its width across the camera and the path), its colours per segment, then a head quad of five
// times the width at the particle.
struct CPcFlatRibbonParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;              // +0x134: history steps
    uint8_t engine138[4];
    float width;               // +0x13C
    sSimpleMaterial* material; // +0x140

    // 0x004BDC80: construct. Returns this.
    CPcFlatRibbonParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                      int fixedAlpha, int segments, float width, int history);
    // 0x004BDD60: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004BECD0 (virtual [0]) / 0x004BDC70 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcFlatRibbonParticles* Destroy(unsigned int freeFlags);
    // 0x004BDDD0 (virtual [1]): the ribbons (untextured), then the heads (the default pixel shader); a draw that
    // fills the buffer to within four vertices of its room stops that pass undrawn, as the original. No emission.
    void Render();
};
static_assert(offsetof(CPcFlatRibbonParticles, segments) == 0x134 && offsetof(CPcFlatRibbonParticles, width) == 0x13C &&
                  offsetof(CPcFlatRibbonParticles, material) == 0x140 && sizeof(CPcFlatRibbonParticles) == 0x144,
              "layout must match the original");

// The "glow" type (0x150 bytes, vtables 0x0064A540 and 0x0064A53C at +0x0C; the engine's class 0x004995B0 -
// destructor body 0x00499550): each particle a fan of `segments` around it - a disc (the centre's colour to the inner
// ring's) and a ring of `glowWidth` times its size beyond (to the outer colour), in the camera's plane - written to a
// buffer on the stack and drawn with DrawPrimitiveUP (0x7FF words a draw). No emission.
struct CPcGlowParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;              // +0x134
    float glowWidth;           // +0x138: the outer ring's radius, a multiple of the inner's
    uint8_t engine13C[0xC];
    sSimpleMaterial* material; // +0x148
    uint8_t unknown14C[4];

    // 0x004BED10: construct. Returns this.
    CPcGlowParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                int segments, float unknown);
    // 0x004BEDE0: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004BF6E0 (virtual [0]) / 0x004BED00 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcGlowParticles* Destroy(unsigned int freeFlags);
    // 0x004BEE50 (virtual [1]): drawn. The original's triangle count is a quarter of the triangles it writes (words /
    // 16 / 3) - kept.
    void Render();
};
static_assert(offsetof(CPcGlowParticles, segments) == 0x134 && offsetof(CPcGlowParticles, glowWidth) == 0x138 &&
                  offsetof(CPcGlowParticles, material) == 0x148 && sizeof(CPcGlowParticles) == 0x150,
              "layout must match the original");

// The "glowribbontrail" type (0x150 bytes, vtables 0x0064A558 and 0x0064A554; the engine's class 0x00499880 -
// destructor body 0x00499800): each particle's history as a ribbon (two triangles a step; its colours from index 3 of
// the colour arrays on) and a glow fan about its head (colours 0-2, the fraction gone through the mid selection a
// second time, as the original) - all the particles locked at once and drawn in one DrawPrimitive with alpha
// blending off; positions as stored (the emitter's not added). Colours in 4096ths, not scaled.
struct CPcGlowRibbonTrailParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;              // +0x134: the glow's
    float width;               // +0x138
    uint8_t engine13C[0xC];
    sSimpleMaterial* material; // +0x148
    float glowStep;            // +0x14C: degrees between the glow's points

    // 0x004BF720: construct. Returns this.
    CPcGlowRibbonTrailParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                           int fixedAlpha, int segments, float unknown, int history);
    // 0x004C0940: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C09B0 (virtual [0]) / 0x004BF710 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcGlowRibbonTrailParticles* Destroy(unsigned int freeFlags);
    // 0x004BF7F0 (virtual [1]): drawn. No emission.
    void Render();
    // (Inline in Render.)
    static uint32_t FineColour(uint32_t from, uint32_t to, double t);
};
static_assert(offsetof(CPcGlowRibbonTrailParticles, segments) == 0x134 &&
                  offsetof(CPcGlowRibbonTrailParticles, material) == 0x148 &&
                  offsetof(CPcGlowRibbonTrailParticles, glowStep) == 0x14C &&
                  sizeof(CPcGlowRibbonTrailParticles) == 0x150,
              "layout must match the original");

// The "line" type (0x148 bytes, vtables 0x0064A570 and 0x0064A56C at +0x0C; the engine's class 0x00499B80 -
// destructor body 0x00499B20): each particle a line from its position to its next history step, a colour each end.
struct CPcLineParticles : CPcFlexibleParticles {
    uint8_t engine128[0x18];
    sSimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C09F0: construct. Returns this.
    CPcLineParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                int segments, float unknown);
    // 0x004C0AC0: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C0E70 (virtual [0]) / 0x004C09E0 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcLineParticles* Destroy(unsigned int freeFlags);
    // 0x004C0B30 (virtual [1]): drawn as a line list (no index buffer); a draw that fills the buffer to within two
    // vertices stops undrawn, as the original. No emission.
    void Render();
};
static_assert(offsetof(CPcLineParticles, material) == 0x140 && sizeof(CPcLineParticles) == 0x148,
              "layout must match the original");

// The "ribbon" type (0x148 bytes, vtables 0x0064A584 and 0x0064A580; the engine's class 0x0049B0D0 - destructor body
// 0x0049B070): each particle a quad from its position to its oldest history step, as wide as its size across the
// camera and the path - textured when the material has a texture.
struct CPcRibbonParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;              // +0x134: history steps
    uint8_t engine138[8];
    sSimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C0EB0: construct. Returns this.
    CPcRibbonParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                  int fixedAlpha, int segments, float unknown, int history);
    // 0x004C0F80: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C16E0 (virtual [0]) / 0x004C0EA0 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcRibbonParticles* Destroy(unsigned int freeFlags);
    // 0x004C0FF0 (virtual [1]): drawn. No emission.
    void Render();
};
static_assert(offsetof(CPcRibbonParticles, segments) == 0x134 && offsetof(CPcRibbonParticles, material) == 0x140 &&
                  sizeof(CPcRibbonParticles) == 0x148,
              "layout must match the original");

// The "ribbontrail" type (0x148 bytes, vtables 0x0064A598 and 0x0064A594; the engine's class 0x0049B380 - destructor
// body 0x0049B320): each particle's history drawn as a ribbon of `segments` points, its head end's colour scaled by
// the brightness alone (the others by LerpColour).
struct CPcRibbonTrailParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;              // +0x134
    uint8_t engine138[8];
    sSimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C1720: construct. Returns this.
    CPcRibbonTrailParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                       int fixedAlpha, int segments, float unknown, int history);
    // 0x004C17F0: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C2160 (virtual [0]) / 0x004C1710 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcRibbonTrailParticles* Destroy(unsigned int freeFlags);
    // 0x004C1860 (virtual [1]): drawn. No emission.
    void Render();
    // (Inline in Render.)
    uint32_t HeadColour(uint32_t from, uint32_t to, float t);
};
static_assert(offsetof(CPcRibbonTrailParticles, segments) == 0x134 &&
                  offsetof(CPcRibbonTrailParticles, material) == 0x140 && sizeof(CPcRibbonTrailParticles) == 0x148,
              "layout must match the original");

// The "shaded" type (0x138 bytes, vtables 0x0064A5AC and 0x0064A5A8; the engine's class 0x0049B660 - destructor body
// 0x0049B600): each particle a quad with a colour per corner, facing the camera or along alignTo; with
// UseRandomUVQuadrants (flags 0x4000) a quarter of the texture by the particle's index.
struct CPcShadedParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    sSimpleMaterial* material; // +0x134

    // 0x004C21A0: construct. Returns this.
    CPcShadedParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                  int fixedAlpha);
    // 0x004C2260: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C2AF0 (virtual [0]) / 0x004C2190 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcShadedParticles* Destroy(unsigned int freeFlags);
    // 0x004C22D0 (virtual [1]): drawn (the material's own pixel shader). No emission.
    void Render();
};
static_assert(offsetof(CPcShadedParticles, material) == 0x134 && sizeof(CPcShadedParticles) == 0x138,
              "layout must match the original");

// The "smooth" type (0x148 bytes, vtables 0x0064A5C0 and 0x0064A5BC; the engine's class 0x0049B910 - destructor body
// 0x0049B8A0): each particle four triangles from its centre (its own colour) to the quad's corners (the edge's).
struct CPcSmoothParticles : CPcFlexibleParticles {
    uint8_t engine128[0x18];
    sSimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C2B30: construct. Returns this.
    CPcSmoothParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                  int fixedAlpha, int segments, float unknown);
    // 0x004C2C00: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C3500 (virtual [0]) / 0x004C2B20 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcSmoothParticles* Destroy(unsigned int freeFlags);
    // 0x004C2C70 (virtual [1]): drawn (DrawPrimitive, no index buffer; a draw that fills the buffer to within twelve
    // vertices stops undrawn, as the original). No emission.
    void Render();
};
static_assert(offsetof(CPcSmoothParticles, material) == 0x140 && sizeof(CPcSmoothParticles) == 0x148,
              "layout must match the original");

// The "smoothribbon" type (0x140 bytes, vtables 0x0064A5D4 and 0x0064A5D0; the engine's class 0x0049BBD0 -
// destructor body 0x0049BB50): each particle two quads from the line between its position and its oldest history
// step out to either side, four colours.
struct CPcSmoothRibbonParticles : CPcFlexibleParticles {
    uint8_t engine128[0x10];
    int segments;              // +0x138: history steps
    sSimpleMaterial* material; // +0x13C

    // 0x004C3550: construct. Returns this.
    CPcSmoothRibbonParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                        int fixedAlpha, int segments, float unknown, int history);
    // 0x004C3620: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C3D40 (virtual [0]) / 0x004C3540 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcSmoothRibbonParticles* Destroy(unsigned int freeFlags);
    // 0x004C3690 (virtual [1]): drawn. No emission.
    void Render();
    // 0x004C3530 (virtual [2]): the history steps.
    int GetSegments();
};
static_assert(offsetof(CPcSmoothRibbonParticles, segments) == 0x138 &&
                  offsetof(CPcSmoothRibbonParticles, material) == 0x13C && sizeof(CPcSmoothRibbonParticles) == 0x140,
              "layout must match the original");

// The "smoothstar" type (0x150 bytes, vtables 0x0064A5EC and 0x0064A5E8; the engine's class 0x0049BE80 -
// destructor body 0x0049BE20): each particle a star of `segments` points (starWidth out) about its centre - two
// quads a point: centre, tip, the next outer point and the inner one; three colours.
struct CPcSmoothStarParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;              // +0x134
    float starWidth;           // +0x138
    uint8_t engine13C[0xC];
    sSimpleMaterial* material; // +0x148
    uint8_t unknown14C[4];

    // 0x004C3D80: construct. Returns this.
    CPcSmoothStarParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                      int fixedAlpha, int segments, float unknown);
    // 0x004C3E50: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C4880 (virtual [0]) / 0x004C3D70 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcSmoothStarParticles* Destroy(unsigned int freeFlags);
    // 0x004C3EC0 (virtual [1]): drawn. No emission.
    void Render();
};
static_assert(offsetof(CPcSmoothStarParticles, segments) == 0x134 &&
                  offsetof(CPcSmoothStarParticles, starWidth) == 0x138 &&
                  offsetof(CPcSmoothStarParticles, material) == 0x148 && sizeof(CPcSmoothStarParticles) == 0x150,
              "layout must match the original");

// The "star" type (0x150 bytes, vtables 0x0064A604 and 0x0064A600; the engine's class 0x0049C140 - destructor body
// 0x0049C0D0): each particle a star of `segments` points about its centre, a quad a point.
struct CPcStarParticles : CPcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;              // +0x134
    float starWidth;           // +0x138
    uint8_t engine13C[0xC];
    sSimpleMaterial* material; // +0x148
    uint8_t unknown14C[4];

    // 0x004C48C0: construct. Returns this.
    CPcStarParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                int segments, float unknown);
    // 0x004C4990: the destructor's body (the material freed).
    void DestroyBody();
    // 0x004C5200 (virtual [0]) / 0x004C48B0 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    CPcStarParticles* Destroy(unsigned int freeFlags);
    // 0x004C4A00 (virtual [1]): drawn. No emission.
    void Render();
};
static_assert(offsetof(CPcStarParticles, segments) == 0x134 && offsetof(CPcStarParticles, starWidth) == 0x138 &&
                  offsetof(CPcStarParticles, material) == 0x148 && sizeof(CPcStarParticles) == 0x150,
              "layout must match the original");

} // namespace Nx
