// The Velocity component: see velocity_component.h. From the disassembly of 0x0043D2B0-0x0043D487.

#include "engine/objects/velocity_component.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/collide_and_die.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"

namespace objects {

namespace {

constexpr uint32_t kVel = 0x0C4C809E;
const float kHalf = 0.5f; // 0x006453BC

// The Suspend component's flag at +0x30 (0x00433E90; not rewritten yet).
bool suspend_flag(ObjectModule* suspend) { return static_cast<DormancyModule*>(suspend)->is_far(); }

} // namespace

// 0x0043D430
memory::owner<ObjectModule*> VelocityTrackerModule::fabricate() { return new VelocityTrackerModule; }

// (the constructor: inlined in fabricate in the original)
VelocityTrackerModule::VelocityTrackerModule() { kind_tag_ = kType; }

// 0x0043D2B0 (0x0043D2C0 the deleting one)
VelocityTrackerModule::~VelocityTrackerModule() = default;

// 0x0043D2F0
void VelocityTrackerModule::configure_from_data(script::Struct* params)
{
    script::VectorValue vel{};
    params->fetch_vec(kVel, &vel, 1);
    entity_->vel_.x = vel.x;
    entity_->vel_.y = vel.y;
    entity_->vel_.z = vel.z;
    entity_->vel_.w = vel.w;
}

// 0x0043D340
void VelocityTrackerModule::tick_frame()
{
    if (!g_online_hub->in_net_game() && suspend_flag(suspend_))
        return;
    const maths::Vec4& vel = entity_->vel_;
    // Half the velocity (each rounded), the position as it was.
    float half_x = vel.x * kHalf, half_y = vel.y * kHalf, half_z = vel.z * kHalf, half_w = vel.w * kHalf;
    maths::Vec4 pos = entity_->at_point_;
    double frame = frame_clock::get_slomo_frame_seconds();
    // Each step rounded, then added - but w's step isn't rounded.
    auto step_x = static_cast<float>(half_x * frame);
    auto step_y = static_cast<float>(half_y * frame);
    auto step_z = static_cast<float>(half_z * frame);
    maths::Vec4& out = entity_->at_point_;
    out.z = static_cast<float>(static_cast<double>(step_z) + pos.z);
    out.w = static_cast<float>(frame * half_w + pos.w);
    out.x = static_cast<float>(static_cast<double>(step_x) + pos.x);
    out.y = static_cast<float>(static_cast<double>(step_y) + pos.y);
}

} // namespace objects
