#pragma once

#include <cstdint>

#include "game/obj/object.h"

namespace Obj {

// A component that shows its object as it is turned: after each of the object's updates its display matrix is its
// orientation (type 0x1BBF844D - the name isn't known; vtable 0x00646464, 0x18 bytes, nothing of its own; made by
// 0x004312A0). Vtable: 0 the scalar deleting destructor; 1 Update; 2 / 8 the empty 0x005BAB90; 3 / 4 / 9 the empty
// 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 10 0x005144C0; 11 the base's member
// function table entry 0x0045D720; 12 CallMemberFunction 0x0051ADC0 (none of its own).
class CDisplayMatrixComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x1BBF844D;

    // 0x004312A0 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00431240: the destructor body (the base's).
    void DestroyBody();
    // 0x00431250 (vtable slot 0)
    CDisplayMatrixComponent* Destroy(unsigned int flags);
    // 0x00431280 (vtable slot 1): the object's display matrix = its orientation.
    void Update();
};
static_assert(sizeof(CDisplayMatrixComponent) == 0x18, "layout must match the original");

} // namespace Obj
