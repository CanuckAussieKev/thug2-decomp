#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

// The skater's finalize-physics component (SkaterFinalizePhysics, type 0x9B373E58, vtable 0x0064BDEC, 0x20 bytes):
// after the physics each frame, the core physics component's results go to the skater's state component, the object's
// matrices and the two sound components. From the disassembly of 0x00511010-0x005112F8.
//
// Vtable: 0 Destroy, 1 Update, 3 Finalize, 12 the shared invoke_method_by_name 0x0051ADC0; the rest the base's or shared
// empty functions.
namespace skater {
class SkaterPhysicsModule;
} // namespace skater
namespace objects {

class RiderPoseState;

class RiderPhysicsCommit final : public ObjectModule {
public:
    // 0x00511280 (registered by register_component_types): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderPhysicsCommit();
    // 0x00511010: the destructor body (the base's).
    ~RiderPhysicsCommit() override;
    // 0x00511050 (vtable +0x0C): the skater's core physics and state components found.
    void seal() override;
    // 0x00511080 (vtable +0x04): the state's +0xF4 (out of a lip trick straight up: into the air, moving less than 1
    // in x and z; cleared off the air), the physics' two vectors (+0x58, +0x68) copied to the state
    // (+0xF8, +0x108); the object's matrix and the physics' made orthonormal again about up (and the physics' taken
    // from the object's when the object's still isn't), the display matrix from the physics'; the sound components
    // told the terrain and state, the looping sound the speed as a fraction of the top speed.
    void tick_frame() override;

    skater::SkaterPhysicsModule* core_physics_; // +0x18 SkaterCorePhysics (0x5BD63B29)
    RiderPoseState* pose_;                      // +0x1C SkaterState (0x43686585)
};

} // namespace objects
