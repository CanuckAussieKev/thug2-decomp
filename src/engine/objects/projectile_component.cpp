// The projectile component (GameObjects::ProjectileComponent): see projectile_component.h.

#include "engine/objects/projectile_component.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <gsl/pointers>

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
#include "game/rider/rider.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

// The local skater's projectile slots (Rider +0x1C0, five of 0x18 bytes: +0 the name, +4 taken, +8 the position).
constexpr int kSlotCount = 5;

// The trigger component's events for a projectile's touch (0x0064B89C: 14, none, none).
const std::array<int, 3>& touch_events() { return kProjectileTouchEvents; }

constexpr float kReach = 12.0f;     // 0x0064546C (also added to the feeler's end w)
constexpr float kStep = 0.8975979f; // 0x00646968 (2pi / 7)
constexpr int kFeelers = 8;
constexpr int kRestFrames = 10;


// One feeler's end x / z: the cosine of the angle as computed (in double), the sine of it rounded to float; z adds
// (sin + float(cos)) * 12 rounded to float first.
void probe_tip(int index, float x, float z, float* last_column, float* end_z)
{
    const double angle = static_cast<double>(index) * kStep;
    const double cosine = std::cos(angle);
    const double sine = std::sin(static_cast<double>(static_cast<float>(angle)));
    const auto side = static_cast<float>((sine + static_cast<float>(cosine)) * kReach);
    *last_column = static_cast<float>((cosine - sine) * kReach + x);
    *end_z = static_cast<float>(z + static_cast<double>(side));
}

} // namespace

// 0x004F3890
gsl::owner<ObjectModule*> ProjectileComponent::fabricate() { return new ProjectileComponent; }

// (the constructor: inlined in fabricate in the original)
ProjectileComponent::ProjectileComponent() { kind_tag_ = kType; }

// 0x004F37B0 (0x004F37D0 the deleting one)
ProjectileComponent::~ProjectileComponent() { core::at(rider_ref_->projectiles(), seat_).active = false; }

// 0x004F3800
void ProjectileComponent::configure_from_data(script::Struct* /*params*/)
{
    auto* skater = core::down_cast<Rider>(skate_calls::get_home_rider());
    rider_ref_ = skater;
    const uint32_t name = entity_->ident();
    seat_ = -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if (name == core::at(skater->projectiles(), i).name) {
            seat_ = i;
            break;
        }
    }
    core::at(skater->projectiles(), seat_).active = true;
}

// 0x004F3860
void ProjectileComponent::seal()
{
    auto* skater = skate_calls::get_home_rider();
    if (skater)
        trigger_ptr_ = core::down_cast<ActivationZoneModule>(skater->fetch_module(ActivationZoneModule::kType));
}

// 0x004F3A90
void ProjectileComponent::tick_frame()
{
    core::at(rider_ref_->projectiles(), seat_).pos = entity_->at_point();
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
    const maths::Vec4 pos = entity_->at_point();
    auto end_w = static_cast<float>(static_cast<double>(pos.w) + kReach);
    for (int i = 0; i < kFeelers; ++i) {
        feeler.line().start = pos;
        feeler.line().end.y = pos.y;
        feeler.line().end.w = end_w;
        probe_tip(i, pos.x, pos.z, &feeler.line().end.x, &feeler.line().end.z);
        if (feeler.get_hit_shape(true, false) && trigger_ptr_->probe_touches_zone(touch_events(), &feeler))
            rest_ = kRestFrames;
    }
    feeler.entity().get_release();
}

} // namespace objects
