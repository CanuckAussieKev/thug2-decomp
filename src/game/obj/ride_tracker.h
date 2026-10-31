#pragma once

// What a point riding on a moving object (a skater on a moving platform or vehicle) is carried by each frame.

#include <cstddef>
#include <cstdint>

#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/obj/entity.h"
#include "game/obj/smtptr.h"

namespace GameObjects {

// Follows an object (0xB0 bytes; our name): its position and orientation when last updated, and how far a point on
// it was carried by the last update - the object's move, plus the point turned about the object's old position
// when the object turned.
struct CRideTracker {
    // 0x00416E70: following `object` (not null): its orientation and position now, nothing carried yet (0, 0, 0,
    // 1), not turned. Returns this.
    CRideTracker* MakeNew(CGameEntity* object);
    // 0x00416F00: the object's move since the last update into m_carried (w included), plus - when its
    // orientation changed - how `point` turns with it about its old position (m_turn: the orientation change
    // without translation; m_turned set). Then the position and orientation kept. False (nothing done) when the
    // object is gone.
    bool TickFrame(const Maths::Vec4* point);

    CSharedRef<CGameEntity> m_entity;  // +0x00
    Maths::Mat4 m_lastMatrix;            // +0x0C
    Maths::Vec4 m_lastPos;               // +0x4C
    Maths::Vec4 m_carried;               // +0x5C
    Maths::Mat4 m_turn;                  // +0x6C
    bool m_turned;                       // +0xAC
    uint8_t m_padAD[3];
};
static_assert(offsetof(CRideTracker, m_lastMatrix) == 0x0C && offsetof(CRideTracker, m_carried) == 0x5C &&
                  offsetof(CRideTracker, m_turned) == 0xAC && sizeof(CRideTracker) == 0xB0,
              "layout must match the original");

} // namespace Obj
