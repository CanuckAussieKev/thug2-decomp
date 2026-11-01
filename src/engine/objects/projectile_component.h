#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Struct;
} // namespace script

namespace render {
class ProbeRay;
}

// The projectile component (type 0x7E747547, vtable 0x0064B8A8, 0x28 bytes; made by 0x004F3890): on a thrown
// object (a snowball...), which takes one of the local skater's five projectile slots (Rider +0x1C0: the slot
// named as the object, else the last) and keeps the slot's position up to date; every frame eight short feelers
// around it ask the local skater's trigger component whether they touch a trigger face - one that does sets it off
// (event 14) and the feelers rest for ten frames. From the disassembly of 0x004F37B0-0x004F3AC2.
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize; the rest the base's / shared (4 / 9 the empty
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 the base's member
// function table entry 0x0045D720, 12 0x0051ADC0).
namespace objects {

class Rider;

class ActivationZoneModule;

class ProjectileComponent final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x7E747547;

    // 0x004F3890 (registered by register_component_types): a new one (0 if out of memory): the feelers not resting;
    // the rest zero (a cleared allocation).
    static memory::owner<ObjectModule*> __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    ProjectileComponent();
    // 0x004F37B0: the destructor body: its projectile slot let go (the base's after).
    ~ProjectileComponent() override;
    // 0x004F3800 (vtable slot 2): the local skater's projectile slot named as the object taken (none: the last one,
    // -1 kept as the slot - the original then marks the byte before the slots' first). `params` isn't read.
    void configure_from_data(script::Struct* params) override;
    // 0x004F3860 (vtable slot 3): the local skater's trigger component (none without a local skater).
    void seal() override;
    // 0x004F3A90 (vtable slot 1): the slot's position set to the object's, then check_triggers.
    void tick_frame() override;
    // 0x004F3910 (inside Ghidra's 0x004F3A90; reached by its jump): resting: one frame less. Otherwise eight
    // feelers from the object, 12 out at 2pi/7 steps (movable objects too), each that hits a trigger face set off on
    // the trigger component (event 14) - any that does rests them for ten frames.
    void check_triggers();

    Rider* rider_ref_;                  // +0x18 the local skater (its projectile slots)
    int seat_;                          // +0x1C its slot (-1: none)
    int rest_;                          // +0x20 frames left before the feelers run again
    ActivationZoneModule* trigger_ptr_; // +0x24 the local skater's trigger component
};

} // namespace objects
