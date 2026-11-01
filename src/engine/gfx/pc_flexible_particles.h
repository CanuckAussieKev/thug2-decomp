#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_types.h"
#include "engine/maths/vec4.h"

// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor): a COM interface: released, never deleted
struct IDirect3DIndexBuffer9;

// The PC flexible particle systems (Nx engine; the particle manager's 0x0049AC60 makes them by type: the flexible
// particle component's systems): each type the engine's class of its own (its update, its particles) with a PC one
// over it that draws them - camera-facing quads, ribbons, stars, glows - through the dynamic vertex buffer with a
// material of its own (SSimpleMaterial), and emits the particles its rate asks for.
namespace render {

struct SimpleMaterial;

// (SFlexibleParticle: particle_system.h.)

// 0x004BCEF0: a new system of `type` (glow, ribbon, smoothstar, star, line, smooth, glowribbontrail, smoothribbon,
// ribbontrail, shaded, 0x3D56A7A6 - a flat ribbon -, newflat - and newflat for any other type -; 0x286A8D26 the engine's
// own class 0x0049A0C0, without a PC one, from `model`), each from the parameters it uses. Null if no memory.
EmitterInstance* create_flexible_particles(uint32_t name, uint32_t type, int particle_cap, int max_streams,
                                           uint32_t texture, uint32_t blend_mode, int fixed_alpha, int segments,
                                           float unknown, int history, const char* model);

// The "newflat" type (0x148 bytes, vtable 0x0064A514; the engine's class 0x00499010 -
// destructor body 0x00498FB0): each particle a quad, its size and colour through its life.
struct PcNewFlatParticles final : NewFlatParticles {
    uint8_t unknown134_[0xC];
    SimpleMaterial* material; // +0x140
    float emitted;            // +0x144: particles owed, a fraction

    // 0x004BD3A0
    PcNewFlatParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                       int segments, float unknown);
    // 0x004BD610 (0x004BDC40 the scalar deleting destructor; 0x004BD390 through the node): the material freed, then the engine's.
    ~PcNewFlatParticles() override;
    // 0x004BD680 (vtable slot 1): the particles drawn, then emitted for the frame. While a draw fills the vertex
    // buffer to within a quad of its room, the original stops - those particles and the rest aren't drawn.
    void render() override;
};

// The type 0x3D56A7A6 (its name unknown - a flat ribbon trail; 0x144 bytes, vtable 0x0064A528;
// the engine's class 0x004992C0 - destructor body 0x00499200): each particle's history of `segments` positions
// drawn as a ribbon (its width across the camera and the path), its colours per segment, then a head quad of five
// times the width at the particle.
struct PcFlatRibbonParticles final : FlatRibbonParticles {
    uint8_t unknown138_[4];
    float width;              // +0x13C
    SimpleMaterial* material; // +0x140

    // 0x004BDC80
    PcFlatRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                          int segments, float width, int history);
    // 0x004BDD60 (0x004BECD0 the scalar deleting destructor; 0x004BDC70 through the node): the material freed, then the engine's.
    ~PcFlatRibbonParticles() override;
    // 0x004BDDD0 (vtable slot 1): the ribbons (untextured), then the heads (the default pixel shader); a draw that
    // fills the buffer to within four vertices of its room stops that pass undrawn, as the original. No emission.
    void render() override;
};

// The "glow" type (0x150 bytes, vtable 0x0064A540; the engine's class 0x004995B0 -
// destructor body 0x00499550): each particle a fan of `segments` around it - a disc (the centre's colour to the inner
// ring's) and a ring of `glowWidth` times its size beyond (to the outer colour), in the camera's plane - written to a
// buffer on the stack and drawn with DrawPrimitiveUP (0x7FF words a draw). No emission.
struct PcGlowParticles final : GlowParticles {
    uint8_t unknown13c_[0xC];
    SimpleMaterial* material; // +0x148
    uint8_t unknown14c_[4];

    // 0x004BED10
    PcGlowParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                    int segments, float unknown);
    // 0x004BEDE0 (0x004BF6E0 the scalar deleting destructor; 0x004BED00 through the node): the material freed, then the engine's.
    ~PcGlowParticles() override;
    // 0x004BEE50 (vtable slot 1): drawn. The original's triangle count is a quarter of the triangles it writes (words /
    // 16 / 3) - kept.
    void render() override;
};

// The "glowribbontrail" type (0x150 bytes, vtable 0x0064A558; the engine's class 0x00499880 -
// destructor body 0x00499800): each particle's history as a ribbon (two triangles a step; its colours from index 3 of
// the colour arrays on) and a glow fan about its head (colours 0-2, the fraction gone through the mid selection a
// second time, as the original) - all the particles locked at once and drawn in one DrawPrimitive with alpha
// blending off; positions as stored (the emitter's not added). Colours in 4096ths, not scaled.
struct PcGlowRibbonTrailParticles final : GlowRibbonTrailParticles {
    uint8_t unknown13c_[0xC];
    SimpleMaterial* material; // +0x148
    float glow_step_;         // +0x14C: degrees between the glow's points

    // 0x004BF720
    PcGlowRibbonTrailParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                               int segments, float unknown, int history);
    // 0x004C0940 (0x004C09B0 the scalar deleting destructor; 0x004BF710 through the node): the material freed, then the engine's.
    ~PcGlowRibbonTrailParticles() override;
    // 0x004BF7F0 (vtable slot 1): drawn. No emission.
    void render() override;
    // (Inline in Render.)
    static uint32_t fine_colour(uint32_t from, uint32_t to, double t);
};

// The "line" type (0x148 bytes, vtable 0x0064A570; the engine's class 0x00499B80 -
// destructor body 0x00499B20): each particle a line from its position to its next history step, a colour each end.
struct PcLineParticles final : LineParticles {
    uint8_t unknown138_[8];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144_[4];

    // 0x004C09F0
    PcLineParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                    int segments, float unknown);
    // 0x004C0AC0 (0x004C0E70 the scalar deleting destructor; 0x004C09E0 through the node): the material freed, then the engine's.
    ~PcLineParticles() override;
    // 0x004C0B30 (vtable slot 1): drawn as a line list (no index buffer); a draw that fills the buffer to within two
    // vertices stops undrawn, as the original. No emission.
    void render() override;
};

// The "ribbon" type (0x148 bytes, vtable 0x0064A584; the engine's class 0x0049B0D0 - destructor body
// 0x0049B070): each particle a quad from its position to its oldest history step, as wide as its size across the
// camera and the path - textured when the material has a texture.
struct PcRibbonParticles final : RibbonParticles {
    uint8_t unknown138_[8];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144_[4];

    // 0x004C0EB0
    PcRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                      int segments, float unknown, int history);
    // 0x004C0F80 (0x004C16E0 the scalar deleting destructor; 0x004C0EA0 through the node): the material freed, then the engine's.
    ~PcRibbonParticles() override;
    // 0x004C0FF0 (vtable slot 1): drawn. No emission.
    void render() override;
};

// The "ribbontrail" type (0x148 bytes, vtable 0x0064A598; the engine's class 0x0049B380 - destructor
// body 0x0049B320): each particle's history drawn as a ribbon of `segments` points, its head end's colour scaled by
// the brightness alone (the others by lerp_colour).
struct PcRibbonTrailParticles final : RibbonTrailParticles {
    uint8_t unknown138_[8];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144_[4];

    // 0x004C1720
    PcRibbonTrailParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                           int segments, float unknown, int history);
    // 0x004C17F0 (0x004C2160 the scalar deleting destructor; 0x004C1710 through the node): the material freed, then the engine's.
    ~PcRibbonTrailParticles() override;
    // 0x004C1860 (vtable slot 1): drawn. No emission.
    void render() override;
    // (Inline in Render.)
    uint32_t head_colour(uint32_t from, uint32_t to, float t);
};

// The "shaded" type (0x138 bytes, vtable 0x0064A5AC; the engine's class 0x0049B660 - destructor body
// 0x0049B600): each particle a quad with a colour per corner, facing the camera or along alignTo; with
// UseRandomUVQuadrants (flags 0x4000) a quarter of the texture by the particle's index.
struct PcShadedParticles final : ShadedParticles {
    SimpleMaterial* material; // +0x134

    // 0x004C21A0
    PcShadedParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha);
    // 0x004C2260 (0x004C2AF0 the scalar deleting destructor; 0x004C2190 through the node): the material freed, then the engine's.
    ~PcShadedParticles() override;
    // 0x004C22D0 (vtable slot 1): drawn (the material's own pixel shader). No emission.
    void render() override;
};

// The "smooth" type (0x148 bytes, vtable 0x0064A5C0; the engine's class 0x0049B910 - destructor body
// 0x0049B8A0): each particle four triangles from its centre (its own colour) to the quad's corners (the edge's).
struct PcSmoothParticles final : SmoothParticles {
    uint8_t unknown134_[0xC];
    SimpleMaterial* material; // +0x140
    uint8_t unknown144_[4];

    // 0x004C2B30
    PcSmoothParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                      int segments, float unknown);
    // 0x004C2C00 (0x004C3500 the scalar deleting destructor; 0x004C2B20 through the node): the material freed, then the engine's.
    ~PcSmoothParticles() override;
    // 0x004C2C70 (vtable slot 1): drawn (DrawPrimitive, no index buffer; a draw that fills the buffer to within twelve
    // vertices stops undrawn, as the original). No emission.
    void render() override;
};

// The "smoothribbon" type (0x140 bytes, vtable 0x0064A5D4; the engine's class 0x0049BBD0 -
// destructor body 0x0049BB50): each particle two quads from the line between its position and its oldest history
// step out to either side, four colours.
struct PcSmoothRibbonParticles final : SmoothRibbonParticles {
    int segments;             // +0x138: history steps
    SimpleMaterial* material; // +0x13C

    // 0x004C3550
    PcSmoothRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                            int segments, float unknown, int history);
    // 0x004C3620 (0x004C3D40 the scalar deleting destructor; 0x004C3540 through the node): the material freed, then the engine's.
    ~PcSmoothRibbonParticles() override;
    // 0x004C3690 (vtable slot 1): drawn. No emission.
    void render() override;
    // 0x004C3530 (vtable slot 2): its own history steps (segments, not the engine's steps_).
    int get_num_vertex_lists() override;
};

// The "smoothstar" type (0x150 bytes, vtable 0x0064A5EC; the engine's class 0x0049BE80 -
// destructor body 0x0049BE20): each particle a star of `segments` points (star_width_ out) about its centre - two
// quads a point: centre, tip, the next outer point and the inner one; three colours.
struct PcSmoothStarParticles final : SmoothStarParticles {
    uint8_t unknown13c_[0xC];
    SimpleMaterial* material; // +0x148
    uint8_t unknown14c_[4];

    // 0x004C3D80
    PcSmoothStarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                          int segments, float unknown);
    // 0x004C3E50 (0x004C4880 the scalar deleting destructor; 0x004C3D70 through the node): the material freed, then the engine's.
    ~PcSmoothStarParticles() override;
    // 0x004C3EC0 (vtable slot 1): drawn. No emission.
    void render() override;
};

// The "star" type (0x150 bytes, vtable 0x0064A604; the engine's class 0x0049C140 - destructor body
// 0x0049C0D0): each particle a star of `segments` points about its centre, a quad a point.
struct PcStarParticles final : StarParticles {
    uint8_t unknown13c_[0xC];
    SimpleMaterial* material; // +0x148
    uint8_t unknown14c_[4];

    // 0x004C48C0
    PcStarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                    int segments, float unknown);
    // 0x004C4990 (0x004C5200 the scalar deleting destructor; 0x004C48B0 through the node): the material freed, then the engine's.
    ~PcStarParticles() override;
    // 0x004C4A00 (vtable slot 1): drawn. No emission.
    void render() override;
};

extern IDirect3DIndexBuffer9* g_quad_indices;

} // namespace render
