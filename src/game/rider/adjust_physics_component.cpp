// The skater's adjust-physics component (adjust_physics.h): its life, its frame and the checks around Update
// (adjust_physics.cpp). From the disassembly of 0x004F8040-0x004F840F and 0x004F8AA0-0x004F8CC2.

#include "game/rider/adjust_physics.h"

#include <cstdint>
#include <gsl/pointers>

#include "engine/core/dbg.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/walk_hang.h"
#include "platform/game_clock.h"
#include "game/rider/core_physics.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/skater_state.h"
#include "engine/core/down_cast.h"
#include "engine/objects/shadow_component.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kModel = 0x286A8D26, kShadow = 0x8A897DD2,
                   kMovableContact = 0x408D2AA9, kSkaterState = 0x43686585;
constexpr uint32_t kUnderTheFloor = 0xAFAA46BA; // (as Update sends it)

// The core physics component's: the position as the frame began, its state component (+0x18 the physics state,
// +0x60 landed, +0x64 when, +0x88 skitching).

constexpr float kFeelerReach = 30.0f;     // 0x00646240
constexpr float kBrightnessAbove = 12.0f; // 0x0064546C


void change_phase(void* core, int state) { static_cast<skater::SkaterPhysicsModule*>(core)->set_physics_state(state); }

// The debug message's strings as the original passes them ("M:%s:%i: %s\n", the source file, "found to be inside a
// moving object").

} // namespace

// 0x004F80E0
gsl::owner<ObjectModule*> RiderPhysicsTweaks::fabricate() { return new RiderPhysicsTweaks; }

// (the constructor: inlined in fabricate in the original)
RiderPhysicsTweaks::RiderPhysicsTweaks() { kind_tag_ = kType; }

// 0x004F8040 (0x004F8050 the deleting one)
RiderPhysicsTweaks::~RiderPhysicsTweaks() = default;

// 0x004F8080
void RiderPhysicsTweaks::seal()
{
    core_ptr_ = core::down_cast<skater::SkaterPhysicsModule>(entity_->fetch_module(kSkaterCorePhysics));
    mesh_instance_ = core::down_cast<MeshInstanceModule>(entity_->fetch_module(kModel));
    shadow_handle_ = core::down_cast<GroundShadowModule>(entity_->fetch_module(kShadow));
    ride_ = core::down_cast<MovingSurfaceModule>(entity_->fetch_module(kMovableContact));
    state_ref_ = core::down_cast<RiderPoseState>(entity_->fetch_module(kSkaterState));
}

// 0x004F8C40
void RiderPhysicsTweaks::tick_frame()
{
    objects::RiderPoseState* flags = core_ptr_->state_;
    if (flags->cur_phase_ == 0 || entity_->vel_.y < 0.0f) {
        if (flags->flag(PoseFlag::kDescended).on != 1) {
            flags->flag(PoseFlag::kDescended).on = 1;
            flags->flag(PoseFlag::kDescended).changed_at = frame_clock::game_clock_elapsed() + g_clock_offset;
        }
    }
    if (!core_ptr_->state_->flag(PoseFlag::kSkitching).on)
        check_inside_moving_object();
    keep_above_ground();
    entity_->old_pos_ = entity_->at_point_;
}

// 0x004F8AA0
void RiderPhysicsTweaks::check_inside_moving_object()
{
    const maths::Vec4& frame_start = core_ptr_->safe_pos_;
    if (!ride_->find_contact_ahead(&entity_->at_point_, &frame_start))
        return;
    debug::printf("M:%s:%i: %s\n", "mponents\\SkaterAdjustPhysicsComponent.cpp", 0x182,
                  "found to be inside a moving object");
    render::ProbeRay feeler;
    const maths::Vec4& pos = entity_->at_point_;
    const maths::Vec4 start = {.x = pos.x, .y = static_cast<float>(static_cast<double>(pos.y) + kFeelerReach), .z = pos.z, .w = pos.w};
    feeler.change_origin(&start);
    const maths::Vec4 end = {.x = pos.x, .y = static_cast<float>(static_cast<double>(pos.y) - kFeelerReach), .z = pos.z, .w = pos.w};
    feeler.change_finish(&end);
    if (feeler.get_hit_shape(true, false) && feeler.hit_object_alive())
        ride_->ride_on(feeler.get_hit_object());
    if (core_ptr_->state_->cur_phase_ == 4) {
        change_phase(core_ptr_, 1);
        entity_->post_own_event(kUnderTheFloor, nullptr);
    } else if (entity_->at_point_ == frame_start) {
        entity_->vel_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    }
    feeler.entity_.get_release();
}

// 0x004F82F0
void RiderPhysicsTweaks::get_update_brightness(render::ProbeRay* ground)
{
    const bool hit = ground->get_hit_shape(false, false) != 0;
    render::ProbeRay feeler;
    feeler.copy_construct(*ground);
    feeler.set_excluded(0, 0x20);
    feeler.line_.start.y = static_cast<float>(static_cast<double>(feeler.line_.start.y) + kBrightnessAbove);
    if (hit)
        feeler.line_.end = {.x = ground->contact_pos_.x,
                            .y = static_cast<float>(static_cast<double>(ground->contact_pos_.y) - 1.0f),
                            .z = ground->contact_pos_.z, .w = ground->contact_pos_.w};
    else
        feeler.line_.end = feeler.line_.start;
    if (feeler.get_hit_shape(false, false))
        mesh_instance_->apply_brightness(&feeler, true, 1.0f);
    else if (hit)
        mesh_instance_->apply_brightness(ground, true, 1.0f);
    feeler.entity_.get_release();
}

} // namespace objects
