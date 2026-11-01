#pragma once

// What a point riding on a moving object (a skater on a moving platform or vehicle) is carried by each frame.

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"

namespace objects {

// Follows an object (0xB0 bytes; our name): its position and orientation when last updated, and how far a point on
// it was carried by the last update - the object's move, plus the point turned about the object's old position
// when the object turned.
struct RideTracker {
    // 0x00416E70: following `object` (not null): its orientation and position now, nothing carried yet (0, 0, 0,
    // 1), not turned.
    explicit RideTracker(GameEntity* object);
    // 0x00416F00: the object's move since the last update into carried_ (w included), plus - when its
    // orientation changed - how `point` turns with it about its old position (turn_: the orientation change
    // without translation; turned_ set). Then the position and orientation kept. False (nothing done) when the
    // object is gone.
    bool tick_frame(const maths::Vec4* point);

    SharedRef<GameEntity> entity_; // +0x00
    maths::Mat4 previous_xform_{}; // +0x0C
    maths::Vec4 last_pos_{};       // +0x4C
    maths::Vec4 carried_{};        // +0x5C
    maths::Mat4 turn_{};           // +0x6C
    bool turned_;                  // +0xAC
};

} // namespace objects
