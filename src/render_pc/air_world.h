#pragma once

// NOT ORIGINAL - round W: the level's air transport (engine/gfx/air_volume.h) made from the loaded world - every
// collidable face of its sectors (render_pc/snow_world.h collision_faces) with the share of it open to air by what its
// terrain says it is made of (rt_material_table.h kind_of_terrain: a chain-link fence, a grating let air through; glass
// and everything else none). Made again whenever the loaded sectors change (a level, the editor's loads); the faces read
// on the game's thread, the volume built on a worker and published when done (air::set_volume) - until then, and with
// no level, every place reads as open air. Read only: nothing of the game's is written.
#include <cstdint>

namespace render::air_world {

// snow_world::step's (every simulation step): `sectors` the loaded world's sectors' key.
void step(uint64_t sectors);

} // namespace render::air_world
