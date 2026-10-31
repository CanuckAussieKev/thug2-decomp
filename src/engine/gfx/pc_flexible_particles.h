#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/particle_system.h"
#include "engine/maths/vec4.h"

struct IDirect3DIndexBuffer9;

// The PC flexible particle systems (Nx engine; the particle manager's 0x0049AC60 makes them by type: the flexible
// particle component's systems): each type the engine's class of its own (its update, its particles) with a PC one
// over it that draws them - camera-facing quads, ribbons, stars, glows - through the dynamic vertex buffer with a
// material of its own (SSimpleMaterial), and emits the particles its rate asks for.
namespace render {

struct SimpleMaterial;

// (SFlexibleParticle: particle_system.h.)

// What the PC classes share (the engine's particle system's fields they read; render::EmitterInstance in
// flexible_particle.h is the component's view of the same object). Two vtables: +0x00 and +0x0C.
struct PcFlexibleParticles {
    void** vtable;
    uint8_t engine04[8];
    void** secondary_vtable_; // +0x0C
    uint8_t engine10[0x10];
    maths::Vec4 position; // +0x20: the emitter's (particles are relative to it unless emitterIndependent)
    uint8_t engine30[0x10];
    maths::Vec4 align_to_; // +0x40: the direction the aligned types face along
    uint8_t engine50[0x10];
    float colour_scale_[3]; // +0x60: red, green, blue
    uint8_t engine_6c_[4];
    float emit_rate_; // +0x70: particles a second
    uint8_t engine74[8];
    int history_stride_; // +0x7C: between a particle's positions at successive history steps (in positions)
    uint8_t engine80[0xC];
    int count; // +0x8C: live particles
    uint8_t engine90[0x5C];
    float brightness; // +0xEC: scales colour_scale_
    uint8_t engine_f0_[8];
    FlexibleParticle* particles; // +0xF8
    uint8_t engine_fc_[0xC];
    float colour_mid_;       // +0x108: when the mid colour is, a fraction of a particle's life (below 0: none)
    uint32_t* start_colour_; // +0x10C (RGBA)
    uint32_t* mid_colour_;   // +0x110
    uint32_t* end_colour_;   // +0x114
    int history_length_;     // +0x118: history steps (the glow ribbon trail's)
    float* positions;        // +0x11C: three floats a particle
    uint32_t flags;          // +0x120: 0x4000 UseRandomUVQuadrants
    uint8_t engine124[2];
    bool emitter_independent_; // +0x126
    bool aligned;              // +0x127: facing along alignTo, not the camera

    // 0x004BD470: the colour `t` of the way from `from` to `to` (RGBA bytes, each channel wrapping as a byte), its
    // red, green and blue then scaled by colour_scale_ x brightness (clamped to 0-255) - into *out as a d_3d colour
    // (BGRA bytes). Returns out.
    uint32_t* lerp_colour(uint32_t* out, const uint8_t* from, const uint8_t* to, float t);
    // Which colours a particle `life` of the way through its life is between, and how far.
    struct ColourSpan {
        const uint32_t* from;
        const uint32_t* to;
        float t;
    };
    ColourSpan colour_span(float life) const;
    // lerp_colour of two colours (copies).
    uint32_t get_colour(uint32_t from, uint32_t to, float t);
    // The axes of a quad facing along alignTo: r = alignTo x (0, 1, 0) (x when alignTo is y itself), u = r x
    // alignTo, normalised.
    void aligned_axes(maths::Vec4& r, maths::Vec4& u) const;
    // A destructor body: the vtables, the material destroyed and freed, the engine's destructor body.
    void destroy_parts(void** primary, void** secondary, SimpleMaterial* material,
                       void(__cdecl* engine_destructor)(PcFlexibleParticles*));
    // The rate's particles for this frame (1/60 s) emitted through the engine's 0x00496FE0, the fraction kept.
    void emit_for_frame(float& accumulator);
};
static_assert(offsetof(PcFlexibleParticles, position) == 0x20 && offsetof(PcFlexibleParticles, colour_scale_) == 0x60 &&
                  offsetof(PcFlexibleParticles, emit_rate_) == 0x70 &&
                  offsetof(PcFlexibleParticles, history_stride_) == 0x7C &&
                  offsetof(PcFlexibleParticles, count) == 0x8C && offsetof(PcFlexibleParticles, brightness) == 0xEC &&
                  offsetof(PcFlexibleParticles, particles) == 0xF8 &&
                  offsetof(PcFlexibleParticles, colour_mid_) == 0x108 &&
                  offsetof(PcFlexibleParticles, positions) == 0x11C &&
                  offsetof(PcFlexibleParticles, history_length_) == 0x118 &&
                  offsetof(PcFlexibleParticles, align_to_) == 0x40 && offsetof(PcFlexibleParticles, flags) == 0x120 &&
                  offsetof(PcFlexibleParticles, aligned) == 0x127 &&
                  offsetof(PcFlexibleParticles, emitter_independent_) == 0x126 && sizeof(PcFlexibleParticles) == 0x128,
              "layout must match the original");

// 0x004BCEF0: a new system of `type` (glow, ribbon, smoothstar, star, line, smooth, glowribbontrail, smoothribbon,
// ribbontrail, shaded, 0x3D56A7A6 - a flat ribbon -, newflat - and newflat for any other type -; 0x286A8D26 the engine's
// own class 0x0049A0C0, without a PC one, from `model`), each from the parameters it uses. Null if no memory.
void* __cdecl create_flexible_particles(uint32_t name, uint32_t type, int particle_cap, int max_streams,
                                        uint32_t texture, uint32_t blend_mode, int fixed_alpha, int segments,
                                        float unknown, int history, const char* model);

// The "newflat" type (0x148 bytes, vtables 0x0064A514 and 0x0064A510 at +0x0C; the engine's class 0x00499010 -
// destructor body 0x00498FB0): each particle a quad, its size and colour through its life.
struct PcNewFlatParticles : PcFlexibleParticles {
    uint8_t engine128[0x18];
    SimpleMaterial* material; // +0x140
    float emitted;            // +0x144: particles owed, a fraction

    // 0x004BD3A0: construct. Returns this.
    PcNewFlatParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                 int fixed_alpha, int segments, float unknown);
    // 0x004BD610: the destructor's body (the material freed).
    void destroy_body();
    // 0x004BDC40 (virtual [0]) / 0x004BD390 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcNewFlatParticles* teardown(unsigned int free_flags);
    // 0x004BD680 (virtual [1]): the particles drawn, then emitted for the frame. While a draw fills the vertex
    // buffer to within a quad of its room, the original stops - those particles and the rest aren't drawn.
    void render();
};
static_assert(offsetof(PcNewFlatParticles, material) == 0x140 && sizeof(PcNewFlatParticles) == 0x148,
              "layout must match the original");

// The type 0x3D56A7A6 (its name unknown - a flat ribbon trail; 0x144 bytes, vtables 0x0064A528 and 0x0064A524 at
// +0x0C; the engine's class 0x004992C0 - destructor body 0x00499200): each particle's history of `segments` positions
// drawn as a ribbon (its width across the camera and the path), its colours per segment, then a head quad of five
// times the width at the particle.
struct PcFlatRibbonParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments; // +0x134: history steps
    uint8_t engine138[4];
    float width;              // +0x13C
    SimpleMaterial* material; // +0x140

    // 0x004BDC80: construct. Returns this.
    PcFlatRibbonParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                    int fixed_alpha, int segments, float width, int history);
    // 0x004BDD60: the destructor's body (the material freed).
    void destroy_body();
    // 0x004BECD0 (virtual [0]) / 0x004BDC70 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcFlatRibbonParticles* teardown(unsigned int free_flags);
    // 0x004BDDD0 (virtual [1]): the ribbons (untextured), then the heads (the default pixel shader); a draw that
    // fills the buffer to within four vertices of its room stops that pass undrawn, as the original. No emission.
    void render();
};
static_assert(offsetof(PcFlatRibbonParticles, segments) == 0x134 && offsetof(PcFlatRibbonParticles, width) == 0x13C &&
                  offsetof(PcFlatRibbonParticles, material) == 0x140 && sizeof(PcFlatRibbonParticles) == 0x144,
              "layout must match the original");

// The "glow" type (0x150 bytes, vtables 0x0064A540 and 0x0064A53C at +0x0C; the engine's class 0x004995B0 -
// destructor body 0x00499550): each particle a fan of `segments` around it - a disc (the centre's colour to the inner
// ring's) and a ring of `glowWidth` times its size beyond (to the outer colour), in the camera's plane - written to a
// buffer on the stack and drawn with DrawPrimitiveUP (0x7FF words a draw). No emission.
struct PcGlowParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;      // +0x134
    float glow_width_; // +0x138: the outer ring's radius, a multiple of the inner's
    uint8_t engine_13c_[0xC];
    SimpleMaterial* material; // +0x148
    uint8_t unknown14c_[4];

    // 0x004BED10: construct. Returns this.
    PcGlowParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                              int segments, float unknown);
    // 0x004BEDE0: the destructor's body (the material freed).
    void destroy_body();
    // 0x004BF6E0 (virtual [0]) / 0x004BED00 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcGlowParticles* teardown(unsigned int free_flags);
    // 0x004BEE50 (virtual [1]): drawn. The original's triangle count is a quarter of the triangles it writes (words /
    // 16 / 3) - kept.
    void render();
};
static_assert(offsetof(PcGlowParticles, segments) == 0x134 && offsetof(PcGlowParticles, glow_width_) == 0x138 &&
                  offsetof(PcGlowParticles, material) == 0x148 && sizeof(PcGlowParticles) == 0x150,
              "layout must match the original");

// The "glowribbontrail" type (0x150 bytes, vtables 0x0064A558 and 0x0064A554; the engine's class 0x00499880 -
// destructor body 0x00499800): each particle's history as a ribbon (two triangles a step; its colours from index 3 of
// the colour arrays on) and a glow fan about its head (colours 0-2, the fraction gone through the mid selection a
// second time, as the original) - all the particles locked at once and drawn in one DrawPrimitive with alpha
// blending off; positions as stored (the emitter's not added). Colours in 4096ths, not scaled.
struct PcGlowRibbonTrailParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments; // +0x134: the glow's
    float width;  // +0x138
    uint8_t engine_13c_[0xC];
    SimpleMaterial* material; // +0x148
    float glow_step_;         // +0x14C: degrees between the glow's points

    // 0x004BF720: construct. Returns this.
    PcGlowRibbonTrailParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                         int fixed_alpha, int segments, float unknown, int history);
    // 0x004C0940: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C09B0 (virtual [0]) / 0x004BF710 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcGlowRibbonTrailParticles* teardown(unsigned int free_flags);
    // 0x004BF7F0 (virtual [1]): drawn. No emission.
    void render();
    // (Inline in Render.)
    static uint32_t fine_colour(uint32_t from, uint32_t to, double t);
};
static_assert(offsetof(PcGlowRibbonTrailParticles, segments) == 0x134 &&
                  offsetof(PcGlowRibbonTrailParticles, material) == 0x148 &&
                  offsetof(PcGlowRibbonTrailParticles, glow_step_) == 0x14C &&
                  sizeof(PcGlowRibbonTrailParticles) == 0x150,
              "layout must match the original");

// The "line" type (0x148 bytes, vtables 0x0064A570 and 0x0064A56C at +0x0C; the engine's class 0x00499B80 -
// destructor body 0x00499B20): each particle a line from its position to its next history step, a colour each end.
struct PcLineParticles : PcFlexibleParticles {
    uint8_t engine128[0x18];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C09F0: construct. Returns this.
    PcLineParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                              int segments, float unknown);
    // 0x004C0AC0: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C0E70 (virtual [0]) / 0x004C09E0 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcLineParticles* teardown(unsigned int free_flags);
    // 0x004C0B30 (virtual [1]): drawn as a line list (no index buffer); a draw that fills the buffer to within two
    // vertices stops undrawn, as the original. No emission.
    void render();
};
static_assert(offsetof(PcLineParticles, material) == 0x140 && sizeof(PcLineParticles) == 0x148,
              "layout must match the original");

// The "ribbon" type (0x148 bytes, vtables 0x0064A584 and 0x0064A580; the engine's class 0x0049B0D0 - destructor body
// 0x0049B070): each particle a quad from its position to its oldest history step, as wide as its size across the
// camera and the path - textured when the material has a texture.
struct PcRibbonParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments; // +0x134: history steps
    uint8_t engine138[8];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C0EB0: construct. Returns this.
    PcRibbonParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                                int segments, float unknown, int history);
    // 0x004C0F80: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C16E0 (virtual [0]) / 0x004C0EA0 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcRibbonParticles* teardown(unsigned int free_flags);
    // 0x004C0FF0 (virtual [1]): drawn. No emission.
    void render();
};
static_assert(offsetof(PcRibbonParticles, segments) == 0x134 && offsetof(PcRibbonParticles, material) == 0x140 &&
                  sizeof(PcRibbonParticles) == 0x148,
              "layout must match the original");

// The "ribbontrail" type (0x148 bytes, vtables 0x0064A598 and 0x0064A594; the engine's class 0x0049B380 - destructor
// body 0x0049B320): each particle's history drawn as a ribbon of `segments` points, its head end's colour scaled by
// the brightness alone (the others by lerp_colour).
struct PcRibbonTrailParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments; // +0x134
    uint8_t engine138[8];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C1720: construct. Returns this.
    PcRibbonTrailParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha, int segments, float unknown, int history);
    // 0x004C17F0: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C2160 (virtual [0]) / 0x004C1710 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcRibbonTrailParticles* teardown(unsigned int free_flags);
    // 0x004C1860 (virtual [1]): drawn. No emission.
    void render();
    // (Inline in Render.)
    uint32_t head_colour(uint32_t from, uint32_t to, float t);
};
static_assert(offsetof(PcRibbonTrailParticles, segments) == 0x134 &&
                  offsetof(PcRibbonTrailParticles, material) == 0x140 && sizeof(PcRibbonTrailParticles) == 0x148,
              "layout must match the original");

// The "shaded" type (0x138 bytes, vtables 0x0064A5AC and 0x0064A5A8; the engine's class 0x0049B660 - destructor body
// 0x0049B600): each particle a quad with a colour per corner, facing the camera or along alignTo; with
// UseRandomUVQuadrants (flags 0x4000) a quarter of the texture by the particle's index.
struct PcShadedParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    SimpleMaterial* material; // +0x134

    // 0x004C21A0: construct. Returns this.
    PcShadedParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                int fixed_alpha);
    // 0x004C2260: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C2AF0 (virtual [0]) / 0x004C2190 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcShadedParticles* teardown(unsigned int free_flags);
    // 0x004C22D0 (virtual [1]): drawn (the material's own pixel shader). No emission.
    void render();
};
static_assert(offsetof(PcShadedParticles, material) == 0x134 && sizeof(PcShadedParticles) == 0x138,
              "layout must match the original");

// The "smooth" type (0x148 bytes, vtables 0x0064A5C0 and 0x0064A5BC; the engine's class 0x0049B910 - destructor body
// 0x0049B8A0): each particle four triangles from its centre (its own colour) to the quad's corners (the edge's).
struct PcSmoothParticles : PcFlexibleParticles {
    uint8_t engine128[0x18];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144[4];

    // 0x004C2B30: construct. Returns this.
    PcSmoothParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                                int segments, float unknown);
    // 0x004C2C00: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C3500 (virtual [0]) / 0x004C2B20 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcSmoothParticles* teardown(unsigned int free_flags);
    // 0x004C2C70 (virtual [1]): drawn (DrawPrimitive, no index buffer; a draw that fills the buffer to within twelve
    // vertices stops undrawn, as the original). No emission.
    void render();
};
static_assert(offsetof(PcSmoothParticles, material) == 0x140 && sizeof(PcSmoothParticles) == 0x148,
              "layout must match the original");

// The "smoothribbon" type (0x140 bytes, vtables 0x0064A5D4 and 0x0064A5D0; the engine's class 0x0049BBD0 -
// destructor body 0x0049BB50): each particle two quads from the line between its position and its oldest history
// step out to either side, four colours.
struct PcSmoothRibbonParticles : PcFlexibleParticles {
    uint8_t engine128[0x10];
    int segments;             // +0x138: history steps
    SimpleMaterial* material; // +0x13C

    // 0x004C3550: construct. Returns this.
    PcSmoothRibbonParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                      int fixed_alpha, int segments, float unknown, int history);
    // 0x004C3620: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C3D40 (virtual [0]) / 0x004C3540 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcSmoothRibbonParticles* teardown(unsigned int free_flags);
    // 0x004C3690 (virtual [1]): drawn. No emission.
    void render();
    // 0x004C3530 (virtual [2]): the history steps.
    int get_segments();
};
static_assert(offsetof(PcSmoothRibbonParticles, segments) == 0x138 &&
                  offsetof(PcSmoothRibbonParticles, material) == 0x13C && sizeof(PcSmoothRibbonParticles) == 0x140,
              "layout must match the original");

// The "smoothstar" type (0x150 bytes, vtables 0x0064A5EC and 0x0064A5E8; the engine's class 0x0049BE80 -
// destructor body 0x0049BE20): each particle a star of `segments` points (star_width_ out) about its centre - two
// quads a point: centre, tip, the next outer point and the inner one; three colours.
struct PcSmoothStarParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;      // +0x134
    float star_width_; // +0x138
    uint8_t engine_13c_[0xC];
    SimpleMaterial* material; // +0x148
    uint8_t unknown14c_[4];

    // 0x004C3D80: construct. Returns this.
    PcSmoothStarParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                    int fixed_alpha, int segments, float unknown);
    // 0x004C3E50: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C4880 (virtual [0]) / 0x004C3D70 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcSmoothStarParticles* teardown(unsigned int free_flags);
    // 0x004C3EC0 (virtual [1]): drawn. No emission.
    void render();
};
static_assert(offsetof(PcSmoothStarParticles, segments) == 0x134 &&
                  offsetof(PcSmoothStarParticles, star_width_) == 0x138 &&
                  offsetof(PcSmoothStarParticles, material) == 0x148 && sizeof(PcSmoothStarParticles) == 0x150,
              "layout must match the original");

// The "star" type (0x150 bytes, vtables 0x0064A604 and 0x0064A600; the engine's class 0x0049C140 - destructor body
// 0x0049C0D0): each particle a star of `segments` points about its centre, a quad a point.
struct PcStarParticles : PcFlexibleParticles {
    uint8_t engine128[0xC];
    int segments;      // +0x134
    float star_width_; // +0x138
    uint8_t engine_13c_[0xC];
    SimpleMaterial* material; // +0x148
    uint8_t unknown14c_[4];

    // 0x004C48C0: construct. Returns this.
    PcStarParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                              int segments, float unknown);
    // 0x004C4990: the destructor's body (the material freed).
    void destroy_body();
    // 0x004C5200 (virtual [0]) / 0x004C48B0 (through the vtable at +0x0C): destroyed, and freed with flags & 1.
    PcStarParticles* teardown(unsigned int free_flags);
    // 0x004C4A00 (virtual [1]): drawn. No emission.
    void render();
};
static_assert(offsetof(PcStarParticles, segments) == 0x134 && offsetof(PcStarParticles, star_width_) == 0x138 &&
                  offsetof(PcStarParticles, material) == 0x148 && sizeof(PcStarParticles) == 0x150,
              "layout must match the original");

extern IDirect3DIndexBuffer9* g_quad_indices;

} // namespace render
