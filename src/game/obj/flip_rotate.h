#pragma once

#include <cstddef>
#include <cstdint>

namespace Script {
class CStruct;
}

namespace Obj {

// The "FlipRotate" component (made by the component factory 0x00481FB0 for the type checksum 0xDF5C091A): an
// object's flipped / rotated state. Its base, the component (0x00481710 constructs it, vtable 0x0064877C), has
// no source yet. Vtable 0x0064B1D4: the scalar deleting destructor, Update (0x004E2D20, no source yet),
// 0x005BAB90, 0x0051ADC0, InitFromStructure, the empty 0x005480A0.
class CFlipRotateComponent {
public:
    // 0x004E2C50: the component of `object`, neither flipped nor rotated. Returns this.
    CFlipRotateComponent* Construct(void* object);
    // 0x004E2C80
    void DestroyBody();
    // 0x004E2CF0 (vtable slot 0)
    CFlipRotateComponent* Destroy(unsigned int flags);
    // 0x004E2C90 (vtable slot 4): the base's parameters, then "flipped" and "rotated" (integers: non-zero = on;
    // each left as it was when not given).
    void InitFromStructure(Script::CStruct* params);

    void** m_vtable;
    void* mp_object;        // +0x04: the object it belongs to
    uint32_t m_base08[3];   // +0x08..+0x10: the base component's (+0x10: a checksum from its parameters)
    bool m_flipped;         // +0x14
    bool m_rotated;         // +0x15
    uint8_t pad16[2];
};
static_assert(offsetof(CFlipRotateComponent, m_flipped) == 0x14);
static_assert(sizeof(CFlipRotateComponent) == 0x18);

} // namespace Obj
