#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace script {
class Struct;
} // namespace script

// The Velocity component (GameObjects::VelocityTrackerModule, type 0x41272956 "Velocity", vtable 0x00646830, 0x1C bytes; made by
// 0x0043D430): moves its object by its velocity each frame - the object's position plus half its velocity times the
// frame's length (as the original does it: half). Not while the object's Suspend component says so (outside a
// network game).
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize 0x0041C330 (shared: the object's Suspend component
// into +0x18), the empty 0x005480A0 / 0x005BAB90, the base's Suspend, 10 0x005144C0 (empty), 11 0x0045D720, 12
// invoke_method_by_name 0x0051ADC0 ("not mine").
namespace objects {

class VelocityTrackerModule final : public ObjectModule {
public:
    VelocityTrackerModule(const VelocityTrackerModule&) = delete;
    VelocityTrackerModule& operator=(const VelocityTrackerModule&) = delete;
    static constexpr uint32_t kType = 0x41272956;

    // 0x0043D430 (registered with the composite object manager): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    VelocityTrackerModule();
    // 0x0043D2B0: the destructor body (the base's).
    ~VelocityTrackerModule() override;
    void reapply_from_data(script::Struct* /*params*/) override {} // (slot 5: nothing)
    // 0x0041C330 (slot 3, the collide-and-die component's Finalize, folded): the object's Suspend component kept.
    void seal() override { suspend_ = entity_->fetch_module(0xCE0CA665); }
    // 0x0043D2F0 (vtable slot 2): the object's velocity from `vel` (0x0C4C809E, asserted).
    void configure_from_data(script::Struct* params) override;
    // 0x0043D340 (vtable slot 1): the object's position (w too) plus half its velocity times the frame's length -
    // unless, outside a network game, the Suspend component's flag (0x00433E90) is set.
    void tick_frame() override;

    ObjectModule* suspend_{}; // +0x18 the object's Suspend component (0xCE0CA665), from Finalize
};

} // namespace objects
