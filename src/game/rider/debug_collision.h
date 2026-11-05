#pragma once

// NOT ORIGINAL - [features] debug_collision (thug2-decomp.ini): logging what collision feelers hit. See
// debug_collision.cpp.

#include <cstdint>

namespace render {
struct FeelerResult;
}

namespace collision_overlay {

// After a feeler's query (render::ProbeRay::get_hit_shape, its feeler-query observer): `hit` is its result, `caller`
// the return address into the code that asked. Logs the hit when the feature is on.
void note_feeler_query(const void* feeler, render::FeelerResult feeler_result);
// note_feeler_query made the feelers' observer (render::set_feeler_query_observer), from app/main.cpp.
void install();

} // namespace collision_overlay
