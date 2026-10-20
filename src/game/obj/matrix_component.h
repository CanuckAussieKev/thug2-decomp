#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/obj/object.h"

namespace Obj {

// A component holding a matrix that becomes its object's display matrix after each of the object's updates (type
// 0x208353AC, vtable 0x00645F24, 0x58 bytes; made by 0x0041CB20). A cutscene's stand-in hands it the object's display
// matrix (cutscene_update.cpp, where the type is called kSoundComponent). Vtable: 0 the scalar deleting destructor;
// 1 Update; 2 and 7 / 8 / 10 the empty 0x005BAB90; 3 / 4 / 9 the empty 0x005480A0; 5 the shared 0x004E3F90; 12
// CallMemberFunction 0x0051ADC0.
class CMatrixComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x208353AC;

    // 0x0041CB20 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041CAD0: the base's constructor, its vtable and type, the matrix the identity. Returns this.
    CMatrixComponent* Construct();
    // 0x0041CA60: the base's body.
    void DestroyBody();
    // 0x0041CA70 (vtable slot 0)
    CMatrixComponent* Destroy(unsigned int flags);
    // 0x0041CAA0 (vtable slot 1): the object's display matrix = the matrix.
    void Update();
    // 0x0041CAC0: the matrix set.
    void SetMatrix(const Mth::Matrix* matrix);

    Mth::Matrix m_matrix; // +0x18
};
static_assert(offsetof(CMatrixComponent, m_matrix) == 0x18 && sizeof(CMatrixComponent) == 0x58,
              "layout must match the original");

} // namespace Obj
