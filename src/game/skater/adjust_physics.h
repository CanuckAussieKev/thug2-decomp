#pragma once

// The skater's adjust-physics component (type 0x9F9CC949 "SkaterAdjustPhysics", made by 0x004F80E0): run after the
// core physics each frame, it puts right what the move left wrong - the "uber frig" (no ground at all under the
// skater: back where the frame began, turned round), the skater kept just above the ground it stands on, leaving a
// moving object it no longer stands on, the shadow placed, and the "under the floor" check (a floor right above:
// onto it; further up: back where the frame began, half speed the other way).

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Obj {

class CMovableContactComponent;
class CShadowComponent;
class CSkaterStateComponent;

class CSkaterAdjustPhysicsComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x9F9CC949;

    // 0x004F8420 (vtable slot 1)
    void Update();

    int m_uberFrigTries;                 // +0x18 the uber frig's tries in a row (0-5: its offsets from the frame's start)
    uint8_t m_uberFrigged;               // +0x1C set while the uber frig put the skater back this frame
    uint8_t m_pad1D[3];
    void* mp_core;                       // +0x20 the skater's core physics component
    void* mp_unknown24;                  // +0x24 (0x004F82F0's)
    CShadowComponent* mp_shadow;         // +0x28
    CMovableContactComponent* mp_ride;   // +0x2C
    CSkaterStateComponent* mp_state;     // +0x30
};
static_assert(offsetof(CSkaterAdjustPhysicsComponent, m_uberFrigTries) == 0x18 &&
                  offsetof(CSkaterAdjustPhysicsComponent, m_uberFrigged) == 0x1C &&
                  offsetof(CSkaterAdjustPhysicsComponent, mp_core) == 0x20 &&
                  offsetof(CSkaterAdjustPhysicsComponent, mp_shadow) == 0x28 &&
                  offsetof(CSkaterAdjustPhysicsComponent, mp_ride) == 0x2C &&
                  offsetof(CSkaterAdjustPhysicsComponent, mp_state) == 0x30,
              "layout must match the original");

} // namespace Obj
