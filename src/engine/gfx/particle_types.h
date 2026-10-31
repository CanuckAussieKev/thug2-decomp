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

// "ribbon" (vtable 0x006494DC): history + 1 positions each (zeroed), two colours.
struct RibbonParticles : EmitterInstance {
    int steps_; // +0x134: history + 1

    // 0x0049B0D0
    RibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                    int segments, float unknown, int history);
    // 0x0049B070 (0x0049B0A0 the scalar deleting destructor; 0x0049B090 through the node): the base's.
    ~RibbonParticles() override = default;
    int get_num_vertex_lists() override { return steps_; }
    int get_num_colours() override { return 2; }
};

// "ribbontrail" (vtable 0x006494F0): history + 1 positions each (zeroed), a colour a step.
struct RibbonTrailParticles : EmitterInstance {
    int steps_; // +0x134: history + 1

    // 0x0049B380
    RibbonTrailParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                         int segments, float unknown, int history);
    // 0x0049B320 (0x0049B350 the scalar deleting destructor; 0x0049B340 through the node): the base's.
    ~RibbonTrailParticles() override = default;
    int get_num_vertex_lists() override { return steps_; }
    int get_num_colours() override { return steps_; }
};

// "shaded" (vtable 0x00649504): a position each, four colours.
struct ShadedParticles : EmitterInstance {
    // 0x0049B660
    ShadedParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                    int segments, float unknown, int history);
    // 0x0049B600 (0x0049B630 the scalar deleting destructor; 0x0049B620 through the node): the base's.
    ~ShadedParticles() override = default;
    int get_num_vertex_lists() override { return 0; }
    int get_num_colours() override { return 4; }
};

// "smooth" (vtable 0x00649518): a position each, two colours.
struct SmoothParticles : EmitterInstance {
    // 0x0049B910
    SmoothParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                    int segments, float unknown, int history);
    // 0x0049B8A0 (0x0049B8E0 the scalar deleting destructor; 0x0049B8D0 through the node): the base's.
    ~SmoothParticles() override = default;
    int get_num_vertex_lists() override { return 0; }
    int get_num_colours() override { return 2; }
};

// "smoothribbon" (vtable 0x0064952C): history + 1 positions each (zeroed), four colours.
struct SmoothRibbonParticles : EmitterInstance {
    int steps_; // +0x134: history + 1 (set once everything is allocated)

    // 0x0049BBD0
    SmoothRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                          int segments, float unknown, int history);
    // 0x0049BB50 (0x0049BBA0 the scalar deleting destructor; 0x0049BB90 through the node): the base's.
    ~SmoothRibbonParticles() override = default;
    int get_num_vertex_lists() override { return steps_; }
    int get_num_colours() override { return 4; }
};

// "smoothstar" (vtable 0x00649540): a position each, three colours; the star's points and size.
struct SmoothStarParticles : EmitterInstance {
    int segments_; // +0x134: the star's points
    float extent_; // +0x138

    // 0x0049BE80
    SmoothStarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                        int segments, float size, int history);
    // 0x0049BE20 (0x0049BE50 the scalar deleting destructor; 0x0049BE40 through the node): the base's.
    ~SmoothStarParticles() override = default;
    int get_num_vertex_lists() override { return 0; }
    int get_num_colours() override { return 3; }
};

// "star" (vtable 0x00649554): as smoothstar.
struct StarParticles : EmitterInstance {
    int segments_; // +0x134
    float extent_; // +0x138

    // 0x0049C140
    StarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha, int segments,
                  float size, int history);
    // 0x0049C0D0 (0x0049C110 the scalar deleting destructor; 0x0049C100 through the node): the base's.
    ~StarParticles() override = default;
    int get_num_vertex_lists() override { return 0; }
    int get_num_colours() override { return 3; }
};

static_assert(offsetof(RibbonParticles, steps_) == 0x134 && offsetof(SmoothStarParticles, extent_) == 0x138 &&
                  offsetof(StarParticles, extent_) == 0x138,
              "layout must match the original");

// The types' slot 2 / 3 bodies as the original's linker folded them (0x0049B8C0 two colours, 0x0049BB70 four,
// 0x0049C0F0 three, 0x0049BB80 the int at +0x134): the particle classes override with their own now; only the
// resource tables (resource_entry.cpp) still name them.
struct ParticleCounts {
    int two_colours();
    int four_colours();
    int three_colours();
    int field134();
};

} // namespace render
