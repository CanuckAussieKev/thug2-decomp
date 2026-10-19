#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The CameraLookAround component (type 0x81C7122D, vtable 0x0064B28C, 0x44 bytes; made by 0x004E42E0): the
// camera's look-around angles from the pad's right stick (heading and tilt eased toward the stick each frame), or
// turned over a time to a heading and tilt a script asks for.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize, 5 0x004E3F90 (the base
// classes' "refresh from a structure": InitFromStructure again), the base's Suspend and CallMemberFunction by
// entry, 12 CallMemberFunction.
class CCameraLookAroundComponent : public CBaseComponent {
public:
    // 0x004E42E0: a new one (0 if out of memory): the base's constructor, its vtable and type.
    static CBaseComponent* __cdecl Create();
    // 0x004E3F60: the destructor body (the base's).
    void DestroyBody();
    // 0x004E4010 (vtable slot 0)
    CCameraLookAroundComponent* Destroy(unsigned int flags);
    // 0x004E3F70 (slot 2): looking ahead, not locked, nothing in progress, the factor 1. `params` isn't read.
    void InitFromStructure(Script::CStruct* params);
    // 0x004E4040 (slot 3): the object's Input component.
    void Finalize();
    // 0x004E4060 (slot 1): the pad (the second one's state when the global 0x702247A5 is set): unless the
    // global 0xF3E055E1 is set, its button at +0x1F0 runs 0x60871393 (for the object 0x967C138C) or 0xA1B1146D;
    // the button at +0x130 locks / unlocks the angles. Unlocked and not turning: the tilt eased toward the stick's
    // +0x214 (x 0.7 below 0, else x 0.5; then x -1.4) and the heading toward its +0x210 x 3, at 3.75 x their
    // distance a second (set outright within 0.001). Turning: by the rates until the time is up, then exactly to
    // the targets.
    void Update();
    // 0x004E4220 (slot 12): 0xB8F23899 turns to heading=, tilt= over time= (floats, asserted) with the factor
    // 0x48D4868B (1 by default); 0xE3B327C0 stops turning. 2 for anything else.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004E3FA0 (not a Ghidra function): turn to `heading` and `tilt` over `time` (straight away without one).
    void TurnTo(float heading, float tilt, float time, float factor);
    // 0x004E4000: lock (or unlock) the angles where they are.
    void SetLocked(bool locked);

    bool m_locked;      // +0x18
    bool m_turning;     // +0x19
    uint8_t pad1A[2];
    float m_factor;     // +0x1C
    float m_tilt;       // +0x20
    float m_heading;    // +0x24
    uint32_t unknown28;
    float m_targetTilt;     // +0x2C
    float m_targetHeading;  // +0x30
    float m_tiltRate;       // +0x34 per second
    float m_headingRate;    // +0x38
    float m_timeLeft;       // +0x3C
    uint8_t* mp_input;      // +0x40 the Input component
};
static_assert(offsetof(CCameraLookAroundComponent, m_locked) == 0x18 &&
                  offsetof(CCameraLookAroundComponent, m_factor) == 0x1C &&
                  offsetof(CCameraLookAroundComponent, m_targetTilt) == 0x2C &&
                  offsetof(CCameraLookAroundComponent, mp_input) == 0x40 && sizeof(CCameraLookAroundComponent) == 0x44,
              "layout must match the original");

} // namespace Obj
