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
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"

namespace objects {

namespace {

extern void** const kVtable;
constexpr uint32_t kVel = 0x0C4C809E;
const float kHalf = 0.5f; // 0x006453BC

// The Suspend component's flag at +0x30 (0x00433E90; not rewritten yet).
bool suspend_flag(ObjectModule* suspend) { return static_cast<DormancyModule*>(suspend)->is_far(); }

} // namespace

// 0x0043D430
ObjectModule* __cdecl VelocityTrackerModule::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(VelocityTrackerModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<VelocityTrackerModule*>(memory);
    component->ObjectModule::make_new();
    component->vtable_ = kVtable;
    component->kind_tag_ = kType;
    return component;
}

// 0x0043D2B0
void VelocityTrackerModule::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x0043D2C0
VelocityTrackerModule* VelocityTrackerModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0043D2F0
void VelocityTrackerModule::configure_from_data(script::Struct* params)
{
    script::VectorValue vel;
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
    float step_x = static_cast<float>(half_x * frame);
    float step_y = static_cast<float>(half_y * frame);
    float step_z = static_cast<float>(half_z * frame);
    maths::Vec4& out = entity_->at_point_;
    out.z = static_cast<float>(static_cast<double>(step_z) + pos.z);
    out.w = static_cast<float>(frame * half_w + pos.w);
    out.x = static_cast<float>(static_cast<double>(step_x) + pos.x);
    out.y = static_cast<float>(static_cast<double>(step_y) + pos.y);
}

} // namespace objects

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {
    &objects::VelocityTrackerModule::teardown,
    &objects::VelocityTrackerModule::tick_frame,
    &objects::VelocityTrackerModule::configure_from_data,
    &objects::ImpactDestructModule::seal,
    stubs::do_nothing,
    stubs::ignore_argument,
    stubs::ignore_arguments2,
    &objects::ObjectModule::hold_off,
    stubs::ignore_argument,
    stubs::do_nothing,
    stubs::ignore_argument,
    static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::ObjectModule::invoke_method_by_name),
    &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace objects
