#pragma once

// NOT ORIGINAL - [features] debug_collision (thug2-decomp.ini): logging what collision feelers hit. See
// debug_collision.cpp.

#include <cstdint>

namespace collision_overlay {

// A feeler query's result and the code that asked.
struct FeelerResult {
    int hit{};
    uint32_t caller{};
};

// After a feeler's query (render::ProbeRay::get_hit_shape): `hit` is its result, `caller` the return address into the
// code that asked. Logs the hit when the feature is on.
void note_feeler_query(const void* feeler, FeelerResult feeler_result);

} // namespace collision_overlay
