#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/particle_system.h"

// The rest of the flexible particle types' engine classes (particle_system.h has newflat, the flat ribbon, glow,
// glowribbontrail, line and the model type): ribbon, ribbontrail, shaded, smooth, smoothribbon, smoothstar and star.
// Each is constructed as the others are - (name, max particles, texture, blend mode, fixed alpha, segments, a float,
// history), each reading what it uses -, its particles, positions and colours allocated (each step stops at the first
// that fails), the colours grey, no mid colour, flag 4. Their PC classes over them are in pc_flexible_particles.h.
namespace render {

// "ribbon" (vtables 0x006494DC and 0x006494D8): history + 1 positions each (zeroed), two colours.
struct RibbonParticles : EmitterInstance {
    int steps_; // +0x134: history + 1

    // 0x0049B0D0. Returns this.
    RibbonParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                                int segments, float unknown, int history);
    // 0x0049B070: the vtables, the base's body.
    void destroy_body();
    // 0x0049B0A0 (vtable slot 0) / 0x0049B090 (through the node)
    RibbonParticles* teardown(unsigned int flags);
};

// "ribbontrail" (vtables 0x006494F0 and 0x006494EC): history + 1 positions each (zeroed), a colour a step.
struct RibbonTrailParticles : EmitterInstance {
    int steps_; // +0x134: history + 1

    // 0x0049B380. Returns this.
    RibbonTrailParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha, int segments, float unknown, int history);
    // 0x0049B320
    void destroy_body();
    // 0x0049B350 (vtable slot 0) / 0x0049B340 (through the node)
    RibbonTrailParticles* teardown(unsigned int flags);
};

// "shaded" (vtables 0x00649504 and 0x00649500): a position each, four colours.
struct ShadedParticles : EmitterInstance {
    // 0x0049B660. Returns this.
    ShadedParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                                int segments, float unknown, int history);
    // 0x0049B600
    void destroy_body();
    // 0x0049B630 (vtable slot 0) / 0x0049B620 (through the node)
    ShadedParticles* teardown(unsigned int flags);
};

// "smooth" (vtables 0x00649518 and 0x00649514): a position each, two colours.
struct SmoothParticles : EmitterInstance {
    // 0x0049B910. Returns this.
    SmoothParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                                int segments, float unknown, int history);
    // 0x0049B8A0
    void destroy_body();
    // 0x0049B8E0 (vtable slot 0) / 0x0049B8D0 (through the node)
    SmoothParticles* teardown(unsigned int flags);
};

// "smoothribbon" (vtables 0x0064952C and 0x00649528): history + 1 positions each (zeroed), four colours.
struct SmoothRibbonParticles : EmitterInstance {
    int steps_; // +0x134: history + 1 (set once everything is allocated)

    // 0x0049BBD0. Returns this.
    SmoothRibbonParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                      int fixed_alpha, int segments, float unknown, int history);
    // 0x0049BB50
    void destroy_body();
    // 0x0049BBA0 (vtable slot 0) / 0x0049BB90 (through the node)
    SmoothRibbonParticles* teardown(unsigned int flags);
};

// "smoothstar" (vtables 0x00649540 and 0x0064953C): a position each, three colours; the star's points and size.
struct SmoothStarParticles : EmitterInstance {
    int segments_;  // +0x134: the star's points
    float extent_;    // +0x138

    // 0x0049BE80. Returns this.
    SmoothStarParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                    int fixed_alpha, int segments, float size, int history);
    // 0x0049BE20
    void destroy_body();
    // 0x0049BE50 (vtable slot 0) / 0x0049BE40 (through the node)
    SmoothStarParticles* teardown(unsigned int flags);
};

// "star" (vtables 0x00649554 and 0x00649550): as smoothstar.
struct StarParticles : EmitterInstance {
    int segments_; // +0x134
    float extent_;   // +0x138

    // 0x0049C140. Returns this.
    StarParticles* make_new(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                              int segments, float size, int history);
    // 0x0049C0D0
    void destroy_body();
    // 0x0049C110 (vtable slot 0) / 0x0049C100 (through the node)
    StarParticles* teardown(unsigned int flags);
};

static_assert(offsetof(RibbonParticles, steps_) == 0x134 && offsetof(SmoothStarParticles, extent_) == 0x138 &&
                  offsetof(StarParticles, extent_) == 0x138,
              "layout must match the original");

// The types' counts (vtable slots 3 and 2; linker-folded: one body each, shared by every class whose count it is):
// 0x0049B8C0 two colours (ribbon, smooth), 0x0049BB70 four (shaded, smoothribbon), 0x0049C0F0 three (smoothstar,
// star, glow), 0x0049BB80 the int at +0x134 (the vertex lists of the ribbons, ribbontrail's colours too).
struct ParticleCounts {
    int two_colours();
    int four_colours();
    int three_colours();
    int field134();
};

} // namespace Nx
