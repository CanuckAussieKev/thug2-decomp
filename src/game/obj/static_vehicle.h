#pragma once

#include <cstdint>

#include "game/obj/object.h"

namespace Obj {

// The static vehicle component (type 0x9649D4F9, "staticvehicle"; vtable 0x00646560, 0x58 bytes - nothing of its own
// is used; made by 0x00433690): a parked car that doesn't drive - when its object is finalised, its skeleton's bones
// are posed from its model's hierarchy: the body where the model's root is, the wheels where its next four objects
// are (in the game's axes: x and the model's y negated, y and z swapped).
//
// Vtable: 0 the scalar deleting destructor; 1 Update 0x0051B970 (shared, empty); 2 / 8 the empty 0x005BAB90; 3
// Finalize; 4 / 9 the empty 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 10 0x005144C0;
// 11 the base's member function table entry 0x0045D720; 12 CallMemberFunction 0x0051ADC0 (none of its own).
class CStaticVehicleComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x9649D4F9;

    // 0x00433690 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00433650: the destructor body (the base's).
    void DestroyBody();
    // 0x00433660 (vtable slot 0)
    CStaticVehicleComponent* Destroy(unsigned int flags);
    // 0x004336F0 (vtable slot 3): the skeleton component's bone matrices: bone 0 all zero; bone 1 the body (rows
    // (1, 0, 0), (0, 0, -1), (0, 1, 0)) at the model's root; each later bone a wheel (rows (-1, 0, 0), (0, 0, 1),
    // (0, 1, 0)) at the next of the hierarchy's objects 1-4.
    void Finalize();

    uint8_t m_unused18[0x40];
};
static_assert(sizeof(CStaticVehicleComponent) == 0x58, "layout must match the original");

} // namespace Obj
