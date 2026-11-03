// The skater's adjust-physics component (adjust_physics.h). From the disassembly of 0x004F8420.

#include "game/rider/adjust_physics.h"

#include <cstdint>
#include <array>

#include "game/park_editing/park_editor.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/shadow_component.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/rider/thaw_bmx.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

// The core physics component's fields read here.
// The skater object's.
// The state component's: the height over the ground the skater stands on.

constexpr uint32_t kUberFrigCurrentHeight = 0x801C9CB6; // uber_frig_current_height (feet)
constexpr uint32_t kUnderTheFloor = 0xAFAA46BA;         // the event sent when lifted out from under a floor (lip state)
constexpr uint32_t kParkEditorTestPlay = 0xD443A2BC;    // the park editor's +0x40 while its park is being played

constexpr float kUpNudge = 0.01f;            // 0x0064544C
constexpr float kFeelerAbove = 8.0f;         // 0x006461D4
constexpr float kFeet = 12.0f;               // 0x0064546C
constexpr float kLeastHeight = 0.001f;       // 0x006461D8
constexpr float kFloorProbeHeight = 3000.0f; // 0x0064B9D0
constexpr float kFloorNear = 36.0f;          // 0x0064B9CC (6 squared)
constexpr float kOne = 1.0f;                 // 0x006453A8
constexpr float kBounce = -0.5f;             // (0xBF000000)
constexpr float kLift = 2.0f;                // 0x00645448
// The velocity's nudge after the uber frig (0x0064B9C8, 0x0064B9C8, 0x0064B9C4, 0x006453A8).
constexpr float kNudgeXy = 0.101f, kNudgeZ = 0.1001f;
// The uber frig's offsets from the frame's start, one a try (the original's function-local static, 0x007CCD60).
constexpr std::array<std::array<float, 4>, 5> kUberFrigOffsets = {{
    {0.0f, 0.0f, 0.0f, 1.0f},  {0.1f, 0.1f, 0.1f, 1.0f}, {-0.2f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -0.2f, 1.0f}, {0.2f, 0.0f, 0.0f, 1.0f},
}};

int state_of(skater::SkaterPhysicsModule* core) { return core->state()->cur_phase(); }
void change_phase(skater::SkaterPhysicsModule* core, int state) { core->set_physics_state(state); }

} // namespace

// 0x004F8420
void RiderPhysicsTweaks::keep_above_ground()
{
    skater::SkaterPhysicsModule* core = core_ptr_;
    GameEntity* object = entity();
    uber_frigged_ = 0;
    maths::Vec4& position = object->at_point();
    const maths::Vec4& frame_start = core->safe_pos();
    if (position.x == frame_start.x && position.y == frame_start.y && position.z == frame_start.z &&
        position.w == frame_start.w)
        return; // (not moved this frame)

    // The ground below: a feeler from just over the skater (8 up) to uber_frig_current_height feet under it.
    maths::Vec4& start = core->probe_start();
    maths::Vec4& end = core->probe_end();
    start = position;
    end = position;
    const maths::Vec4& up = core::at(object->basis_frame().row, 1);
    start.x = static_cast<float>(static_cast<double>(up.x) * kUpNudge + start.x);
    start.y = static_cast<float>(static_cast<double>(up.y) * kUpNudge + start.y);
    start.z = static_cast<float>(static_cast<double>(up.z) * kUpNudge + start.z);
    start.w = static_cast<float>(static_cast<double>(up.w) * kUpNudge + start.w);
    start.y = static_cast<float>(static_cast<double>(start.y) + kFeelerAbove);
    end.y = static_cast<float>(static_cast<double>(end.y) -
                               static_cast<double>(script::fetch_float(kUberFrigCurrentHeight)) * kFeet);
    // Not original: THAW 0x005C2820 - on the bike the feeler starts higher (thaw_bmx_physics.cpp).
    const float raise = skater::thaw_bmx::live_now(core) ? skater::thaw_bmx::stuck_check_raise(core) : 0.0f;
    if (raise != 0.0f)
        start.y += raise;
    auto* physics = static_cast<skater::SkaterPhysicsModule*>(core);
    const park_editing::ParkBuilderMode* park_editor = park_editing::ParkBuilderMode::sole();

    if (!physics->probe_touches_zone(0x10, 0)) {
        // Nothing at all: the uber frig (not in the park editor's play while in states 3 / 4): back where the frame
        // began (a little off it each try), going the other way; out of the air states, into the air.
        int state = state_of(core);
        if (!((state == 4 || state == 3) && park_editor->editor_state() == kParkEditorTestPlay)) {
            uber_frigged_ = 1;
            if (uber_frig_tries_ < 5) {
                const auto& offset = core::at(kUberFrigOffsets, uber_frig_tries_++);
                const maths::Vec4& from = core->safe_pos();
                position = {.x = core::at(offset, 0) + from.x, .y = from.y + core::at(offset, 1), .z = from.z + core::at(offset, 2),
                            .w = from.w + core::at(offset, 3)};
            } else {
                uber_frig_tries_ = 0;
            }
            maths::Vec4& velocity = object->vel();
            velocity = {.x = -velocity.x + kNudgeXy, .y = -velocity.y + kNudgeXy, .z = -velocity.z + kNudgeZ, .w = -velocity.w + kOne};
            state = state_of(core);
            if (state != 0 && state != 1)
                change_phase(core, 1);
        }
        state_ref_->set_height_above_ground(0.0f);
        return;
    }

    // Ground. Standing on a moving object (the skater on the ground or in the air, not skitching (+0x88) nor
    // wallriding (+0x78)) that isn't what the feeler hit: off it, with its velocity.
    objects::RiderPoseState* flags = core->state();
    if (!flags->flag(PoseFlag::kSkitching).on && !flags->flag(PoseFlag::kAcidDrop).on && ride_->ride()) {
        const RideTracker* ride = ride_->ride().get();
        bool leave = true;
        if (ride->entity().get_pull()) {
            const int state = flags->cur_phase();
            if (state == 0 || state == 1) {
                render::ProbeRay& feeler = core->feeler();
                if (feeler.hit_object_alive() && feeler.get_hit_object() == ride->entity().get_pull())
                    leave = false;
            } else {
                leave = false;
            }
            if (leave)
                object->vel() += ride_->ride()->entity().get_pull()->vel();
        }
        if (leave)
            ride_->stop_riding();
    }

    // Kept just over the ground.
    {
        const double over = static_cast<double>(position.y) - core->feeler().contact_pos().y;
        state_ref_->set_height_above_ground(static_cast<float>(over));
        if (over < kLeastHeight)
            position.y = static_cast<float>(static_cast<double>(core->feeler().contact_pos().y) + kLeastHeight);
    }
    shadow_handle_->change_place(&core->feeler().contact_pos());
    shadow_handle_->set_normal(&core->feeler().surface_dir());
    if (state_of(core) != 4)
        get_update_brightness(&core->feeler());

    if (park_editor->editor_state() != kParkEditorTestPlay) {
        // Under the floor: a feeler from far above down to a unit over the skater (faces flagged 0x800 - "under ok" -
        // passed over) hits a floor. Within 6 of it: onto it. Further, moved this frame: back where the frame began
        // at half the speed the other way (a lip: out of it, into the air, 2 up); hardly moved: onto it.
        render::ProbeRay feeler;
        feeler.set_excluded({.ignore = 0x810, .require = 0});
        const maths::Vec4 from = {.x = position.x, .y = static_cast<float>(static_cast<double>(position.y) + kFloorProbeHeight),
                                  .z = position.z, .w = position.w};
        feeler.change_origin(&from);
        maths::Vec4 to = {.x = up.x + position.x, .y = position.y + up.y, .z = position.z + up.z, .w = position.w + up.w};
        // Not original: on the bike as far up as the ground check's start (thaw_bmx_physics.cpp stuck_check_raise).
        if (raise != 0.0f)
            to.y += raise;
        feeler.change_finish(&to);
        if (feeler.get_hit_shape(true, false)) {
            const maths::Vec4 off = {.x = position.x - feeler.contact_pos().x, .y = position.y - feeler.contact_pos().y,
                                     .z = position.z - feeler.contact_pos().z, .w = position.w - feeler.contact_pos().w};
            const double near = static_cast<double>(off.x) * off.x + static_cast<double>(off.y) * off.y +
                                static_cast<double>(off.z) * off.z;
            if (near < kFloorNear) {
                position = feeler.contact_pos();
            } else {
                const maths::Vec4& began = core->safe_pos();
                const maths::Vec4 moved = {.x = position.x - began.x, .y = position.y - began.y, .z = position.z - began.z,
                                           .w = position.w - began.w};
                const double moved_sqr = static_cast<double>(moved.x) * moved.x +
                                         static_cast<double>(moved.y) * moved.y +
                                         static_cast<double>(moved.z) * moved.z;
                position = moved_sqr < kOne ? feeler.contact_pos() : began;
                maths::Vec4& velocity = object->vel();
                velocity = {.x = kBounce * velocity.x, .y = kBounce * velocity.y, .z = kBounce * velocity.z, .w = kBounce * velocity.w};
                if (state_of(core) == 4) {
                    change_phase(core, 1);
                    static_cast<Entity*>(object)->post_own_event(kUnderTheFloor, nullptr);
                    position.y = static_cast<float>(static_cast<double>(position.y) + kLift);
                }
            }
        }
        feeler.entity().get_release();
    }
    uber_frig_tries_ = 0;
    core->safe_pos() = position;
}

} // namespace objects
