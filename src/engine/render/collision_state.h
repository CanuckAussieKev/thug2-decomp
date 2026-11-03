#pragma once

// NOT ORIGINAL - the collision queries' state shared between their files (the original's globals). The header is the
// render subsystem's own: other subsystems use its functions (set_current_cache).

#include "engine/core/arena_alloc.h"

namespace render {

class HitMeshCache;

// The movable objects' list (its head; made by the start-up initialisers), whether a caller (0x00410930) wants the
// farthest hit rather than the nearest, and the cache queries use when the feeler has none. Made on first use, never
// destroyed (collision_state()).
class CollisionState {
public:
    memory::ListNode& movables() { return movables_; }
    [[nodiscard]] const memory::ListNode& movables() const { return movables_; }
    [[nodiscard]] bool want_farthest() const { return want_farthest_; }
    void set_want_farthest(bool value) { want_farthest_ = value; }
    [[nodiscard]] HitMeshCache* current_cache() const { return current_cache_; }
    void set_current_cache(HitMeshCache* value) { current_cache_ = value; }

private:
    memory::ListNode movables_{};
    bool want_farthest_{};
    HitMeshCache* current_cache_{};
};
CollisionState& collision_state();

} // namespace render
