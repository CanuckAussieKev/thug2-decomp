#pragma once

// NOT ORIGINAL - [features] debug_collision (thug2-decomp.ini): logging what collision feelers hit. See
// debug_collision.cpp.

#include <cstdint>

namespace CollisionOverlay {

// After a feeler's query (Engine3D::CProbeRay::GetHitShape): `hit` is its result, `caller` the return address into the
// code that asked. Logs the hit when the feature is on.
void NoteFeelerQuery(const void* feeler, int hit, uint32_t caller);

} // namespace CollisionOverlay
