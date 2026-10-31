#pragma once

#include <cstdint>

#include "engine/gfx/particle_system.h"

// What the flexible particle types' engine constructors and destructors share (particle_system.cpp).
namespace render::particle_parts {

// The particles: `count` after their count (the compiler's array new; null if no memory) / freed.
FlexibleParticle* new_particles(int count);
void delete_particles(FlexibleParticle* particles);
// The three colour arrays, `count` RGBA colours each, each allocated only if the one before was; all grey once the
// three are there. Whether they are.
bool new_colours(EmitterInstance* system, int count);
// After the base's constructor: the name and the maximum, no particles yet; the particles, then the positions
// (`positions_zeroed`: zeroed memory), then `colours` colours. Whether everything was allocated.
bool construct_parts(EmitterInstance* system, uint32_t name, int particle_cap, unsigned int positions_size,
                     bool positions_zeroed, int colours);

} // namespace render::particle_parts
