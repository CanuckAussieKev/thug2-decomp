#pragma once

#include <cstddef>
#include <cstdint>

#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace script {
class Struct;
} // namespace script

namespace objects {

class DormancyModule;

// The collide-and-die component (type 0x6259B52B "collideanddie", vtable 0x00645EB4, 0x54 bytes; made by 0x0041C370):
// a projectile's end - each update a collision feeler from where its object was (24 up) to where it is (24 up, plus
// `radius` along its velocity); on a hit its projectile collision component (0x7767D6D7) is told, death_script runs
// (pos = the hit point, vel, scale) and the object dies a second later. It dies anyway ten seconds after it was made.
// Suspended (unless in a network game), nothing happens.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize; the rest the base's / shared (4
// / 9 0x005480A0, 5 / 8 0x005BAB90, 6 0x0048DCD0, 7 0x0045D760, 10 0x005144C0, 11 0x0045D720, 12 0x0051ADC0).
class ImpactDestructModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x6259B52B;

    // 0x0041C370 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x0041C210: its type; no radius, scale or script, made now (the game clock),
    // not dying, the first update to come.
    ImpactDestructModule();
    // 0x0041C1F0: the base's body.
    ~ImpactDestructModule() override;
    void reapply_from_data(script::Struct* /*params*/) override {} // (slot 5: nothing)
    // 0x0041C2B0 (vtable slot 2): radius, scale, death_script (each asserted), vel (asserted; default (0, 0, 1),
    // normalised).
    void configure_from_data(script::Struct* params) override;
    // 0x0041C330 (vtable slot 3): the object's suspend component.
    void seal() override;
    // 0x0041C3D0 (vtable slot 1): see the class. The times are the game clock's, compared unsigned.
    void tick_frame() override;

private:
    DormancyModule* suspend_{};  // +0x18 the suspend component
    float radius_{0.0F};         // +0x1C radius
    float size_factor_{0.0F};    // +0x20 scale (given to death_script)
    uint32_t made_time_;         // +0x24
    uint32_t on_collide_script_{0}; // +0x28 death_script
    uint32_t death_time_{};      // +0x2C (a second after the hit)
    bool fading_out_{false};     // +0x30
    bool first_update_{true};    // +0x31
    maths::Vec4 vel_{};          // +0x34 vel (normalised)
    maths::Vec4 last_pos_{};     // +0x44 the object's position at the last update
};

} // namespace objects
