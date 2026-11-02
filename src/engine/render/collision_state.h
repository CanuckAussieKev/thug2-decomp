#pragma once

// NOT ORIGINAL - the collision queries' state shared between their files (the original's globals). The header is the
// render subsystem's own: other subsystems use its functions (set_current_cache).

#include "engine/core/arena_alloc.h"

namespace render {

class HitMeshCache;

// The movable objects' list (its head; made by the start-up initialisers), whether a caller (0x00410930) wants the
// farthest hit rather than the nearest, and the cache queries use when the feeler has none. Made on first use, never
// destroyed (collision_state()).
struct CollisionState {
    memory::ListNode movables_{};
    bool want_farthest_{};
    HitMeshCache* current_cache_{};
};
CollisionState& collision_state();

} // namespace render
