#pragma once

#include <cstdint>

#include "engine/gfx/particle_system.h"

// What the flexible particle types' engine constructors and destructors share (particle_system.cpp).
namespace Engine3D::ParticleParts {

// The particles: `count` after their count (the compiler's array new; null if no memory) / freed.
sFlexibleParticle* NewParticles(int count);
void DeleteParticles(sFlexibleParticle* particles);
// The three colour arrays, `count` RGBA colours each, each allocated only if the one before was; all grey once the
// three are there. Whether they are.
bool NewColours(CEmitterInstance* system, int count);
// After the base's constructor: the name and the maximum, no particles yet; the particles, then the positions
// (`positionsZeroed`: zeroed memory), then `colours` colours. Whether everything was allocated.
bool ConstructParts(CEmitterInstance* system, uint32_t name, int particleCap, unsigned int positionsSize,
                    bool positionsZeroed, int colours);
// The system's vtable and its node's.
void SetVtables(CEmitterInstance* system, void** primary, void** node);

} // namespace Engine3D::ParticleParts
