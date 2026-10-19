#pragma once

// NOT ORIGINAL - [features] debug_collision (thug2-decomp.ini): logging what collision feelers hit. See
// debug_collision.cpp.

#include <cstdint>

namespace DebugCollision {

// After a feeler's query (Nx::CFeeler::GetCollision): `hit` is its result, `caller` the return address into the
// code that asked. Logs the hit when the feature is on.
void NoteFeelerQuery(const void* feeler, int hit, uint32_t caller);

} // namespace DebugCollision
