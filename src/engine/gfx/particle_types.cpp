// The flexible particle types' engine classes: ribbon, ribbontrail, shaded, smooth, smoothribbon, smoothstar, star
// (see particle_types.h). From the disassembly of 0x0049B070-0x0049C384.

#include <cstddef>
#include <cstdint>

#include "engine/gfx/particle_types.h"

#include "engine/core/mem.h"
#include "engine/gfx/particle_system_internal.h"

namespace render {

using namespace particle_parts;

namespace {

constexpr unsigned int kPosition = 12; // three floats

} // namespace

// --- ribbon ---

// 0x0049B0D0
RibbonParticles::RibbonParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float, int history)
{
    steps_ = history + 1;
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * steps_) * kPosition, true,
                        2)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// --- ribbontrail ---

// 0x0049B380
RibbonTrailParticles::RibbonTrailParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float,
                                           int history)
{
    steps_ = history + 1;
    // (the colours' count read again from steps_ for each array and each step of the loop)
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * steps_) * kPosition, true,
                        steps_)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// --- shaded ---

// 0x0049B660
ShadedParticles::ShadedParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float, int)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 4)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// --- smooth ---

// 0x0049B910
SmoothParticles::SmoothParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float, int)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 2)) {
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// --- smoothribbon ---

// 0x0049BBD0
SmoothRibbonParticles::SmoothRibbonParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int, float,
                                             int history)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap * (history + 1)) * kPosition,
                        true, 4)) {
        steps_ = history + 1;
        colour_mid_ = -1.0f;
        flag_bits_ |= 4u;
    }
}

// --- smoothstar ---

// 0x0049BE80
SmoothStarParticles::SmoothStarParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int segments,
                                         float size, int)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 3)) {
        segments_ = segments;
        colour_mid_ = -1.0f;
        extent_ = size;
        flag_bits_ |= 4u;
    }
}

// --- star ---

// 0x0049C140
StarParticles::StarParticles(uint32_t name, int particle_cap, uint32_t, uint32_t, int, int segments, float size, int)
{
    if (construct_parts(this, name, particle_cap, static_cast<unsigned int>(particle_cap) * kPosition, false, 3)) {
        segments_ = segments;
        colour_mid_ = -1.0f;
        extent_ = size;
        flag_bits_ |= 4u;
    }
}

// --- the counts ---

// 0x0049B8C0 / 0x0049BB70 / 0x0049C0F0 / 0x0049BB80
int ParticleCounts::two_colours() { return 2; }
int ParticleCounts::four_colours() { return 4; }
int ParticleCounts::three_colours() { return 3; }
int ParticleCounts::field134() { return *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(this) + 0x134); }

} // namespace render
