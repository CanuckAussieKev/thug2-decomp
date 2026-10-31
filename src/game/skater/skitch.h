#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Maths {
class Vector;
}
namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

// The skitch component (Skitch, type 0x3506CE64, vtable 0x0064C218, 0x20 bytes; made by 0x0051BA10): on a car the
// skater can hold on to - whether skitching is allowed (Obj_AllowSkitching), and the car's hook points (its object
// hook manager's nodes, 0x27443708) placed on the car.
//
// Vtable: 0 Destroy, 1 Update 0x0051B970 (shared: suspends itself), 2 ConfigureFromData, 3 the empty 0x005480A0, 12
// InvokeMethodByName; the rest the base's or empty.
namespace GameObjects {

class CTowRideHandler : public CObjectModule {
public:
    static constexpr uint32_t kType = 0x3506CE64;

    // 0x0051BA10 (registered with the composite object manager): a new one, skitching allowed (0 if out of memory).
    static CObjectModule* __cdecl Create();
    // 0x0051B960: the destructor body (the base's).
    void DestroyBody();
    // 0x0051B980 (vtable slot 0)
    CTowRideHandler* Destroy(unsigned int flags);
    // 0x0051B9B0 (vtable slot 2): the object's hook manager (`params` isn't read).
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x0051B9D0 (vtable slot 12): Obj_AllowSkitching (0xC38CCE3B) - allowed unless the flag "off" is given; true.
    // Other names: 2 (not mine). (Ghidra splits it at 0x0051B9D8, after the first compare.)
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    // 0x0051BA70: the hook point nearest `target` (its squared distance), each point on the car: its node through
    // the object's matrix and position, its height above the car dropped (projected onto the car's plane - the
    // matrix's up row). The point goes to `out` and its index is returned; -1 (`out` untouched) without a hook
    // manager, an object or a node list, and -1 with `out` = (0, 0, 0, 1) without nodes.
    int GetNearestHookPoint(Maths::Vector* out, const Maths::Vector* target);
    // 0x0051BC70: hook point `index` on the car, as GetNearestHookPoint places it, into `out`. False without a hook
    // manager, an object or a node list; true otherwise - also when there's no such point (`out` untouched).
    bool GetHookPoint(Maths::Vector* out, int index);

    bool m_allowed;            // +0x18
    uint8_t pad19[3];
    CObjectModule* m_hooks;   // +0x1C the object's hook manager (0x27443708)
};
static_assert(offsetof(CTowRideHandler, m_allowed) == 0x18 && offsetof(CTowRideHandler, m_hooks) == 0x1C &&
                  sizeof(CTowRideHandler) == 0x20,
              "layout must match the original");

// The components with nothing to update (the state history, skitch, special item, tagging, avoid components...).
struct ComponentWithoutUpdate : CObjectModule {
    // 0x0051B970 (vtable slot 1): suspends itself (vtable +0x1C), so it isn't updated again.
    void Update();
};

} // namespace Obj
