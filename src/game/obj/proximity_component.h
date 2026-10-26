#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The proximity component (member type 0xE54DB2EE "Proximity"; our name): runs its enter / exit scripts as the
// skater (the global at 0x0069399C) comes within a capsule round it or leaves it. Only its script member functions
// and its update are in the game - their table (0x007CEBF8, made by the dynamic initialiser 0x00644910: Proximity_Init,
// Proximity_SetActive, Proximity_EnterProximityNotHandled) - nothing constructs one: a leftover.
class CProximityComponent : public CBaseComponent {
public:
    // 0x0042B620 (RET 8): `script` run on the object with a new structure: 0x0AE89B6C = the skater's id, plus its
    // own parameters (+0x30).
    void RunScript(uint32_t script, CObject* skater);
    // 0x0042B700: the skater's head (its position and the line a radius 0x006939A8 up from it, as a capsule of the
    // inner radius - the outer once inside) holding its position: in or out; a change runs the enter / exit script.
    // Inactive: out.
    void Update();

    // --- script member functions (true) ---
    // 0x0042B850 (Proximity_Init): inactive, updated, then InitFromStructure (vtable slot 2), active, not suspended.
    bool Proximity_Init(Script::CStruct* params, Script::CScript* script);
    // 0x0042B590 (Proximity_SetActive): active unless INACTIVE (inactive: +0x1C the game clock plus the unnamed
    // float's seconds, or 0); not suspended.
    bool Proximity_SetActive(Script::CStruct* params, Script::CScript* script);
    // 0x0042B570 (Proximity_EnterProximityNotHandled): counts as out.
    bool Proximity_EnterProximityNotHandled(Script::CStruct* params, Script::CScript* script);

    bool m_active;            // +0x18
    uint8_t pad19[3];
    int m_reactivateTime;     // +0x1C
    float m_innerRadius;      // +0x20
    float m_outerRadius;      // +0x24
    uint32_t m_enterScript;   // +0x28
    uint32_t m_exitScript;    // +0x2C
    Script::CStruct* mp_params; // +0x30
    int m_inside;             // +0x34
};
static_assert(offsetof(CProximityComponent, m_active) == 0x18 && offsetof(CProximityComponent, m_inside) == 0x34,
              "layout must match the original");

} // namespace Obj
