// The skater's core physics component: the ground state's update.

#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "engine/maths/maths.h"
#include "platform/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/ride_tracker.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/collision_events.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/manual_balance.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "game/skate/skate_module.h"
#include "game/rider/balance_trick.h"
#include "game/rider/skater_sound.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "engine/render/coll_obj.h"

namespace skater {

using namespace core_fields;

namespace {
constexpr float kStepSlack = 0.1F;

constexpr uint32_t kGroundGravity = 0x6618A713;    // Physics_Ground_Gravity
constexpr uint32_t kSkitchSpeedMatch = 0xDEF25B34; // skitch_speed_match
constexpr uint32_t kSkitch = 0x3506CE64;           // skitch (the balance trick's kind)
constexpr uint32_t kManual = 0xEF24413B;           // Manual
constexpr uint32_t kNoseManual = 0x0AC90769;       // (another manual kind)
constexpr uint32_t kPerfectSkitch = 0x69A1CE96;    // CHEAT_PERFECT_SKITCH
constexpr uint32_t kPerfectManual = 0xB38341C9;    // CHEAT_PERFECT_MANUAL

constexpr float kDegreesPerRadian = 57.2957764F; // 0x00645468
constexpr float kFullTurn = 360.0F;              // 0x00645EA8
constexpr float kMoving = 0.001F;                // 0x006461D8
constexpr float kSkitchFeelUp = 36.0F;           // 0x0064B9CC
constexpr float kSkitchFeelDown = 200.0F;        // 0x0064BDB0

objects::RiderStoryProgress* get_career() { return modules::Skate::sole()->career().get(); }

// A cheat that keeps a balance trick perfect: its global flag set, the balance's lean and its speed zeroed.
void perfect_cheat(uint32_t cheat, objects::Balance& balance)
{
    if (!get_career()->is_code_on(cheat)) {
        return;
    }
    get_career()->set_global_flag(script::fetch_int_value(cheat));
    balance.set_lean(0.0F);
    balance.set_lean_speed(0.0F);
    online::net_game_state().set_cheat_flag(true);
}

} // namespace

// 0x0050EE70
void SkaterPhysicsModule::do_ground_physics()
{
    for (PoseFlag flag : {PoseFlag::kLeftVert, PoseFlag::kVertAir, PoseFlag::kVertWall, PoseFlag::kAcidDrop,
                          PoseFlag::kFromAcidDrop, PoseFlag::kRecoverInAir, PoseFlag::kNoAcidDrop}) {
        set_flag(this, flag, 0);
    }
    rail_node_ = nullptr;
    last_rail_script_ = 0;
    vert_wallplanted_ = 0;
    transfer_blocked_ = 0;
    flip_spin_started_ = 0;
    late_wallplant_frames_ = 0;

    // The heading (+0x348, degrees round from the facing).
    objects::GameEntity* object = object_of(this);
    const double heading = static_cast<double>(maths::fast_a_cos(core::at(object->basis_frame().row, 2).x)) * kDegreesPerRadian;
    last_heading_ = static_cast<float>(heading);
    if (core::at(object->basis_frame().row, 2).z < 0.0F) {
        last_heading_ = static_cast<float>(kFullTurn - heading);
    }
    if (thaw_bmx::live_now(
            this)) { // (not original: THAW 0x005E4579, the time on vert ground noted - thaw_bmx_physics.cpp)
        thaw_bmx::note_vert_ground(this);
    }
    // Moving: the velocity turned into the ground's plane.
    if (length_of(object->vel()) > kMoving) {
        object->vel().align_to_plane(floor_normal_);
    }
    // Gravity along the ground (+0x360's instead going up while +0x350 is set).
    maths::Vec4 gravity = {.x = 0.0F, .y = tuning_float_lookup(kGroundGravity, object_of(this)), .z = 0.0F, .w = 1.0F};
    if (!(limits_time_ == 0.0F) && entity()->vel().y > 0.0F) {
        gravity = {.x = 0.0F, .y = limits_gravity_, .z = 0.0F, .w = 1.0F};
    }
    gravity.flatten_onto_plane(floor_normal_);
    const bool bike = thaw_bmx::live_now(this); // (not original: THAW's bike branches, thaw_bmx_physics.cpp)
    if (bike) {
        thaw_bmx::get_on_ground(this);
    }
    if (!bike || thaw_bmx::ground_gravity_applies(this, gravity)) {
        const float frame_length = frame_length_;
        maths::Vec4& v = entity()->vel();
        const float gy = gravity.y * frame_length;
        const float gz = gravity.z * frame_length;
        const float gw = gravity.w * frame_length;
        v.x = static_cast<float>((static_cast<double>(gravity.x) * frame_length) + v.x);
        v.y = gy + v.y;
        v.z = gz + v.z;
        v.w = gw + v.w;
    }

    // The balance trick.
    tick_balance_trick(bike);
    collision_events::checkpoint(this, "gravity"); // (not original)
    apply_ground_friction();
    check_side_feelers();
    collision_events::checkpoint(this, "friction-side-feelers"); // (not original)

    // The move this frame - skitching, none of its own: the skitched object's velocity (scaled), 0x00501DB0.
    maths::Vec4 step{};
    {
        const float frame_length = frame_length_;
        const maths::Vec4& v = entity()->vel();
        step = {.x = frame_length * v.x, .y = frame_length * v.y, .z = frame_length * v.z, .w = frame_length * v.w};
    }
    if (bit_mark(this, PoseFlag::kSkitching) != 0U) {
        const objects::RideTracker* tracker = get_tracker(this);
        if (tracker && tracker->entity().get_pull()) {
            void* skitched = tracker->entity().get_pull();
            const float match = script::fetch_float(kSkitchSpeedMatch);
            const maths::Vec4& theirs = static_cast<objects::GameEntity*>(skitched)->vel();
            entity()->vel() = {.x = match * theirs.x, .y = match * theirs.y, .z = match * theirs.z, .w = match * theirs.w};
        }
        step = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        get_skitch();
    }
    // Carried by what the skater rides on (not while skitching - that's the ride).
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity().get_pull() || !tracker->tick_frame(&entity()->at_point())) {
            stop_riding(this);
        } else if (bit_mark(this, PoseFlag::kSkitching) == 0U) {
            objects::GameEntity* skater = object_of(this);
            skater->at_point() += get_tracker(this)->carried();
            entity()->old_pos() = entity()->at_point();
            safe_pos_ = entity()->at_point();
            if (get_tracker(this)->turned()) {
                auto multiply = d3dx::matrix_multiply;
                maths::Mat4& basis = entity()->basis_frame();
                multiply(&basis, &basis, &get_tracker(this)->turn());
                multiply(&display_matrix_, &display_matrix_, &get_tracker(this)->turn());
            }
        }
    }

    move_on_ground(step);
    collision_events::checkpoint(this, "moved"); // (not original)
    if (state_of(this) != kGround) {
        return;
    }

    // Still on the ground.
    if (powerslide_ == 0U) {
        turn_on_ground();
    }
    if (bike && state_of(this) == kGround) {
        thaw_bmx::manual_pitch(this);             // (not original: THAW 0x005E54BA)
    }
    collision_events::checkpoint(this, "turned"); // (not original)
    if (lock_velocity_ == 0U) {
        get_along_facing(&entity()->vel());
    }
    collision_events::checkpoint(this, "along-facing-4FF590"); // (not original)
    if (bit_mark(this, PoseFlag::kSkitching) == 0U) {
        test_lateral_hits();
        lean_push_off_wall();
    }
    collision_events::checkpoint(this, "side-collisions"); // (not original)
    // (not original: THAW 0x005CCDD0 returns at once on the bike: rolling backwards faster
    // than Skater_Flip_Speed (1) the skater is turned round to ride fakie, the bike isn't - it rolls on backwards in
    // THAW's backwards balance, bike_backwards_balance_trick)
    if ((lock_velocity_ == 0U) && !bike) {
        turn_round_if_backwards();
    }
    collision_events::checkpoint(this, "turn-round-4FF920"); // (not original)
    if (jump_released()) {
        start_vert_air();
        set_flag(this, PoseFlag::kLeftVert, 1);
        set_flag(this, PoseFlag::kJumped, 1);
    }
    static_cast<objects::MoveExecutor*>(trick_)->pass_to_trick_objects(floor_feeler_.hit().object_checksum_);
    queued_spin_ = 0.0F;
}

// (Part of 0x0050EE70) The balance trick: none - braking or pushing off; a skitch; a manual.
void SkaterPhysicsModule::tick_balance_trick(bool bike)
{
    crouch_stats_ = 0;
    const uint32_t kind = balance_->trick();
    if (kind == 0) {
        if (bike ? thaw_bmx::should_brake(this) : should_brake()) {
            get_brake();
        } else {
            braking_ = 0;
            if (bike ? thaw_bmx::can_kick(this) && thaw_bmx::may_accelerate(this) : can_push_off()) {
                push_off();
            }
        }
    } else if (kind == kSkitch) {
        balance_->skitch().tick_frame();
        perfect_cheat(kPerfectSkitch, balance_->skitch());
    } else if (kind == kNoseManual || kind == kManual) {
        braking_ = 0;
        if (motoskateboard_ != 0U) {
            crouch_stats_ = 1;
            if (can_push_off()) {
                push_off();
            }
        }
        balance_->manual().tick_frame();
        static_cast<objects::MoveExecutor*>(trick_)->set_trick_objects(true);
        perfect_cheat(kPerfectManual, balance_->manual());
    }
}

// (Part of 0x0050EE70) The move along the ground: the step, then kept to the ground (skitching: a feeler down the
// up for the terrain under the skater). Stopped short of the step (on the ground, the first time): the rest of the
// step once more, along the velocity.
void SkaterPhysicsModule::move_on_ground(maths::Vec4 step)
{
    bool again = false;
    for (;;) {
        maths::Vec4& skater_position = entity()->at_point();
        const maths::Vec4 before = skater_position; // (x, y, z used)
        skater_position.x = step.x + skater_position.x;
        skater_position.y = step.y + skater_position.y;
        skater_position.z = step.z + skater_position.z;
        skater_position.w = step.w + skater_position.w;
        if (bit_mark(this, PoseFlag::kSkitching) == 0U) {
            check_wall_ahead();
        }
        collision_events::checkpoint(this, "wall-ahead-check"); // (not original)
        if (bit_mark(this, PoseFlag::kSkitching) != 0U) {
            // Skitching: a feeler from 36 above to 200 below, along the up, for the ground's terrain.
            const objects::GameEntity* object = object_of(this);
            const maths::Vec4& up = core::at(object->basis_frame().row, 1);
            const maths::Vec4& position = object->at_point();
            const float up_y = up.y * kSkitchFeelUp;
            const float up_z = up.z * kSkitchFeelUp;
            const float up_w = up.w * kSkitchFeelUp;
            maths::Vec4& start = feeler_.line().start; // (the feeler's own line)
            maths::Vec4& end = feeler_.line().end;
            start.z = up_z + position.z;
            start.w = up_w + position.w;
            start.x = static_cast<float>((static_cast<double>(up.x) * kSkitchFeelUp) + position.x);
            start.y = up_y + position.y;
            const float down_y = up.y * kSkitchFeelDown;
            const float down_z = up.z * kSkitchFeelDown;
            const float down_w = up.w * kSkitchFeelDown;
            end.z = position.z - down_z;
            end.w = position.w - down_w;
            end.x = static_cast<float>(position.x - (static_cast<double>(up.x) * kSkitchFeelDown));
            end.y = position.y - down_y;
            if (get_feel(this, render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
                const uint8_t terrain = feeler_.hit().terrain;
                state_->set_surface_kind(terrain);
                sound_->set_surface_type(terrain);
            }
        } else {
            check_for_ground();
        }
        collision_events::checkpoint(this, "ground-check"); // (not original)
        // How far it got against the step.
        const maths::Vec4& now = entity()->at_point();
        const double dx = static_cast<double>(now.x) - before.x;
        const double dy = static_cast<double>(now.y) - before.y;
        const double dz = static_cast<double>(now.z) - before.z;
        const double moved = std::sqrt(((dx * dx) + (dz * dz)) + (dy * dy));
        auto step_length = static_cast<float>(
            std::sqrt(((static_cast<double>(step.z) * step.z) + (static_cast<double>(step.y) * step.y)) +
                      (static_cast<double>(step.x) * step.x)));
        if (again || state_of(this) != kGround || !(moved < static_cast<double>(step_length) - kStepSlack)) {
            return;
        }
        // The rest: along the velocity (x, y the unrounded unit vector, z rounded), the step's length, scaled by
        // what's left of it.
        const maths::Vec4& v = entity()->vel();
        double vx = v.x;
        double vy = v.y;
        float vz = v.z;
        const float vw = v.w;
        const double length = std::sqrt(((static_cast<double>(vz) * vz) + (vy * vy)) + (vx * vx));
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            vx *= inverse;
            vy *= inverse;
            vz = static_cast<float>(vz * inverse);
        }
        again = true;
        auto sx = static_cast<float>(vx * step_length);
        const double sy = vy * step_length;
        const double sz = static_cast<double>(vz) * step_length;
        const float sw = vw * step_length;
        const double left = 1.0 - (moved / step_length);
        auto left_rounded = static_cast<float>(left);
        step.x = static_cast<float>(left * sx);
        step.y = static_cast<float>(sy * left_rounded);
        step.z = static_cast<float>(sz * left_rounded);
        step.w = sw * left_rounded;
    }
}

} // namespace skater
