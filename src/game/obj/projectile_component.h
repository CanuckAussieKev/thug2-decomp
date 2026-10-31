#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/entity.h"

namespace Scripts {
class CStruct;
} // namespace Script

namespace Engine3D {
class CProbeRay;
}

// The projectile component (type 0x7E747547, vtable 0x0064B8A8, 0x28 bytes; made by 0x004F3890): on a thrown
// object (a snowball...), which takes one of the local skater's five projectile slots (CRider +0x1C0: the slot
// named as the object, else the last) and keeps the slot's position up to date; every frame eight short feelers
// around it ask the local skater's trigger component whether they touch a trigger face - one that does sets it off
// (event 14) and the feelers rest for ten frames. From the disassembly of 0x004F37B0-0x004F3AC2.
//
// Vtable: 0 Destroy, 1 Update, 2 ConfigureFromData, 3 Finalize; the rest the base's / shared (4 / 9 the empty
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 the base's member
// function table entry 0x0045D720, 12 0x0051ADC0).
namespace GameObjects {

class CActivationZoneModule;

class CProjectileComponent : public CObjectModule {
public:
    static constexpr uint32_t kType = 0x7E747547;

    // 0x004F3890 (registered by RegisterComponentTypes): a new one (0 if out of memory): the feelers not resting;
    // the rest zero (a cleared allocation).
    static CObjectModule* __cdecl Fabricate();
    // 0x004F37B0: the destructor body: its projectile slot let go (the base's after).
    void DestroyBody();
    // 0x004F37D0 (vtable slot 0)
    CProjectileComponent* Teardown(unsigned int flags);
    // 0x004F3800 (vtable slot 2): the local skater's projectile slot named as the object taken (none: the last one,
    // -1 kept as the slot - the original then marks the byte before the slots' first). `params` isn't read.
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x004F3860 (vtable slot 3): the local skater's trigger component (none without a local skater).
    void Seal();
    // 0x004F3A90 (vtable slot 1): the slot's position set to the object's, then CheckTriggers.
    void TickFrame();
    // 0x004F3910 (inside Ghidra's 0x004F3A90; reached by its jump): resting: one frame less. Otherwise eight
    // feelers from the object, 12 out at 2pi/7 steps (movable objects too), each that hits a trigger face set off on
    // the trigger component (event 14) - any that does rests them for ten frames.
    void CheckTriggers();

    uint8_t* m_riderRef;           // +0x18 the local skater (its projectile slots at +0x1C0, 0x18 bytes each)
    int m_seat;                   // +0x1C its slot (-1: none)
    int m_rest;                   // +0x20 frames left before the feelers run again
    CActivationZoneModule* m_triggerPtr; // +0x24 the local skater's trigger component
};
static_assert(offsetof(CProjectileComponent, m_riderRef) == 0x18 && offsetof(CProjectileComponent, m_seat) == 0x1C &&
                  offsetof(CProjectileComponent, m_rest) == 0x20 && offsetof(CProjectileComponent, m_triggerPtr) == 0x24 &&
                  sizeof(CProjectileComponent) == 0x28,
              "layout must match the original (0x004F3890 allocates 0x28 bytes)");

} // namespace Obj
