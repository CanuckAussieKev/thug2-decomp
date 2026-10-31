#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/entity.h"

// The skater's finalize-physics component (SkaterFinalizePhysics, type 0x9B373E58, vtable 0x0064BDEC, 0x20 bytes):
// after the physics each frame, the core physics component's results go to the skater's state component, the object's
// matrices and the two sound components. From the disassembly of 0x00511010-0x005112F8.
//
// Vtable: 0 Destroy, 1 Update, 3 Finalize, 12 the shared InvokeMethodByName 0x0051ADC0; the rest the base's or shared
// empty functions.
namespace GameObjects {

class CRiderPhysicsCommit : public CObjectModule {
public:
    // 0x00511280 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x00511010: the destructor body (the base's).
    void DestroyBody();
    // 0x00511020 (vtable slot 0)
    CRiderPhysicsCommit* Teardown(unsigned int flags);
    // 0x00511050 (vtable +0x0C): the skater's core physics and state components found.
    void Seal();
    // 0x00511080 (vtable +0x04): the state's +0xF4 (out of a lip trick straight up: into the air, moving less than 1
    // in x and z; cleared off the air), the physics' two vectors (+0x58, +0x68) copied to the state
    // (+0xF8, +0x108); the object's matrix and the physics' made orthonormal again about up (and the physics' taken
    // from the object's when the object's still isn't), the display matrix from the physics'; the sound components
    // told the terrain and state, the looping sound the speed as a fraction of the top speed.
    void TickFrame();

    uint8_t* m_corePhysics; // +0x18 SkaterCorePhysics (0x5BD63B29)
    uint8_t* m_curPhase;       // +0x1C SkaterState (0x43686585)
};
static_assert(offsetof(CRiderPhysicsCommit, m_corePhysics) == 0x18 &&
                  offsetof(CRiderPhysicsCommit, m_curPhase) == 0x1C &&
                  sizeof(CRiderPhysicsCommit) == 0x20,
              "layout must match the original (0x00511280 allocates 0x20 bytes)");

} // namespace Obj
