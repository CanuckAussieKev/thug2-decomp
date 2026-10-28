#pragma once

#include <cstdint>

#include "game/gfx/particle_system.h"

// What the flexible particle types' engine constructors and destructors share (particle_system.cpp).
namespace Nx::ParticleParts {

// The particles: `count` after their count (the compiler's array new; null if no memory) / freed.
sFlexibleParticle* NewParticles(int count);
void DeleteParticles(sFlexibleParticle* particles);
// The three colour arrays, `count` RGBA colours each, each allocated only if the one before was; all grey once the
// three are there. Whether they are.
bool NewColours(CParticleSystem* system, int count);
// After the base's constructor: the name and the maximum, no particles yet; the particles, then the positions
// (`positionsZeroed`: zeroed memory), then `colours` colours. Whether everything was allocated.
bool ConstructParts(CParticleSystem* system, uint32_t name, int maxParticles, unsigned int positionsSize,
                    bool positionsZeroed, int colours);
// The system's vtable and its node's.
void SetVtables(CParticleSystem* system, uint32_t primary, uint32_t node);

} // namespace Nx::ParticleParts
