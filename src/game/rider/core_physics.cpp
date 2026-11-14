// The skater's core physics component, with PartyMod's PS2-style controls built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/rider/core_physics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "base/core/checksums.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/sys/stopwatch.h"
#include "engine/sys/tick_rate.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/entity.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/trigger_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/collision_events.h"
#include "game/rider/controls.h"
#include "game/rider/rider.h"
#include "game/rider/skater_sound.h"
#include "game/rider/skater_state.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "engine/sys/game_clock.h"
#include "platform/run_log.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/physics_control.h"
#include "engine/objects/input_component.h"
#include "game/rider/rotate_component.h"
#include "base/core/ints.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/render/coll_obj.h"

namespace skater {

namespace {
constexpr double kRepeatLogSeconds = 0.5;           // a repeated script's log line is held back this long
constexpr float kMinLengthSq = 0.001F;
constexpr float kProbeDownLength = 200.0F;
constexpr float kMinAlong = 0.001F;
constexpr float kMinTravel = 2.0F;
constexpr float kMinTurnCos = 0.99F;
constexpr float kDropVelocityLength = 0.1F;
constexpr float kStandProbeOffset = 23.0F;
constexpr float kWallProbeReach = 30.0F; // how far along the wall the probe line is pushed each way
constexpr float kStepMin = 0.5F;
constexpr float kStepShrink = 0.5F;
constexpr int kLowerTries = 10;
constexpr float kLowerStep = 3.0F;
constexpr float kNormalTolerance = 0.02F;
constexpr int kPhaseWallplant = 5;
constexpr int kRecentAirMs = 0xFA;
constexpr float kTinyVelocityClamp = 10.0F;
constexpr uint32_t kSkaterOffEdge = 0xD96F01F1; // SkaterOffEdge
constexpr uint32_t kGroundGone = 0x3B1001B6; // GroundGone
constexpr uint32_t kSkaterVertAllowBreakTime = 0x26495947; // Skater_Vert_Allow_break_Time
constexpr uint32_t kPhysicsAcidDropMinLandSpeed = 0x59484878; // Physics_Acid_Drop_Min_Land_Speed
constexpr uint32_t kLandedName = 0x532B16EF; // Landed

// The input's buttons (offsets into the pad).

// 0x00580400 / 0x00580440: a physics value (the skater's own tuning structure for `object`, else the global
// script value).
float tuning_float_lookup(uint32_t name, objects::GameEntity* object)
{
    return objects::read_tuning_float(checksums::Name{name}, 1, object);
}
int physics_integer(uint32_t name, objects::GameEntity* object)
{
    return objects::read_tuning_int(checksums::Name{name}, 1, object);
}

// The game's clock (0x0044ECE0, plus the offset at 0x00696D60).
int get_now()
{
    const int time = frame_clock::game_clock_elapsed();
    return time + objects::clock_offset();
}

using objects::PoseFlag;

// A timed flag: a bool and, after it, the time it last changed.
void set_timed_flag(objects::RiderPoseState* flags, objects::PoseFlag flag, uint8_t value)
{
    flags->set_flag(flag, value);
}

// A button's times: since it was last pressed (0x0046B920) / released (0x0046B900).
int time_since_press(objects::ControlButton* button) { return button->get_held_millis(); }
int time_since_release(objects::ControlButton* button) { return button->read_let_go_time(); }

void stop_riding(void* ride) { static_cast<objects::MovingSurfaceModule*>(ride)->stop_riding(); }

// The two triggers: both held - or, under PartyMod's PS2 controls (its JZ -> JNZ at each check), either.
bool triggers_held(const objects::PadInputModule* input)
{
    if (ps2_controls()) {
        return (input->pad_primary().shoulder_left_b().held_down() != 0U) ||
               (input->pad_primary().shoulder_right_b().held_down() != 0U);
    }
    return (input->pad_primary().shoulder_left_b().held_down() != 0U) &&
           (input->pad_primary().shoulder_right_b().held_down() != 0U);
}

// velocity += direction * scale, the x component's product unrounded (the original keeps it on the x87 stack).
void stack_on(maths::Vec4& velocity, const maths::Vec4& direction, float scale)
{
    const float y = direction.y * scale;
    const float z = direction.z * scale;
    const float w = direction.w * scale;
    velocity.x = static_cast<float>(velocity.x + (static_cast<double>(direction.x) * scale));
    velocity.y += y;
    velocity.z += z;
    velocity.w += w;
}

} // namespace

// 0x004FCBA0
int SkaterPhysicsModule::lip_push_off(script::Struct* /*params*/, script::Script* /*script*/)
{
    objects::GameEntity* object = entity();
    const maths::Vec4& facing = core::at(object->basis_frame().row, 2);
    maths::Vec4 forward = {.x = facing.x, .y = 0.0F, .z = facing.z, .w = facing.w};
    const double length =
        std::sqrt((static_cast<double>(forward.z) * forward.z) + (static_cast<double>(forward.x) * forward.x));
    if (length > 0.0) {
        const double scale = 1.0 / length; // (w isn't scaled; y stays 0)
        forward.x = static_cast<float>(forward.x * scale);
        forward.z = static_cast<float>(forward.z * scale);
    }
    const maths::Vec4 side = {.x = forward.z, .y = 0.0F, .z = -forward.x, .w = 0.0F};

    objects::PadInputModule* input = input_;
    if (input->pad_primary().right_side().held_down() != 0U) {
        return 0;
    }
    // (The original: both triggers. PartyMod's JZ -> JNZ at 0x004FCC59 makes it either.)
    if (triggers_held(input)) {
        const float scale = tuning_float_lookup(0x1F96C224, object);
        stack_on(entity()->vel(), forward, scale);
        return 1;
    }
    if (input->pad_primary().left_side().held_down() != 0U) {
        const int wait = physics_integer(0x3080E9E7, object);
        if (time_since_press(&input->pad_primary().left_side()) > wait) {
            const float scale = tuning_float_lookup(0xDC818B7E, object);
            stack_on(entity()->vel(), forward, scale);
            return 1;
        }
    }
    if (input->pad_primary().up_dir().held_down() != 0U) {
        const int wait = physics_integer(0x9B25142A, object);
        if (time_since_press(&input->pad_primary().up_dir()) > wait) {
            const float scale = tuning_float_lookup(0x1AB3809F, object);
            maths::Vec4& velocity = entity()->vel();
            // velocity -= side * scale (x's product unrounded, as above)
            const float y = scale * 0.0F;
            const float z = -forward.x * scale;
            velocity.x = static_cast<float>(velocity.x - (static_cast<double>(forward.z) * scale));
            velocity.y -= y;
            velocity.z -= z;
            velocity.w -= y;
            return 1;
        }
    }
    if (input->pad_primary().btn_down().held_down() != 0U) {
        const int wait = physics_integer(0x9B25142A, object);
        if (time_since_press(&input->pad_primary().btn_down()) > wait) {
            const float scale = tuning_float_lookup(0x1AB3809F, object);
            entity()->vel() += side * scale;
            return 1;
        }
    }
    return 0;
}

namespace {

// Not original - [features] debug_rail_speed=1: what the rail speed-up loops do (THAW's RailAccelerate2, which
// the THAW ports carry, and the level editor's ed_RailSpeedLoop: while the skater grinds and climbs, SetSpeed
// with the speed * (1 + rate) each frame). A loop's first boost (a new loop, or one after a pause of half a
// second) and then, once a second, the boosts in it: how many, the speed (game units a second - the length of the
// velocity, as GetSpeed reads it) at the first and at the last, the height. (Only boosts are seen: a loop on a rail
// that doesn't climb boosts nothing and logs nothing.)
struct RailBoostLog {
    script::Script* script = nullptr;
    std::chrono::steady_clock::time_point last, window_start_;
    int boosts = 0;
    float speed_from_ = 0.0F, height_from_ = 0.0F;
};

void log_rail_boost(script::Script* script, float before, float after, float height)
{
    static const bool kOn = config_int("features", "debug_rail_speed", 0) != 0;
    if (!kOn || !script) {
        return;
    }
    static const auto kLoops = std::to_array<uint32_t>({checksums::hash_from_text("RailAccelerate2"),
                                      checksums::hash_from_text("ed_RailSpeedLoop")});
    if (script->script_title() != core::at(kLoops, 0) && script->script_title() != core::at(kLoops, 1)) {
        return;
    }
    static RailBoostLog log;
    auto now = std::chrono::steady_clock::now();
    auto seconds = [](std::chrono::steady_clock::duration duration) {
        return std::chrono::duration<double>(duration).count();
    };
    if (script != log.script || seconds(now - log.last) > kRepeatLogSeconds) {
        float rate = 0.0F;
        script->args()->fetch_float(checksums::hash_from_text("rate"), &rate, 0);
        get_log("rail speed-up: %s boosting (rate %.3f): speed %.0f -> %.0f, height %.0f",
                script->script_title() == core::at(kLoops, 0) ? "RailAccelerate2" : "ed_RailSpeedLoop", rate, before, after,
                height);
        log.script = script;
        log.window_start_ = now;
        log.boosts = 0;
        log.speed_from_ = before;
        log.height_from_ = height;
    } else {
        ++log.boosts;
        const double window = seconds(now - log.window_start_);
        if (window >= 1.0) {
            // (the speed the skater had before each boost - what the rail physics and the speed limit left of the
            // last one)
            get_log("rail speed-up: %.2f s, %d boosts: speed %.0f -> %.0f (%+.0f a second; this boost to %.0f), height "
                    "%.0f -> %.0f",
                    window, log.boosts, log.speed_from_, before, (before - log.speed_from_) / window, after,
                    log.height_from_, height);
            log.window_start_ = now;
            log.boosts = 0;
            log.speed_from_ = before;
            log.height_from_ = height;
        }
    }
    log.last = now;
}

} // namespace

// 0x004FCE90
int SkaterPhysicsModule::set_speed(script::Struct* params, script::Script* script)
{
    float speed = 0.0F;
    params->fetch_float(0U, &speed, 0); // (unnamed)
    objects::GameEntity* object = entity();
    maths::Vec4& velocity = object->vel();
    const double length_sqr = (static_cast<double>(velocity.x) * velocity.x) +
                              (static_cast<double>(velocity.y) * velocity.y) +
                              (static_cast<double>(velocity.z) * velocity.z);
    auto before = static_cast<float>(std::sqrt(length_sqr));
    if (length_sqr < static_cast<double>(kMinLengthSq)) {                             // (not taken for a NaN)
        std::memcpy(&velocity, &core::at(object->basis_frame().row, 2), sizeof(velocity)); // (moved, not loaded as floats)
    } else {
        const double scale = 1.0 / std::sqrt(length_sqr);
        velocity.x = static_cast<float>(scale * velocity.x);
        velocity.y = static_cast<float>(scale * velocity.y);
        velocity.z = static_cast<float>(scale * velocity.z);
        velocity.w = static_cast<float>(scale * velocity.w);
    }
    velocity.x = speed * velocity.x;
    velocity.y = speed * velocity.y;
    velocity.z = speed * velocity.z;
    velocity.w = speed * velocity.w;
    log_rail_boost(script, before, speed, object->at_point().y);
    return 1;
}

// 0x00509BD0
void SkaterPhysicsModule::check_for_ground()
{
    objects::GameEntity* object = entity();
    objects::PadInputModule* input = input_;
    // Not original: THAW 0x005DF050 - on the bike (out of a manual) the two wheels instead (thaw_bmx_physics.cpp).
    const bool bike = thaw_bmx::live_now(this);
    const thaw_bmx::WheelGround wheels =
        bike ? thaw_bmx::wheel_ground_check(this) : thaw_bmx::WheelGround::kWheelsNotUsed;
    if (wheels == thaw_bmx::WheelGround::kWheelsOnGround || wheels == thaw_bmx::WheelGround::kWheelsOnGroundNoHit) {
        ground_under_wheels(wheels == thaw_bmx::WheelGround::kWheelsOnGround);
        return;
    }
    if (wheels == thaw_bmx::WheelGround::kWheelsNotUsed) {
        // The feeler: from above the skater (up * a physics value) to 200 below, along its up axis.
        const float above = tuning_float_lookup(0xE4D79235, object);
        maths::Vec4& start = feelers_.probe_start;
        maths::Vec4& end = feelers_.probe_end;
        start = core::at(object->basis_frame().row, 1) * above;
        end = core::at(object->basis_frame().row, 1) * -kProbeDownLength;
        start += object->at_point();
        end += object->at_point();

        if (feelers_.probe_touches_zone(render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
            const maths::Vec4& position = object->at_point();
            const maths::Vec4& point = feelers_.feeler.contact_pos();
            const maths::Vec4& up = core::at(object->basis_frame().row, 1);
            const double dx = static_cast<double>(position.x) - point.x;
            const double dy = static_cast<double>(position.y) - point.y;
            const double dz = static_cast<double>(position.z) - point.z;
            auto distance = static_cast<float>(std::sqrt((dx * dx) + (dz * dz) + (dy * dy)));
            auto along =
                static_cast<float>((dz * up.z) + (dy * up.y) + (dx * up.x)); // (how far above the hit, along up)

            if (feelers_.hit_skatable == 0U) {
                // Not ground: if the skater is (nearly) at or below it, move it out by the hit's normal; then leave.
                if (along < kMinAlong) {
                    maths::Vec4& skater_position = object->at_point();
                    const maths::Vec4& normal = feelers_.feeler.surface_dir();
                    const float x = point.x + normal.x;
                    const float y = point.y + normal.y;
                    const float z = point.z + normal.z;
                    const float w = point.w + normal.w;
                    skater_position.z = z;
                    skater_position.w = w;
                    skater_position.x = x;
                    skater_position.y = y;
                }
                collision_events::note_path(this, collision_events::RouteWay::kGroundUnskatable,
                                            object->vel()); // (not original)
            } else {
                // Ground. The facing turned into the hit's plane and into the ground's plane: a hit at too steep
                // an angle to the current ground (cos of a physics angle - one value with button +0x20 held,
                // another without), or too far away for how fast the skater moves, isn't kept to.
                if (keep_to_ground({.distance = distance, .along = along}, bike)) {
                    return;
                }
            }
        } else {
            collision_events::note_path(this, collision_events::RouteWay::kNoGround,
                                        object->vel()); // (not original)
        }
    } // (not original: wheels == kWheelsNotUsed)

    // Off the ground.
    set_physics_state(1);
    collision_events::checkpoint(this, "set-air-4FDED0"); // (not original)
    static_cast<objects::Entity*>(entity())->announce(kSkaterOffEdge, nullptr, 0.0F);
    collision_events::checkpoint(this, "SkaterOffEdge-event"); // (not original)
    static_cast<objects::Entity*>(entity())->post_own_event(kGroundGone, nullptr);
    collision_events::checkpoint(this, "GroundGone-event"); // (not original)
    start_vert_air();
    collision_events::checkpoint(this, "after-4FEBA0"); // (not original)
    objects::RiderPoseState* flags = state_;
    if (flags->flag(PoseFlag::kVertAir).on != 0U) {
        set_timed_flag(flags, PoseFlag::kLeftVert, 1);
        // The original: unless both triggers are held. PartyMod only turns the first check's JZ into a JNZ (at
        // 0x00509E47), so under its PS2 controls this is skipped only while the second is held without the first.
        const bool skip = ps2_controls() ? (input->pad_primary().shoulder_left_b().held_down() == 0U) &&
                                               (input->pad_primary().shoulder_right_b().held_down() != 0U)
                                         : (input->pad_primary().shoulder_left_b().held_down() != 0U) &&
                                               (input->pad_primary().shoulder_right_b().held_down() != 0U);
        if (!skip) {
            break_vert(false);
        }
        const int wait = script::fetch_int_value(0x6BB5B751);
        if (time_since_release(&input->pad_primary().up_dir()) > wait &&
            time_since_press(&input->pad_primary().up_dir()) > wait) {
            set_timed_flag(state_, PoseFlag::kLeftVert, 0);
        }
    } else if (triggers_held(input)) {
        // (The original: both triggers; PartyMod's JZ -> JNZ at 0x0050A257 makes it either.)
        AcidDrop drop{};
        objects::GameEntity* skater = entity();
        if (find_acid_drop(true, &skater->at_point(), &skater->old_pos(), &skater->vel(), &drop)) {
            start_acid_drop(&drop);
        }
    }
    static_cast<objects::ActivationZoneModule*>(trigger_)->probe_touches_zone(kOtherFeelerLeaveEvents,
                                                                              &feelers_.floor_feeler);
}

// check_for_ground on skatable ground: the skater kept to it (true) unless the hit is too steep or too far.
bool SkaterPhysicsModule::keep_to_ground(GroundReach hit, bool bike)
{
    objects::GameEntity* object = entity();
    objects::PadInputModule* input = input_;
    const float distance = hit.distance;
    const float along = hit.along;
    const maths::Vec4 normal = feelers_.feeler.surface_dir();
    maths::Vec4 facing = core::at(object->basis_frame().row, 2);
    if (bike && thaw_bmx::rolling_backwards(this)) { // (not original: THAW 0x005DEB08, rolling back on the bike)
        facing = {.x = -facing.x, .y = -facing.y, .z = -facing.z, .w = -facing.w};
    }
    maths::Vec4 in_hit_plane = facing;
    maths::Vec4 in_ground_plane = facing;
    auto facing_into_hit =
        static_cast<float>((static_cast<double>(facing.x) * normal.x) + (static_cast<double>(facing.z) * normal.z) +
                           (static_cast<double>(facing.y) * normal.y));
    in_hit_plane.align_to_plane(normal);
    in_ground_plane.align_to_plane(ground_.floor_normal);
    auto turn = static_cast<float>((static_cast<double>(in_ground_plane.x) * in_hit_plane.x) +
                                   (static_cast<double>(in_ground_plane.z) * in_hit_plane.z) +
                                   (static_cast<double>(in_ground_plane.y) * in_hit_plane.y));
    const float angle =
        tuning_float_lookup((input->pad_primary().up_dir().held_down() != 0U) ? 0x4138283E : 0xEF161C2A, object);
    const double limit = std::cos(static_cast<double>(angle) * 0.0174532924F); // (degrees to radians)
    bool keep = true;
    if (facing_into_hit > 0.0F && turn > 0.0F && turn < limit) {
        keep = false;
    }
    if (keep && along > 0.0F) {
        // How far the skater could have dropped this frame at that angle (at least a physics value).
        const float cosine = turn > 1.0F ? 1.0F : std::max(turn, -1.0F);
        const maths::Vec4& skater_position = object->at_point();
        const maths::Vec4& old = object->old_pos();
        const float mx = skater_position.x - old.x;
        const float my = skater_position.y - old.y;
        const float mz = skater_position.z - old.z;
        const double moved =
            std::sqrt((static_cast<double>(mx) * mx) + (static_cast<double>(mz) * mz) + (static_cast<double>(my) * my));
        auto reach = static_cast<float>(std::tan(std::acos(static_cast<double>(cosine))) * moved);
        const float least = tuning_float_lookup(0x899BA3D0, object);
        reach = std::max(reach, least);
        if (distance > reach) {
            keep = false;
        }
    }
    if (keep) {
        objects::RiderPoseState* flags = state_;
        set_timed_flag(flags, PoseFlag::kOnVert, feelers_.hit_vert);
        turn_to_normal(normal);
        object->at_point() = feelers_.feeler.contact_pos();
        flags = state_;
        if (flags->cur_phase() == 0 && distance > kMinTravel && turn > kMinTurnCos) {
            set_timed_flag(flags, PoseFlag::kGroundSnapped, 1);
        }
        const uint8_t surface = feelers_.feeler.hit().terrain;
        static_cast<objects::RiderPoseState*>(state_)->set_surface_kind(surface);
        sound_->set_surface_type(surface);
        if (!bike || !thaw_bmx::lerp_display_normal(this)) { // (not original: the bike's, THAW 0x005CB420)
            update_ground_normal();
        }
        start_riding_hit_object();
        if (feelers_.floor_feeler.hit().object != feelers_.feeler.hit().object) {
            static_cast<objects::ActivationZoneModule*>(trigger_)->probe_touches_zone(kKickEvents,
                                                                                      &feelers_.floor_feeler);
            if (physics_control_->flag26() != 0U) {
                return true;
            }
            static_cast<objects::ActivationZoneModule*>(trigger_)->probe_touches_zone(kSkaterEvents0064bBb0,
                                                                                      &feelers_.feeler);
            if (physics_control_->flag26() != 0U) {
                return true;
            }
        }
        objects::MoveExecutor* skater = trick_;
        if (!skater->trick_objects_on()) {
            skater->set_trick_objects(false);
        }
        feelers_.floor_feeler.get_assign(feelers_.feeler);
        sound_->set_last_ground(feelers_.floor_feeler.hit().terrain);
        return true;
    }
    collision_events::note_path(this, collision_events::RouteWay::kGroundRejected,
                                object->vel()); // (not original)
    return false;
}

// What the skater rides on (if it's still under the skater) carries it by.
maths::Vec4 SkaterPhysicsModule::carried_by_ride()
{
    maths::Vec4 carried = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    {
        objects::MovingSurfaceModule* ride = ride_;
        objects::RideTracker* rider = ride->ride().get();
        const objects::GameEntity* object = entity();
        if (rider) {
            if (!rider->entity() ||
                !rider->tick_frame(&object->at_point())) { // (what it rides: gone, or its frame failed)
                stop_riding(ride);
            } else if ((state_->flag(PoseFlag::kAcidDrop).on == 0U) || (drop_object_hit_ != 0U)) {
                carried = ride_->ride()->carried();
            }
        }
    }
    return carried;
}

// Out of vert: off a wall (the wall feeler), the time to break out, or the triggers.
void SkaterPhysicsModule::leave_vert(objects::PadInputModule* input, objects::GameEntity* object)
{
    objects::RiderPoseState* flags = state_;
    if (flags->flag(PoseFlag::kLeftVert).on != 0U) {
        if ((flags->flag(PoseFlag::kVertWall).on != 0U) && (flags->flag(PoseFlag::kVertAir).on != 0U)) {
            // On a wall: a feeler from the skater's up (+1) to 23 below; off it, break out of vert.
            const maths::Vec4& up = core::at(object->basis_frame().row, 1);
            const maths::Vec4& position = object->at_point();
            maths::Vec4& start = feelers_.probe_start;
            maths::Vec4& end = feelers_.probe_end;
            float x = up.x + position.x;
            float y = up.y + position.y;
            float z = up.z + position.z;
            float w = up.w + position.w;
            start.z = z, start.w = w, start.x = x, start.y = y;
            const float ey = up.y * -23.0F;
            const float ez = up.z * -23.0F;
            const float ew = up.w * -23.0F;
            x = static_cast<float>((static_cast<double>(up.x) * -kStandProbeOffset) + position.x);
            y = ey + position.y, z = ez + position.z, w = ew + position.w;
            end.z = z, end.w = w, end.x = x, end.y = y;
            if (!feelers_.probe_touches_zone(render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
                break_vert(false);
                const int wait = script::fetch_int_value(0x6BB5B751);
                if (time_since_release(&input->pad_primary().up_dir()) > wait &&
                    time_since_press(&input->pad_primary().up_dir()) > wait) {
                    set_timed_flag(state_, PoseFlag::kLeftVert, 0);
                }
            }
        }
        const uint32_t stamp = state_->flag(PoseFlag::kLeftVert).changed_at;
        auto now = static_cast<uint32_t>(frame_clock::game_clock_elapsed());
        const int elapsed = static_cast<int>(now + (static_cast<uint32_t>(objects::clock_offset()) - stamp));
        if (elapsed > script::fetch_int_value(kSkaterVertAllowBreakTime)) {
            set_timed_flag(state_, PoseFlag::kLeftVert, 0);
        }
    } else if (flags->flag(PoseFlag::kVertAir).on != 0U) {
        // (The original: both triggers. PartyMod's JZ -> JNZ at 0x0050CF89 makes it either.)
        if ((triggers_held(input) || (transfer_blocked_ != 0U)) && (flags->flag(PoseFlag::kAcidDrop).on == 0U) &&
            object->vel().y > 0.0F) {
            break_vert(false);
        }
    }
}

// Along a vert wall: a feeler across it, stepped down until it hits; on it the skater stays, else it's off the wall.
void SkaterPhysicsModule::follow_vert_wall()
{
    maths::Vec4& start = feelers_.probe_start;
    maths::Vec4& end = feelers_.probe_end;
    start.x = entity()->at_point().x;
    start.y = vert_.takeoff.y;
    start.z = entity()->at_point().z;
    end = start;
    const maths::Vec4& wall = vert_.wall_normal;
    const float wz = wall.z * 30.0F;
    const float ww = wall.w * 30.0F;
    start.x = static_cast<float>((static_cast<double>(wall.x) * kWallProbeReach) + start.x);
    start.y = static_cast<float>((static_cast<double>(wall.y) * kWallProbeReach) + start.y);
    start.z = wz + start.z;
    start.w = ww + start.w;
    end.x = static_cast<float>(end.x - (static_cast<double>(wall.x) * kWallProbeReach));
    end.y = static_cast<float>(end.y - (static_cast<double>(wall.y) * kWallProbeReach));
    end.z -= wz;
    end.w -= ww;
    float& step = vert_.step;
    bool found = false;
    if (step > kStepMin) {
        start.y = step + start.y;
        end.y = end.y + step;
        if (feelers_.probe_touches_zone(render::kNoFaceFlags, render::FaceFlag::kVert)) {
            found = true;
        } else {
            start.y -= step;
            end.y -= step;
            step *= kStepShrink;
        }
    }
    if (!found && feelers_.probe_touches_zone(render::kNoFaceFlags, render::FaceFlag::kVert)) {
        found = true;
    }
    for (int i = 0; !found && i < kLowerTries; ++i) {
        start.y -= kLowerStep;
        end.y -= kLowerStep;
        if (feelers_.probe_touches_zone(render::kNoFaceFlags, render::FaceFlag::kVert)) {
            vert_.takeoff.y = feelers_.feeler.contact_pos().y;
            found = true;
        }
    }
    const maths::Vec4& point = feelers_.feeler.contact_pos();
    const maths::Vec4& normal = feelers_.feeler.surface_dir();
    if (found && std::sqrt(std::fabs((static_cast<double>(wall.z) * normal.z) +
                                     (static_cast<double>(wall.x) * normal.x))) > kNormalTolerance) {
        // Still along the wall: stay on it (not below the height so far); its normal is the new wall normal,
        // and the skater is pushed off it by a script value.
        const float height = point.y < vert_.takeoff.y ? vert_.takeoff.y : point.y;
        const float hz = point.z;
        const float hw = point.w;
        vert_.takeoff.x = point.x;
        vert_.takeoff.w = hw;
        vert_.takeoff.y = height;
        vert_.takeoff.z = hz;
        entity()->at_point().x = point.x;
        entity()->at_point().z = hz;
        maths::Vec4& new_wall = vert_.wall_normal;
        new_wall = normal;
        const float push = script::fetch_float(0x78384871);
        maths::Vec4& position = entity()->at_point();
        const float py = push * new_wall.y;
        const float pz = push * new_wall.z;
        const float pw = push * new_wall.w;
        position.x = static_cast<float>((static_cast<double>(push) * new_wall.x) + position.x);
        position.y += py;
        position.z += pz;
        position.w += pw;
        maths::Vec4 flat = {.x = new_wall.x, .y = 0.0F, .z = new_wall.z, .w = new_wall.w};
        flat.make_unit();
        turn_to_normal(flat);
        entity()->vel().align_to_plane(flat);
    } else {
        set_timed_flag(state_, PoseFlag::kVertWall, 0);
    }
}

// The landing feeler's hit: landed on (true), or turned along and pushed off.
bool SkaterPhysicsModule::land_on_hit(maths::Vec4& from, const maths::Vec4& carried, objects::GameEntity* object)
{
    const bool steep = feelers_.feeler.surface_dir().y < 0.1F;
    from = (carried + entity()->old_pos());
    // (Not yet, while something above says so - fast or steep, and the ride's timer past 500.)
    const bool not_yet =
        step_up(&from) && (entity()->vel().y > 10.0F || steep) &&
        static_cast<uint32_t>(static_cast<objects::MovingSurfaceModule*>(ride_)->time_since_stop()) > 500;
    if (!not_yet) {
        const maths::Vec4 normal = feelers_.feeler.surface_dir();
        entity()->at_point() = feelers_.feeler.contact_pos();
        entity()->at_point() += normal;
        if (feelers_.hit_skatable != 0U) {
            land_from_air();
            return true;
        }
        // Not something to land on: turn along it and push off it.
        entity()->vel().align_to_plane(normal);
        core::at(entity()->basis_frame().row, 2).align_to_plane(normal);
        entity()->basis_frame().square_up_axes(2);
        ground_.display_matrix = entity()->basis_frame();
        const float push = tuning_float_lookup(0x23410C14, object);
        entity()->at_point() += normal * push;
        set_timed_flag(state_, PoseFlag::kNoAcidDrop, 1);
    }
    return false;
}

// Not original (THAW's BMX, phase 3 - THAW 0x005DF3E0..0x005DF7B5): the bike on the ground under its wheels
// (ThawBmx::wheel_ground_check has put it there): the display normal towards the ground's; with the feeler under the
// skater on the ground (`hit`), what check_for_ground does on the ground after that: the terrain, a ride on what was
// hit, the triggers, the trick objects, the feeler kept.
void SkaterPhysicsModule::ground_under_wheels(bool hit)
{
    if (!thaw_bmx::lerp_display_normal(this)) {
        update_ground_normal();
    }
    if (!hit) {
        return;
    }
    const uint8_t surface = feelers_.feeler.hit().terrain;
    static_cast<objects::RiderPoseState*>(state_)->set_surface_kind(surface);
    sound_->set_surface_type(surface);
    start_riding_hit_object();
    if (feelers_.floor_feeler.hit().object != feelers_.feeler.hit().object) {
        static_cast<objects::ActivationZoneModule*>(trigger_)->probe_touches_zone(kKickEvents, &feelers_.floor_feeler);
        if (physics_control_->flag26() != 0U) {
            return;
        }
        static_cast<objects::ActivationZoneModule*>(trigger_)->probe_touches_zone(kSkaterEvents0064bBb0,
                                                                                  &feelers_.feeler);
        if (physics_control_->flag26() != 0U) {
            return;
        }
    }
    objects::MoveExecutor* skater = trick_;
    if (!skater->trick_objects_on()) {
        skater->set_trick_objects(false);
    }
    feelers_.floor_feeler.get_assign(feelers_.feeler);
    sound_->set_last_ground(feelers_.floor_feeler.hit().terrain);
}

// 0x0050CA00
void SkaterPhysicsModule::do_air_physics()
{
    objects::PadInputModule* input = input_;
    auto gravity = static_cast<float>(get_gravity());
    set_timed_flag(state_, PoseFlag::kOnVert, 0);
    objects::RiderPoseState* flags = state_;
    if ((flags->flag(PoseFlag::kVertAir).on != 0U) || (flags->flag(PoseFlag::kAcidDrop).on != 0U) ||
        (flags->flag(PoseFlag::kFromAcidDrop).on != 0U) || (flags->flag(PoseFlag::kBailing).on != 0U)) {
        set_timed_flag(flags, PoseFlag::kNoAcidDrop, 1);
    }
    point_rail_spin_ = 0;
    // Not original: THAW 0x005DF880 - on the bike no manual in the air, and its own pitch and spin (0x005D7CB0) for
    // the lean and spin (thaw_bmx_physics.cpp).
    const bool bike = thaw_bmx::live_now(this);
    if (bike) {
        thaw_bmx::on_air(this);
    } else {
        lean_in_air();
    }
    check_flip_spin();
    if (bike) {
        thaw_bmx::air_rotation(this);
    } else {
        get_spin();
    }
    rotate_in_acid_drop();
    // (The original: flag +0x80 or both triggers. PartyMod's JZ -> JNZ at 0x0050CAC4 makes it either trigger.)
    if ((state_->flag(PoseFlag::kRecoverInAir).on != 0U) || triggers_held(input)) {
        recover_in_air();
    }
    // Not original: THAW 0x005DF98B - on the bike the tire tap (looked for, or carried on), and while it's on the
    // bike isn't kept upright (0x005DF9DB).
    if (bike) {
        thaw_bmx::tire_tap(this);
    }
    {
        // (the rotate component's turns about x and z)
        if ((state_->flag(PoseFlag::kVertAir).on == 0U) && (state_->flag(PoseFlag::kAcidDrop).on == 0U) &&
            !core::at(rotate_->turns(), 0).active && !core::at(rotate_->turns(), 2).active &&
            !(bike && thaw_bmx::tire_tapping(this))) {
            keep_upright();
        }
    }
    if (friction_.special_time != 0.0F && forces_this_tick()) {
        // (not original: at another tick rate once a 1/60 s frame over the whole of it - forces_this_tick)
        const float tick_length = frame_.length;
        frame_.length = force_step();
        apply_wheel_drag();
        frame_.length = tick_length;
    }

    maths::Vec4 velocity = entity()->vel();
    int& countdown = late_wallplant_.frames;
    if (countdown > 0) {
        // (not original: the window counts 1/60 s frames - the 60 Hz grid, frame_clock::grid_frames - and the wallplant
        // is tried each tick in it; at 60 one a tick, as the original's --countdown)
        countdown -= frame_clock::grid_frames();
        if (wall_plant(true, false, false)) {
            countdown = 0;
        } else if (countdown <= 0) {
            countdown = 0;
            core::at(entity()->basis_frame().row, 2).align_to_plane(late_wallplant_.wall_normal);
        }
    }
    flags = state_;
    if (flags->flag(PoseFlag::kAcidDrop).on != 0U) {
        if (velocity.y < 0.0F && entity()->at_point().y < drop_target_height_) {
            flags->drop_velocity().set_length(kDropVelocityLength);
        } else {
            velocity += flags->drop_velocity();
        }
    }

    // What the skater rides on (+0x152C's +0x18, if it's still under the skater) carries it along.
    const maths::Vec4 carried = carried_by_ride();

    // Move: position += velocity * t + (0, gravity, 0, 1) * t * t / 2, then the carried offset; velocity +=
    // (0, gravity, 0, 1) * t. (Rounded as the original: x's velocity product and the z, w sums stay unrounded.)
    {
        const float frame_length = frame_.length;
        const double half = static_cast<double>(frame_length) * frame_length * 0.5F;
        auto zero_half = static_cast<float>(0.0 * half);
        auto half_gravity = static_cast<float>(half * gravity);
        auto half_float = static_cast<float>(half);
        const float ty = frame_length * velocity.y;
        const float tz = frame_length * velocity.z;
        const float tw = frame_length * velocity.w;
        auto sx = static_cast<float>((static_cast<double>(velocity.x) * frame_length) + zero_half);
        const float sy = ty + half_gravity;
        const double sz = (0.0 * half) + tz;
        const double sw = static_cast<double>(tw) + half_float;
        maths::Vec4& position = entity()->at_point();
        position.x = sx + position.x;
        position.y = sy + position.y;
        position.z = static_cast<float>(sz + position.z);
        position.w = static_cast<float>(sw + position.w);
        entity()->at_point() += carried;

        const float tt = frame_.length;
        const float zero = 0.0F * tt;
        const float fall = tt * gravity;
        maths::Vec4& v = entity()->vel();
        v.x += zero;
        v.y += fall;
        v.z += zero;
        v.w += tt;
    }

    objects::GameEntity* object = entity();
    leave_vert(input, object);

    // Along a wall (+0x278: its normal; +0x26C: the height so far): a feeler across it, stepped down until it
    // hits.
    bool riding = false;
    {
        objects::MovingSurfaceModule* ride = ride_;
        const objects::RideTracker* rider = ride->ride().get();
        if (rider && rider->entity()) {
            riding = true;
        } else if (rider) {
            stop_riding(ride);
        }
    }
    if (!riding && (state_->flag(PoseFlag::kVertWall).on != 0U) && (state_->flag(PoseFlag::kVertAir).on != 0U)) {
        follow_vert_wall();
    }

    {
        if ((state_->flag(PoseFlag::kVertAir).on == 0U) || (state_->flag(PoseFlag::kAcidDrop).on != 0U) ||
            (model_rotating_ != 0U) || core::at(rotate_->turns(), 0).active || core::at(rotate_->turns(), 2).active) {
            ground_.display_matrix = entity()->basis_frame();
        } else {
            update_ground_normal();
        }
    }
    if (bike) { // (not original: THAW 0x005E0476 - on the bike the wheel points follow it through the air)
        thaw_bmx::air_wheel_points(this);
    }

    // Not original: at a tick rate other than 60 the feelers below run once a 1/60 s frame, in the tick that ends it, from
    // where the skater was when it began - the 60 Hz run's segment (the move itself composes exactly), so the landing,
    // the walls and the ceiling are met where and in the frame they are at 60 (docs/design/tick-rate.md "The 60 Hz
    // grid"). The ticks in between only let go of the jump button. At 60: every tick, from the tick's start.
    if (!tick_rate::original()) {
        if (!frame_clock::grid_frame_ends()) {
            jump_released();
            return;
        }
        entity()->old_pos() = grid_frame_start();
    }
    // Landing: a feeler from the last position (carried along) to the new one.
    auto last_position = [&] { return carried + entity()->old_pos(); };
    maths::Vec4 from = last_position();
    const bool stopped = check_air_forward_collision(&from);
    const int state = state_->cur_phase();
    if (state == 2 || state == kPhaseWallplant) {
        return;
    }
    feelers_.probe_start = last_position();
    feelers_.probe_end = entity()->at_point();
    if (!stopped && feelers_.probe_touches_zone(render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
        if (land_on_hit(from, carried, object)) {
            return;
        }
    } else if (state_->flag(PoseFlag::kVertAir).on == 0U) {
        test_lateral_hits();
    }

    if (keep_off_ceiling()) {
        set_timed_flag(state_, PoseFlag::kNoAcidDrop, 1);
    }
    jump_released();
    vert_wallplant();
    if (state_->flag(PoseFlag::kNoAcidDrop).on != 0U) {
        return;
    }
    // An acid drop: both triggers (the original; PartyMod's JZ -> JNZ at 0x0050DA64 makes it either).
    if (!triggers_held(input)) {
        return;
    }
    bool recent = false;
    if (air_time_.start > ints::as_unsigned(leap_time_)) {
        recent =
            static_cast<uint32_t>(frame_clock::game_clock_time_since(static_cast<int>(air_time_.start))) < kRecentAirMs;
    }
    AcidDrop drop{};
    objects::GameEntity* skater = entity();
    if (find_acid_drop(recent, &skater->at_point(), &skater->old_pos(), &skater->vel(), &drop)) {
        start_acid_drop(&drop);
    }
}

// (Part of 0x0050CA00: landing on something skatable.)
void SkaterPhysicsModule::land_from_air()
{
    set_physics_state(0);
    start_riding_hit_object();
    objects::RiderPoseState* flags = state_;
    if ((flags->flag(PoseFlag::kVertAir).on != 0U) || (flags->flag(PoseFlag::kAcidDrop).on != 0U)) {
        air_flags_.landed_from_vert = 1;
        air_flags_.landed_from_air_vert = 1;
    } else {
        air_flags_.landed_from_air_vert = 0;
    }
    air_flags_.came_down_off_spine = flags->flag(PoseFlag::kAcidDrop).on;
    set_surface_kind(feelers_.feeler.hit().terrain);
    if (on_vehicle_ == 0U) {
        const uint32_t which = state_->surface_type();
        objects::GameEntity* object = entity();
        const float speed = object->vel().len_of();
        const float scale = core::down_cast<objects::Rider>(object)->get_scaled_physics(0xCC5F87AA);
        static_cast<objects::RiderSfxPlayer*>(sound_)->play_land({.speed = speed / scale, .terrain = static_cast<int>(which)});
    }
    objects::MoveExecutor* skater = trick_;
    if (!skater->trick_objects_on()) {
        skater->set_trick_objects(false);
    }
    remember_floor_probe(&feelers_.feeler);
    static_cast<objects::ActivationZoneModule*>(trigger_)->probe_touches_zone(kSkaterEvents0064bB98,
                                                                              &feelers_.floor_feeler);
    if (physics_control_->flag26() == 0U) {
        const maths::Vec4& normal = feelers_.feeler.surface_dir();
        maths::Vec4& velocity = entity()->vel();
        const objects::PadInputModule* input = input_;
        if ((air_flags_.landed_from_air_vert == 0U) && (air_flags_.landed_from_vert == 0U) && velocity.x == 0.0F &&
            velocity.z == 0.0F && (input->pad_primary().btn_down().held_down() != 0U)) {
            // (Landing straight down with button +0x40 held: no bounce.)
            velocity.y = 0.0F; // (the original stores the word 0)
        } else {
            if (state_->flag(PoseFlag::kAcidDrop).on == 0U) {
                velocity.flatten_onto_plane(normal);
            } else {
                // Landing from a wall ride: along +0x490's direction in the ground's plane, unless that turns
                // the skater upwards (0x004FF590) - then the old velocity in the plane; at least a script speed.
                const maths::Vec4 saved = velocity;
                velocity.change_heading(drop_.heading);
                velocity.align_to_plane(normal);
                maths::Vec4& v = entity()->vel();
                maths::Vec4 probe = v;
                get_along_facing(&probe);
                if (probe.y > 0.0F) {
                    v = saved;
                    entity()->vel().flatten_onto_plane(normal);
                }
                const float least = script::fetch_float(0x59484878);
                if (static_cast<double>(least) * least > entity()->vel().magnitude_squared()) {
                    entity()->vel().set_length(script::fetch_float(kPhysicsAcidDropMinLandSpeed));
                }
            }
            entity()->vel().clamp_tiny_to_nothing(kTinyVelocityClamp);
        }
        set_timed_flag(state_, PoseFlag::kVertAir, 0);
        transfer_blocked_ = 0;
        if (thaw_bmx::live_now(this)) {
            // Not original: THAW 0x005E0AA4 - on the bike the landing keeps the
            // bike's own up as the display / ground normal, the display matrix the skater's as it landed, and then
            // sets the ground normal to the hit's (0x004FE860: the skater's up turned to it, the display normal's
            // lerp started) - the bike eases into the landing instead of snapping to the ground.
            const maths::Vec4 up = core::at(entity()->basis_frame().row, 1);
            const maths::Vec4 hit = normal;
            ground_.drawn_normal = up;
            ground_.floor_normal = up;
            ground_.old_normal = ground_.drawn_normal;
            find_nearest_wall_side();
            ground_.display_matrix = entity()->basis_frame();
            turn_to_normal(hit);
            static_cast<objects::Entity*>(entity())->post_own_event(kLandedName, nullptr);
            return;
        }
        maths::Vec4& ground = ground_.drawn_normal;
        ground = normal;
        ground_.floor_normal = ground;
        ground_.old_normal = ground;
        find_nearest_wall_side();
        core::at(entity()->basis_frame().row, 1) = ground;
        entity()->basis_frame().square_up_axes(1);
        ground_.display_matrix = entity()->basis_frame();
    }
    static_cast<objects::Entity*>(entity())->post_own_event(kLandedName, nullptr);
}

} // namespace skater
