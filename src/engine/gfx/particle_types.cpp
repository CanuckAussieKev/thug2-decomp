// The flexible particle types' engine classes: ribbon, ribbontrail, shaded, smooth, smoothribbon, smoothstar, star
// (see particle_types.h). From the disassembly of 0x0049B070-0x0049C384.

#include <cstdint>

#include "engine/gfx/particle_types.h"

#include "engine/gfx/particle_system_internal.h"
#include "engine/core/checksums.h"
#include "engine/gfx/particle_system.h"

namespace render {

using namespace particle_parts;

namespace {

constexpr unsigned int kPosition = 12; // three floats

} // namespace

// --- ribbon ---

// 0x0049B0D0
RibbonParticles::RibbonParticles(uint32_t name, int particle_cap, uint32_t /*unused*/, uint32_t /*unused*/,
                                 int /*unused*/, int /*unused*/, float /*unused*/, int history)
    : steps_(history + 1)
{
    if (construct_parts(this, checksums::Name{name}, {.particle_cap_ = particle_cap, .positions_size_ = static_cast<unsigned int>(particle_cap * steps_) * kPosition}, true,
                        2)) {
        set_colour_mid(-1.0F);
        flag_bits() |= 4U;
    }
}

// --- ribbontrail ---

// 0x0049B380
RibbonTrailParticles::RibbonTrailParticles(uint32_t name, int particle_cap, uint32_t /*unused*/, uint32_t /*unused*/,
                                           int /*unused*/, int /*unused*/, float /*unused*/, int history)
    : steps_(history + 1)
{
    // (the colours' count read again from steps_ for each array and each step of the loop)
    if (construct_parts(this, checksums::Name{name}, {.particle_cap_ = particle_cap, .positions_size_ = static_cast<unsigned int>(particle_cap * steps_) * kPosition}, true,
                        steps_)) {
        set_colour_mid(-1.0F);
        flag_bits() |= 4U;
    }
}

// --- shaded ---

// 0x0049B660
ShadedParticles::ShadedParticles(uint32_t name, int particle_cap, uint32_t /*unused*/, uint32_t /*unused*/,
                                 int /*unused*/, int /*unused*/, float /*unused*/, int /*unused*/)
{
    if (construct_parts(this, checksums::Name{name}, {.particle_cap_ = particle_cap, .positions_size_ = static_cast<unsigned int>(particle_cap) * kPosition}, false, 4)) {
        set_colour_mid(-1.0F);
        flag_bits() |= 4U;
    }
}

// --- smooth ---

// 0x0049B910
SmoothParticles::SmoothParticles(uint32_t name, int particle_cap, uint32_t /*unused*/, uint32_t /*unused*/,
                                 int /*unused*/, int /*unused*/, float /*unused*/, int /*unused*/)
{
    if (construct_parts(this, checksums::Name{name}, {.particle_cap_ = particle_cap, .positions_size_ = static_cast<unsigned int>(particle_cap) * kPosition}, false, 2)) {
        set_colour_mid(-1.0F);
        flag_bits() |= 4U;
    }
}

// --- smoothribbon ---

// 0x0049BBD0
SmoothRibbonParticles::SmoothRibbonParticles(uint32_t name, int particle_cap, uint32_t /*unused*/, uint32_t /*unused*/,
                                             int /*unused*/, int /*unused*/, float /*unused*/, int history)
{
    if (construct_parts(this, checksums::Name{name}, {.particle_cap_ = particle_cap, .positions_size_ = static_cast<unsigned int>(particle_cap * (history + 1)) * kPosition},
                        true, 4)) {
        steps_ = history + 1;
        set_colour_mid(-1.0F);
        flag_bits() |= 4U;
    }
}

// --- smoothstar ---

// 0x0049BE80
SmoothStarParticles::SmoothStarParticles(const ConstructArgs& args)
{
    const uint32_t name = args.name;
    const int particle_cap = args.particle_cap_;
    const int segments = args.segments;
    const float size = args.unknown;
    if (construct_parts(this, checksums::Name{name}, {.particle_cap_ = particle_cap, .positions_size_ = static_cast<unsigned int>(particle_cap) * kPosition}, false, 3)) {
        segments_ = segments;
        set_colour_mid(-1.0F);
        extent_ = size;
        flag_bits() |= 4U;
    }
}

// --- star ---

// 0x0049C140
StarParticles::StarParticles(const ConstructArgs& args)
{
    const uint32_t name = args.name;
    const int particle_cap = args.particle_cap_;
    const int segments = args.segments;
    const float size = args.unknown;
    if (construct_parts(this, checksums::Name{name}, {.particle_cap_ = particle_cap, .positions_size_ = static_cast<unsigned int>(particle_cap) * kPosition}, false, 3)) {
        segments_ = segments;
        set_colour_mid(-1.0F);
        extent_ = size;
        flag_bits() |= 4U;
    }
}

} // namespace render
