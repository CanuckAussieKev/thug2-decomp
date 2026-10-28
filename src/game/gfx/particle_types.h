#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/particle_system.h"

// The rest of the flexible particle types' engine classes (particle_system.h has newflat, the flat ribbon, glow,
// glowribbontrail, line and the model type): ribbon, ribbontrail, shaded, smooth, smoothribbon, smoothstar and star.
// Each is constructed as the others are - (name, max particles, texture, blend mode, fixed alpha, segments, a float,
// history), each reading what it uses -, its particles, positions and colours allocated (each step stops at the first
// that fails), the colours grey, no mid colour, flag 4. Their PC classes over them are in pc_flexible_particles.h.
namespace Nx {

// "ribbon" (vtables 0x006494DC and 0x006494D8): history + 1 positions each (zeroed), two colours.
struct CRibbonParticles : CParticleSystem {
    int m_steps; // +0x134: history + 1

    // 0x0049B0D0. Returns this.
    CRibbonParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                int segments, float unknown, int history);
    // 0x0049B070: the vtables, the base's body.
    void DestroyBody();
    // 0x0049B0A0 (vtable slot 0) / 0x0049B090 (through the node)
    CRibbonParticles* Destroy(unsigned int flags);
};

// "ribbontrail" (vtables 0x006494F0 and 0x006494EC): history + 1 positions each (zeroed), a colour a step.
struct CRibbonTrailParticles : CParticleSystem {
    int m_steps; // +0x134: history + 1

    // 0x0049B380. Returns this.
    CRibbonTrailParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                     int fixedAlpha, int segments, float unknown, int history);
    // 0x0049B320
    void DestroyBody();
    // 0x0049B350 (vtable slot 0) / 0x0049B340 (through the node)
    CRibbonTrailParticles* Destroy(unsigned int flags);
};

// "shaded" (vtables 0x00649504 and 0x00649500): a position each, four colours.
struct CShadedParticles : CParticleSystem {
    // 0x0049B660. Returns this.
    CShadedParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                int segments, float unknown, int history);
    // 0x0049B600
    void DestroyBody();
    // 0x0049B630 (vtable slot 0) / 0x0049B620 (through the node)
    CShadedParticles* Destroy(unsigned int flags);
};

// "smooth" (vtables 0x00649518 and 0x00649514): a position each, two colours.
struct CSmoothParticles : CParticleSystem {
    // 0x0049B910. Returns this.
    CSmoothParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                int segments, float unknown, int history);
    // 0x0049B8A0
    void DestroyBody();
    // 0x0049B8E0 (vtable slot 0) / 0x0049B8D0 (through the node)
    CSmoothParticles* Destroy(unsigned int flags);
};

// "smoothribbon" (vtables 0x0064952C and 0x00649528): history + 1 positions each (zeroed), four colours.
struct CSmoothRibbonParticles : CParticleSystem {
    int m_steps; // +0x134: history + 1 (set once everything is allocated)

    // 0x0049BBD0. Returns this.
    CSmoothRibbonParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                      int fixedAlpha, int segments, float unknown, int history);
    // 0x0049BB50
    void DestroyBody();
    // 0x0049BBA0 (vtable slot 0) / 0x0049BB90 (through the node)
    CSmoothRibbonParticles* Destroy(unsigned int flags);
};

// "smoothstar" (vtables 0x00649540 and 0x0064953C): a position each, three colours; the star's points and size.
struct CSmoothStarParticles : CParticleSystem {
    int m_segments;  // +0x134: the star's points
    float m_size;    // +0x138

    // 0x0049BE80. Returns this.
    CSmoothStarParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                    int fixedAlpha, int segments, float size, int history);
    // 0x0049BE20
    void DestroyBody();
    // 0x0049BE50 (vtable slot 0) / 0x0049BE40 (through the node)
    CSmoothStarParticles* Destroy(unsigned int flags);
};

// "star" (vtables 0x00649554 and 0x00649550): as smoothstar.
struct CStarParticles : CParticleSystem {
    int m_segments; // +0x134
    float m_size;   // +0x138

    // 0x0049C140. Returns this.
    CStarParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                              int segments, float size, int history);
    // 0x0049C0D0
    void DestroyBody();
    // 0x0049C110 (vtable slot 0) / 0x0049C100 (through the node)
    CStarParticles* Destroy(unsigned int flags);
};

static_assert(offsetof(CRibbonParticles, m_steps) == 0x134 && offsetof(CSmoothStarParticles, m_size) == 0x138 &&
                  offsetof(CStarParticles, m_size) == 0x138,
              "layout must match the original");

// The types' counts (vtable slots 3 and 2; linker-folded: one body each, shared by every class whose count it is):
// 0x0049B8C0 two colours (ribbon, smooth), 0x0049BB70 four (shaded, smoothribbon), 0x0049C0F0 three (smoothstar,
// star, glow), 0x0049BB80 the int at +0x134 (the vertex lists of the ribbons, ribbontrail's colours too).
struct CParticleCounts {
    int TwoColours();
    int FourColours();
    int ThreeColours();
    int Field134();
};

} // namespace Nx
