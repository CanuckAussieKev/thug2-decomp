// The projectile component (GameObjects::ProjectileComponent): see projectile_component.h.

#include "engine/objects/projectile_component.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/trigger_component.h"
#include "game/skate/skate_module_calls.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/trigger_events.h"

namespace objects {

namespace {

// The local skater's projectile slots (Rider +0x1C0, five of 0x18 bytes: +0 the name, +4 taken, +8 the position).
constexpr uint32_t kSlots = 0x1C0;
constexpr uint32_t kSlotSize = 0x18;
constexpr int kSlotCount = 5;

// The trigger component's events for a projectile's touch (0x0064B89C: 14, none, none).
const int* touch_events() { return kProjectileTouchEvents; }

constexpr float kReach = 12.0f;     // 0x0064546C (also added to the feeler's end w)
constexpr float kStep = 0.8975979f; // 0x00646968 (2pi / 7)
constexpr int kFeelers = 8;
constexpr int kRestFrames = 10;

uint8_t& slot_taken(uint8_t* skater, int slot) { return skater[kSlots + 4 + slot * static_cast<int>(kSlotSize)]; }

// One feeler's end x / z, as the original's x87 code: the cosine of the angle as computed (53 bits), the sine of it
// rounded to float; z adds (sin + float(cos)) * 12 rounded to float first.
void probe_tip(int index, float x, float z, float* last_column, float* end_z)
{
    float angle_f;
    float slot;
    float step = kStep;
    float reach = kReach;
    __asm {
        fild index
        fmul step
        fst angle_f
        fcos
        fld angle_f
        fsin
        fxch st(1)
        fst slot
        fsub st(0), st(1)
        fxch st(1)
        fadd slot
        fmul reach
        fstp slot
        fmul reach
        fld z
        fadd slot
        fxch st(1)
        fadd x
        mov eax, last_column
        fstp dword ptr [eax]
        mov eax, end_z
        fstp dword ptr [eax]
    }
}

} // namespace

// 0x004F3890
ObjectModule* __cdecl ProjectileComponent::fabricate() { return new ProjectileComponent; }

// (the constructor: inlined in fabricate in the original)
ProjectileComponent::ProjectileComponent()
{
    kind_tag_ = kType;
    rest_ = 0;
}

// 0x004F37B0 (0x004F37D0 the deleting one)
ProjectileComponent::~ProjectileComponent() { slot_taken(rider_ref_, seat_) = 0; }

// 0x004F3800
void ProjectileComponent::configure_from_data(script::Struct* /*params*/)
{
    auto* skater = reinterpret_cast<uint8_t*>(skate_calls::get_home_rider());
    rider_ref_ = skater;
    uint32_t name = entity_->ident_;
    seat_ = -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if (name == *reinterpret_cast<uint32_t*>(skater + kSlots + i * kSlotSize)) {
            seat_ = i;
            break;
        }
    }
    slot_taken(skater, seat_) = 1;
}

// 0x004F3860
void ProjectileComponent::seal()
{
    auto* skater = skate_calls::get_home_rider();
    if (skater)
        trigger_ptr_ = static_cast<ActivationZoneModule*>(skater->fetch_module(ActivationZoneModule::kType));
}

// 0x004F3A90
void ProjectileComponent::tick_frame()
{
    *reinterpret_cast<maths::Vec4*>(rider_ref_ + kSlots + 8 + seat_ * static_cast<int>(kSlotSize)) = entity_->at_point_;
    check_triggers();
}

// 0x004F3910
void ProjectileComponent::check_triggers()
{
    if (rest_ > 0) {
        --rest_;
        return;
    }
    render::ProbeRay feeler;
    feeler.make_new();
    const maths::Vec4 pos = entity_->at_point_;
    float end_w = static_cast<float>(static_cast<double>(pos.w) + kReach);
    for (int i = 0; i < kFeelers; ++i) {
        feeler.line_.start = pos;
        feeler.line_.end.y = pos.y;
        feeler.line_.end.w = end_w;
        probe_tip(i, pos.x, pos.z, &feeler.line_.end.x, &feeler.line_.end.z);
        if (feeler.get_hit_shape(true, false) && trigger_ptr_->probe_touches_zone(touch_events(), &feeler))
            rest_ = kRestFrames;
    }
    feeler.entity_.get_release();
}

} // namespace objects
