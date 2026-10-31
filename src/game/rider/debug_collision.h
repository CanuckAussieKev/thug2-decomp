#pragma once

// NOT ORIGINAL - [features] debug_collision (thug2-decomp.ini): logging what collision feelers hit. See
// debug_collision.cpp.

#include <cstdint>

namespace collision_overlay {

// After a feeler's query (render::ProbeRay::get_hit_shape): `hit` is its result, `caller` the return address into the
// code that asked. Logs the hit when the feature is on.
void note_feeler_query(const void* feeler, int hit, uint32_t caller);

} // namespace collision_overlay
