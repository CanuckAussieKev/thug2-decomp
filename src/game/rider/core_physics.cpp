// The skater's core physics component, with PartyMod's PS2-style controls built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/rider/core_physics.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
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
#include "platform/game_clock.h"
#include "platform/run_log.h"
#include "engine/objects/trigger_events.h"

namespace skater {

namespace {

template <typename T>
T& get_at(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The component's fields (offsets).
constexpr uint32_t kObject = 0xC;         // the skater's object (below)
constexpr uint32_t kDisplayMatrix = 0x18; // (a copy of the object's matrix)
constexpr uint32_t kFloorUpVector = 0x68; // (a vector the wall check turns the facing into)
constexpr uint32_t kFeeler = 0xA8;        // a collision feeler (its result is copied below)
constexpr uint32_t kSurfaceFlags = 0xFA;  // (a byte passed on when landing)
constexpr uint32_t kHitPoint = 0x120;     // the feeler's hit: point, then normal
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kOtherFeeler = 0x158; // another feeler
constexpr uint32_t kProbeOrigin = 0x208; // the feeler's line
constexpr uint32_t kProbeTip = 0x218;
constexpr uint32_t kHitSkatable = 0x228;    // (set by the feeler check 0x005003A0)
constexpr uint32_t kHitFlag = 0x229;        // (likewise)
constexpr uint32_t kWallFaceVector = 0x278; // riding a wall: its normal (+0x268: the point, +0x26C its height)
constexpr uint32_t kInput = 0x150C;         // the pad: a button every 0x20 bytes (pressed, then times)
constexpr uint32_t kActivationZoneSlot = 0x1510;
constexpr uint32_t kOtherComponent = 0x1514; // (+0x1C, +0x1D: bytes set on landing)
constexpr uint32_t kSkaterComponent = 0x1518;
constexpr uint32_t kOtherFlags = 0x151C;   // (+0x28, +0x50: bools)
constexpr uint32_t kTimedFlags = 0x1528;   // bools, each followed by the time it last changed
constexpr uint32_t kRide = 0x152C;         // +0x18: what the skater rides on (+0x5C: how far it moved)
constexpr uint32_t kTriggerState = 0x1530; // +0x26: a trigger's script took over

// The object's fields.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kOldPosition = 0x5C;
constexpr uint32_t kMatrix = 0x6C;
constexpr uint32_t kUp = 0x7C; // the matrix's rows (at +0x6C): up, then the facing
constexpr uint32_t kFacing = 0x8C;
constexpr uint32_t kVelocity = 0xAC;

// The input's buttons (offsets into the pad).
constexpr uint32_t kButton20 = 0x20;
constexpr uint32_t kButton40 = 0x40;
constexpr uint32_t kButton60 = 0x60;
constexpr uint32_t kButton80 = 0x80;
constexpr uint32_t kTrigger1 = 0xC0;
constexpr uint32_t kTrigger2 = 0x120;

maths::Vec4& vector_at(void* object, uint32_t offset) { return get_at<maths::Vec4>(object, offset); }

// 0x00580400 / 0x00580440: a physics value (the skater's own tuning structure for `object`, else the global
// script value).
float tuning_float_lookup(uint32_t name, void* object)
{
    return objects::read_tuning_float(name, 1, static_cast<objects::GameEntity*>(object));
}
int physics_integer(uint32_t name, void* object)
{
    return objects::read_tuning_int(name, 1, static_cast<objects::GameEntity*>(object));
}

// The game's clock (0x0044ECE0, plus the offset at 0x00696D60).
int get_now()
{
    int time = frame_clock::game_clock_elapsed();
    return time + objects::g_clock_offset;
}

// A timed flag: a bool and, after it, the time it last changed.
void set_timed_flag(void* flags, uint32_t offset, uint8_t value)
{
    uint8_t& flag = get_at<uint8_t>(flags, offset);
    if (flag == value)
        return;
    flag = value;
    get_at<int>(flags, offset + 4) = get_now();
}

// A button's times: since it was last pressed (0x0046B920) / released (0x0046B900).
int time_since_press(uint8_t* button) { return reinterpret_cast<objects::ControlButton*>(button)->get_held_millis(); }
int time_since_release(uint8_t* button)
{
    return reinterpret_cast<objects::ControlButton*>(button)->read_let_go_time();
}

// The components the skater's physics reaches through its own fields.
objects::ActivationZoneModule* trigger_of(void* component)
{
    return static_cast<objects::ActivationZoneModule*>(get_at<void*>(component, kActivationZoneSlot));
}
render::ProbeRay* feeler_at(void* component, uint32_t offset) { return &get_at<render::ProbeRay>(component, offset); }
const int* get_events(uint32_t address) { return reinterpret_cast<const int*>(address); }
void stop_riding(void* ride) { static_cast<objects::MovingSurfaceModule*>(ride)->stop_riding(); }

// The two triggers: both held - or, under PartyMod's PS2 controls (its JZ -> JNZ at each check), either.
bool triggers_held(const uint8_t* input)
{
    if (ps2_controls())
        return input[kTrigger1] || input[kTrigger2];
    return input[kTrigger1] && input[kTrigger2];
}

// velocity += direction * scale, the x component's product unrounded (the original keeps it on the x87 stack).
void stack_on(maths::Vec4& velocity, const maths::Vec4& direction, float scale)
{
    float y = direction.y * scale;
    float z = direction.z * scale;
    float w = direction.w * scale;
    velocity.x = static_cast<float>(velocity.x + static_cast<double>(direction.x) * scale);
    velocity.y += y;
    velocity.z += z;
    velocity.w += w;
}

} // namespace

// 0x004FCBA0
int SkaterPhysicsModule::lip_push_off(script::Struct*, script::Script*)
{
    void* object = get_at<void*>(this, kObject);
    const maths::Vec4& facing = vector_at(object, kFacing);
    maths::Vec4 forward = {facing.x, 0.0f, facing.z, facing.w};
    double length = std::sqrt(static_cast<double>(forward.z) * forward.z + static_cast<double>(forward.x) * forward.x);
    if (length > 0.0) {
        double scale = 1.0 / length; // (w isn't scaled; y stays 0)
        forward.x = static_cast<float>(forward.x * scale);
        forward.z = static_cast<float>(forward.z * scale);
    }
    maths::Vec4 side = {forward.z, 0.0f, -forward.x, 0.0f};

    uint8_t* input = get_at<uint8_t*>(this, kInput);
    if (input[kButton80])
        return 0;
    // (The original: both triggers. PartyMod's JZ -> JNZ at 0x004FCC59 makes it either.)
    if (triggers_held(input)) {
        float scale = tuning_float_lookup(0x1F96C224, object);
        stack_on(vector_at(get_at<void*>(this, kObject), kVelocity), forward, scale);
        return 1;
    }
    if (input[kButton60]) {
        int wait = physics_integer(0x3080E9E7, object);
        if (time_since_press(input + kButton60) > wait) {
            float scale = tuning_float_lookup(0xDC818B7E, object);
            stack_on(vector_at(get_at<void*>(this, kObject), kVelocity), forward, scale);
            return 1;
        }
    }
    if (input[kButton20]) {
        int wait = physics_integer(0x9B25142A, object);
        if (time_since_press(input + kButton20) > wait) {
            float scale = tuning_float_lookup(0x1AB3809F, object);
            maths::Vec4& velocity = vector_at(get_at<void*>(this, kObject), kVelocity);
            // velocity -= side * scale (x's product unrounded, as above)
            float y = scale * 0.0f;
            float z = -forward.x * scale;
            velocity.x = static_cast<float>(velocity.x - static_cast<double>(forward.z) * scale);
            velocity.y -= y;
            velocity.z -= z;
            velocity.w -= y;
            return 1;
        }
    }
    if (input[kButton40]) {
        int wait = physics_integer(0x9B25142A, object);
        if (time_since_press(input + kButton40) > wait) {
            float scale = tuning_float_lookup(0x1AB3809F, object);
            vector_at(get_at<void*>(this, kObject), kVelocity) += side * scale;
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
    float speed_from_ = 0.0f, height_from_ = 0.0f;
};

void log_rail_boost(script::Script* script, float before, float after, float height)
{
    static const bool kOn = config_int("features", "debug_rail_speed", 0) != 0;
    if (!kOn || !script)
        return;
    static const uint32_t kLoops[] = {checksums::hash_from_text("RailAccelerate2"),
                                      checksums::hash_from_text("ed_RailSpeedLoop")};
    if (script->script_title_ != kLoops[0] && script->script_title_ != kLoops[1])
        return;
    static RailBoostLog log;
    auto now = std::chrono::steady_clock::now();
    auto seconds = [](std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); };
    if (script != log.script || seconds(now - log.last) > 0.5) {
        float rate = 0.0f;
        script->args_->fetch_float(checksums::hash_from_text("rate"), &rate, 0);
        get_log("rail speed-up: %s boosting (rate %.3f): speed %.0f -> %.0f, height %.0f",
                script->script_title_ == kLoops[0] ? "RailAccelerate2" : "ed_RailSpeedLoop", rate, before, after,
                height);
        log.script = script;
        log.window_start_ = now;
        log.boosts = 0;
        log.speed_from_ = before;
        log.height_from_ = height;
    } else {
        ++log.boosts;
        double window = seconds(now - log.window_start_);
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
    float speed = 0.0f;
    params->fetch_float(0u, &speed, 0); // (unnamed)
    void* object = get_at<void*>(this, kObject);
    maths::Vec4& velocity = vector_at(object, kVelocity);
    double length_sqr = static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y +
                        static_cast<double>(velocity.z) * velocity.z;
    float before = static_cast<float>(std::sqrt(length_sqr));
    if (length_sqr < static_cast<double>(0.001f)) {                            // (not taken for a NaN)
        std::memcpy(&velocity, &vector_at(object, kFacing), sizeof(velocity)); // (moved, not loaded as floats)
    } else {
        double scale = 1.0 / std::sqrt(length_sqr);
        velocity.x = static_cast<float>(scale * velocity.x);
        velocity.y = static_cast<float>(scale * velocity.y);
        velocity.z = static_cast<float>(scale * velocity.z);
        velocity.w = static_cast<float>(scale * velocity.w);
    }
    velocity.x = speed * velocity.x;
    velocity.y = speed * velocity.y;
    velocity.z = speed * velocity.z;
    velocity.w = speed * velocity.w;
    log_rail_boost(script, before, speed, vector_at(object, kPosition).y);
    return 1;
}

// 0x00509BD0
void SkaterPhysicsModule::check_for_ground()
{
    void* object = get_at<void*>(this, kObject);
    uint8_t* input = get_at<uint8_t*>(this, kInput);
    // Not original: THAW 0x005DF050 - on the bike (out of a manual) the two wheels instead (thaw_bmx_physics.cpp).
    bool bike = thaw_bmx::live_now(this);
    thaw_bmx::WheelGround wheels = bike ? thaw_bmx::wheel_ground_check(this) : thaw_bmx::kWheelsNotUsed;
    if (wheels == thaw_bmx::kWheelsOnGround || wheels == thaw_bmx::kWheelsOnGroundNoHit) {
        ground_under_wheels(wheels == thaw_bmx::kWheelsOnGround);
        return;
    }
    if (wheels == thaw_bmx::kWheelsNotUsed) {
        // The feeler: from above the skater (up * a physics value) to 200 below, along its up axis.
        float above = tuning_float_lookup(0xE4D79235, object);
        maths::Vec4& start = vector_at(this, kProbeOrigin);
        maths::Vec4& end = vector_at(this, kProbeTip);
        start = vector_at(object, kUp) * above;
        end = vector_at(object, kUp) * -200.0f;
        start += vector_at(object, kPosition);
        end += vector_at(object, kPosition);

        if (probe_touches_zone(0x10, 0)) {
            const maths::Vec4& position = vector_at(object, kPosition);
            const maths::Vec4& point = vector_at(this, kHitPoint);
            const maths::Vec4& up = vector_at(object, kUp);
            double dx = static_cast<double>(position.x) - point.x;
            double dy = static_cast<double>(position.y) - point.y;
            double dz = static_cast<double>(position.z) - point.z;
            float distance = static_cast<float>(std::sqrt(dx * dx + dz * dz + dy * dy));
            float along = static_cast<float>(dz * up.z + dy * up.y + dx * up.x); // (how far above the hit, along up)

            if (!get_at<uint8_t>(this, kHitSkatable)) {
                // Not ground: if the skater is (nearly) at or below it, move it out by the hit's normal; then leave.
                if (along < 0.001f) {
                    maths::Vec4& p = vector_at(object, kPosition);
                    const maths::Vec4& normal = vector_at(this, kHitNormal);
                    float x = point.x + normal.x, y = point.y + normal.y, z = point.z + normal.z,
                          w = point.w + normal.w;
                    p.z = z;
                    p.w = w;
                    p.x = x;
                    p.y = y;
                }
                collision_events::note_path(this, collision_events::RouteWay::kGroundUnskatable,
                                            vector_at(object, kVelocity)); // (not original)
            } else {
                // Ground. The facing turned into the hit's plane and into the ground's plane: a hit at too steep
                // an angle to the current ground (cos of a physics angle - one value with button +0x20 held,
                // another without), or too far away for how fast the skater moves, isn't kept to.
                maths::Vec4 normal = vector_at(this, kHitNormal);
                maths::Vec4 facing = vector_at(object, kFacing);
                if (bike &&
                    thaw_bmx::rolling_backwards(this)) // (not original: THAW 0x005DEB08, rolling back on the bike)
                    facing = {-facing.x, -facing.y, -facing.z, -facing.w};
                maths::Vec4 in_hit_plane = facing;
                maths::Vec4 in_ground_plane = facing;
                float facing_into_hit = static_cast<float>(static_cast<double>(facing.x) * normal.x +
                                                           static_cast<double>(facing.z) * normal.z +
                                                           static_cast<double>(facing.y) * normal.y);
                in_hit_plane.align_to_plane(normal);
                in_ground_plane.align_to_plane(vector_at(this, kFloorUpVector));
                float turn = static_cast<float>(static_cast<double>(in_ground_plane.x) * in_hit_plane.x +
                                                static_cast<double>(in_ground_plane.z) * in_hit_plane.z +
                                                static_cast<double>(in_ground_plane.y) * in_hit_plane.y);
                float angle = tuning_float_lookup(input[kButton20] ? 0x4138283E : 0xEF161C2A, object);
                double limit = std::cos(static_cast<double>(angle) * 0.0174532924f); // (degrees to radians)
                bool keep = true;
                if (facing_into_hit > 0.0f && turn > 0.0f && turn < limit)
                    keep = false;
                if (keep && along > 0.0f) {
                    // How far the skater could have dropped this frame at that angle (at least a physics value).
                    float cosine = turn > 1.0f ? 1.0f : (turn < -1.0f ? -1.0f : turn);
                    const maths::Vec4& p = vector_at(object, kPosition);
                    const maths::Vec4& old = vector_at(object, kOldPosition);
                    float mx = p.x - old.x, my = p.y - old.y, mz = p.z - old.z;
                    double moved = std::sqrt(static_cast<double>(mx) * mx + static_cast<double>(mz) * mz +
                                             static_cast<double>(my) * my);
                    float reach = static_cast<float>(std::tan(std::acos(static_cast<double>(cosine))) * moved);
                    float least = tuning_float_lookup(0x899BA3D0, object);
                    if (reach < least)
                        reach = least;
                    if (distance > reach)
                        keep = false;
                }
                if (keep) {
                    void* flags = get_at<void*>(this, kTimedFlags);
                    set_timed_flag(flags, 0x40, get_at<uint8_t>(this, kHitFlag));
                    turn_to_normal(normal);
                    vector_at(object, kPosition) = vector_at(this, kHitPoint);
                    flags = get_at<void*>(this, kTimedFlags);
                    if (get_at<int>(flags, 0x18) == 0 && distance > 2.0f && turn > 0.99f)
                        set_timed_flag(flags, 0x98, 1);
                    uint8_t surface = get_at<uint8_t>(this, kSurfaceFlags);
                    static_cast<objects::RiderPoseState*>(get_at<void*>(this, kTimedFlags))->set_surface_kind(surface);
                    get_at<uint8_t>(get_at<uint8_t*>(this, kOtherComponent), 0x1C) = surface;
                    if (!bike || !thaw_bmx::lerp_display_normal(this)) // (not original: the bike's, THAW 0x005CB420)
                        update_ground_normal();
                    start_riding_hit_object();
                    if (get_at<int>(this, 0x1AC) != get_at<int>(this, 0xFC)) {
                        trigger_of(this)->probe_touches_zone(kKickEvents, feeler_at(this, kOtherFeeler));
                        if (get_at<uint8_t>(get_at<uint8_t*>(this, kTriggerState), 0x26))
                            return;
                        trigger_of(this)->probe_touches_zone(kSkaterEvents0064bBb0, feeler_at(this, kFeeler));
                        if (get_at<uint8_t>(get_at<uint8_t*>(this, kTriggerState), 0x26))
                            return;
                    }
                    void* skater = get_at<void*>(this, kSkaterComponent);
                    if (!get_at<uint8_t>(skater, 0x13B0))
                        static_cast<objects::MoveExecutor*>(skater)->set_trick_objects(false);
                    feeler_at(this, kOtherFeeler)->get_assign(*feeler_at(this, kFeeler));
                    get_at<uint8_t>(get_at<uint8_t*>(this, kOtherComponent), 0x1D) = get_at<uint8_t>(this, 0x1AA);
                    return;
                }
                collision_events::note_path(this, collision_events::RouteWay::kGroundRejected,
                                            vector_at(object, kVelocity)); // (not original)
            }
        } else {
            collision_events::note_path(this, collision_events::RouteWay::kNoGround,
                                        vector_at(object, kVelocity)); // (not original)
        }
    } // (not original: wheels == kWheelsNotUsed)

    // Off the ground.
    set_physics_state(1);
    collision_events::checkpoint(this, "set-air-4FDED0"); // (not original)
    static_cast<objects::Entity*>(get_at<void*>(this, kObject))->announce(0xD96F01F1, nullptr, 0.0f);
    collision_events::checkpoint(this, "SkaterOffEdge-event"); // (not original)
    static_cast<objects::Entity*>(get_at<void*>(this, kObject))->post_own_event(0x3B1001B6, nullptr);
    collision_events::checkpoint(this, "GroundGone-event"); // (not original)
    start_vert_air();
    collision_events::checkpoint(this, "after-4FEBA0"); // (not original)
    void* flags = get_at<void*>(this, kTimedFlags);
    if (get_at<uint8_t>(flags, 0x30)) {
        set_timed_flag(flags, 0x48, 1);
        // The original: unless both triggers are held. PartyMod only turns the first check's JZ into a JNZ (at
        // 0x00509E47), so under its PS2 controls this is skipped only while the second is held without the first.
        bool skip = ps2_controls() ? !input[kTrigger1] && input[kTrigger2] : input[kTrigger1] && input[kTrigger2];
        if (!skip)
            break_vert(false);
        int wait = script::fetch_int_value(0x6BB5B751);
        if (time_since_release(input + kButton20) > wait && time_since_press(input + kButton20) > wait)
            set_timed_flag(get_at<void*>(this, kTimedFlags), 0x48, 0);
    } else if (triggers_held(input)) {
        // (The original: both triggers; PartyMod's JZ -> JNZ at 0x0050A257 makes it either.)
        uint8_t drop[40];
        void* o = get_at<void*>(this, kObject);
        if (find_acid_drop(true, &vector_at(o, kPosition), &vector_at(o, kOldPosition), &vector_at(o, kVelocity),
                           reinterpret_cast<AcidDrop*>(drop)))
            start_acid_drop(reinterpret_cast<const AcidDrop*>(drop));
    }
    trigger_of(this)->probe_touches_zone(kOtherFeelerLeaveEvents, feeler_at(this, kOtherFeeler));
}

// Not original (THAW's BMX, phase 3 - THAW 0x005DF3E0..0x005DF7B5): the bike on the ground under its wheels
// (ThawBmx::wheel_ground_check has put it there): the display normal towards the ground's; with the feeler under the
// skater on the ground (`hit`), what check_for_ground does on the ground after that: the terrain, a ride on what was
// hit, the triggers, the trick objects, the feeler kept.
void SkaterPhysicsModule::ground_under_wheels(bool hit)
{
    if (!thaw_bmx::lerp_display_normal(this))
        update_ground_normal();
    if (!hit)
        return;
    uint8_t surface = get_at<uint8_t>(this, kSurfaceFlags);
    static_cast<objects::RiderPoseState*>(get_at<void*>(this, kTimedFlags))->set_surface_kind(surface);
    get_at<uint8_t>(get_at<uint8_t*>(this, kOtherComponent), 0x1C) = surface;
    start_riding_hit_object();
    if (get_at<int>(this, 0x1AC) != get_at<int>(this, 0xFC)) {
        trigger_of(this)->probe_touches_zone(kKickEvents, feeler_at(this, kOtherFeeler));
        if (get_at<uint8_t>(get_at<uint8_t*>(this, kTriggerState), 0x26))
            return;
        trigger_of(this)->probe_touches_zone(kSkaterEvents0064bBb0, feeler_at(this, kFeeler));
        if (get_at<uint8_t>(get_at<uint8_t*>(this, kTriggerState), 0x26))
            return;
    }
    void* skater = get_at<void*>(this, kSkaterComponent);
    if (!get_at<uint8_t>(skater, 0x13B0))
        static_cast<objects::MoveExecutor*>(skater)->set_trick_objects(false);
    feeler_at(this, kOtherFeeler)->get_assign(*feeler_at(this, kFeeler));
    get_at<uint8_t>(get_at<uint8_t*>(this, kOtherComponent), 0x1D) = get_at<uint8_t>(this, 0x1AA);
}

namespace {

uint8_t& bit_mark(void* component, uint32_t offset)
{
    return get_at<uint8_t>(get_at<void*>(component, kTimedFlags), offset);
}
void* object_of(void* component) { return get_at<void*>(component, kObject); }

} // namespace

// 0x0050CA00
void SkaterPhysicsModule::do_air_physics()
{
    uint8_t* input = get_at<uint8_t*>(this, kInput);
    float gravity = static_cast<float>(get_gravity());
    set_timed_flag(get_at<void*>(this, kTimedFlags), 0x40, 0);
    void* flags = get_at<void*>(this, kTimedFlags);
    if (get_at<uint8_t>(flags, 0x30) || get_at<uint8_t>(flags, 0x78) || get_at<uint8_t>(flags, 0xA8) ||
        get_at<uint8_t>(flags, 0x70))
        set_timed_flag(flags, 0xB0, 1);
    get_at<uint8_t>(this, 0x2EC) = 0;
    // Not original: THAW 0x005DF880 - on the bike no manual in the air, and its own pitch and spin (0x005D7CB0) for
    // the lean and spin (thaw_bmx_physics.cpp).
    bool bike = thaw_bmx::live_now(this);
    if (bike)
        thaw_bmx::on_air(this);
    else
        lean_in_air();
    check_flip_spin();
    if (bike)
        thaw_bmx::air_rotation(this);
    else
        get_spin();
    rotate_in_acid_drop();
    // (The original: flag +0x80 or both triggers. PartyMod's JZ -> JNZ at 0x0050CAC4 makes it either trigger.)
    if (bit_mark(this, 0x80) || triggers_held(input))
        recover_in_air();
    // Not original: THAW 0x005DF98B - on the bike the tire tap (looked for, or carried on), and while it's on the
    // bike isn't kept upright (0x005DF9DB).
    if (bike)
        thaw_bmx::tire_tap(this);
    {
        uint8_t* other = get_at<uint8_t*>(this, kOtherFlags);
        if (!bit_mark(this, 0x30) && !bit_mark(this, 0x78) && !other[0x28] && !other[0x50] &&
            !(bike && thaw_bmx::tire_tapping(this)))
            keep_upright();
    }
    if (get_at<float>(this, 0x320) != 0.0f)
        apply_wheel_drag();

    maths::Vec4 velocity = vector_at(object_of(this), kVelocity);
    int& countdown = get_at<int>(this, 0x14CC);
    if (countdown > 0) {
        --countdown;
        if (wall_plant(true, false, false))
            countdown = 0;
        else if (countdown == 0)
            vector_at(object_of(this), kFacing).align_to_plane(vector_at(this, 0x14F0));
    }
    flags = get_at<void*>(this, kTimedFlags);
    if (get_at<uint8_t>(flags, 0x78)) {
        if (velocity.y < 0.0f && vector_at(object_of(this), kPosition).y < get_at<float>(this, 0x37C))
            vector_at(flags, 0xE4).set_length(0.1f);
        else
            velocity += vector_at(flags, 0xE4);
    }

    // What the skater rides on (+0x152C's +0x18, if it's still under the skater) carries it along.
    maths::Vec4 carried = {0.0f, 0.0f, 0.0f, 1.0f};
    {
        void* ride = get_at<void*>(this, kRide);
        uint32_t* rider = get_at<uint32_t*>(ride, 0x18);
        void* object = object_of(this);
        if (rider) {
            if (*rider == 0)
                stop_riding(ride);
            else if (!reinterpret_cast<objects::RideTracker*>(rider)->tick_frame(&vector_at(object, kPosition)))
                stop_riding(ride);
            else if (!bit_mark(this, 0x78) || get_at<uint8_t>(this, 0x14C0))
                carried = vector_at(get_at<void*>(get_at<void*>(this, kRide), 0x18), 0x5C);
        }
    }

    // Move: position += velocity * t + (0, gravity, 0, 1) * t * t / 2, then the carried offset; velocity +=
    // (0, gravity, 0, 1) * t. (Rounded as the original: x's velocity product and the z, w sums stay unrounded.)
    {
        float t = get_at<float>(this, 0x8C);
        double half = static_cast<double>(t) * t * 0.5f;
        float zero_half = static_cast<float>(0.0 * half);
        float half_gravity = static_cast<float>(half * gravity);
        float half_float = static_cast<float>(half);
        float ty = t * velocity.y, tz = t * velocity.z, tw = t * velocity.w;
        float sx = static_cast<float>(static_cast<double>(velocity.x) * t + zero_half);
        float sy = ty + half_gravity;
        double sz = 0.0 * half + tz;
        double sw = static_cast<double>(tw) + half_float;
        maths::Vec4& position = vector_at(object_of(this), kPosition);
        position.x = sx + position.x;
        position.y = sy + position.y;
        position.z = static_cast<float>(sz + position.z);
        position.w = static_cast<float>(sw + position.w);
        vector_at(object_of(this), kPosition) += carried;

        float tt = get_at<float>(this, 0x8C);
        float zero = 0.0f * tt;
        float fall = tt * gravity;
        maths::Vec4& v = vector_at(object_of(this), kVelocity);
        v.x += zero;
        v.y += fall;
        v.z += zero;
        v.w += tt;
    }

    void* object = object_of(this);
    flags = get_at<void*>(this, kTimedFlags);
    if (get_at<uint8_t>(flags, 0x48)) {
        if (get_at<uint8_t>(flags, 0x38) && get_at<uint8_t>(flags, 0x30)) {
            // On a wall: a feeler from the skater's up (+1) to 23 below; off it, break out of vert.
            const maths::Vec4& up = vector_at(object, kUp);
            const maths::Vec4& position = vector_at(object, kPosition);
            maths::Vec4& start = vector_at(this, kProbeOrigin);
            maths::Vec4& end = vector_at(this, kProbeTip);
            float x = up.x + position.x, y = up.y + position.y, z = up.z + position.z, w = up.w + position.w;
            start.z = z, start.w = w, start.x = x, start.y = y;
            float ey = up.y * -23.0f, ez = up.z * -23.0f, ew = up.w * -23.0f;
            x = static_cast<float>(static_cast<double>(up.x) * -23.0f + position.x);
            y = ey + position.y, z = ez + position.z, w = ew + position.w;
            end.z = z, end.w = w, end.x = x, end.y = y;
            if (!probe_touches_zone(0x10, 0)) {
                break_vert(false);
                int wait = script::fetch_int_value(0x6BB5B751);
                if (time_since_release(input + kButton20) > wait && time_since_press(input + kButton20) > wait)
                    set_timed_flag(get_at<void*>(this, kTimedFlags), 0x48, 0);
            }
        }
        uint32_t stamp = get_at<uint32_t>(get_at<void*>(this, kTimedFlags), 0x4C);
        uint32_t now = static_cast<uint32_t>(frame_clock::game_clock_elapsed());
        int elapsed = static_cast<int>(now + (static_cast<uint32_t>(objects::g_clock_offset) - stamp));
        if (elapsed > script::fetch_int_value(0x26495947))
            set_timed_flag(get_at<void*>(this, kTimedFlags), 0x48, 0);
    } else if (get_at<uint8_t>(flags, 0x30)) {
        // (The original: both triggers. PartyMod's JZ -> JNZ at 0x0050CF89 makes it either.)
        if ((triggers_held(input) || get_at<uint8_t>(this, 0x2D0)) && !get_at<uint8_t>(flags, 0x78) &&
            vector_at(object, kVelocity).y > 0.0f)
            break_vert(false);
    }

    // Along a wall (+0x278: its normal; +0x26C: the height so far): a feeler across it, stepped down until it
    // hits.
    bool riding = false;
    {
        void* ride = get_at<void*>(this, kRide);
        uint32_t* rider = get_at<uint32_t*>(ride, 0x18);
        if (rider && *rider)
            riding = true;
        else if (rider)
            stop_riding(ride);
    }
    if (!riding && bit_mark(this, 0x38) && bit_mark(this, 0x30)) {
        maths::Vec4& start = vector_at(this, kProbeOrigin);
        maths::Vec4& end = vector_at(this, kProbeTip);
        start.x = vector_at(object_of(this), kPosition).x;
        start.y = get_at<float>(this, 0x26C);
        start.z = vector_at(object_of(this), kPosition).z;
        end = start;
        const maths::Vec4& wall = vector_at(this, kWallFaceVector);
        float wz = wall.z * 30.0f, ww = wall.w * 30.0f;
        start.x = static_cast<float>(static_cast<double>(wall.x) * 30.0f + start.x);
        start.y = static_cast<float>(static_cast<double>(wall.y) * 30.0f + start.y);
        start.z = wz + start.z;
        start.w = ww + start.w;
        end.x = static_cast<float>(end.x - static_cast<double>(wall.x) * 30.0f);
        end.y = static_cast<float>(end.y - static_cast<double>(wall.y) * 30.0f);
        end.z -= wz;
        end.w -= ww;
        float& step = get_at<float>(this, 0x298);
        bool found = false;
        if (step > 0.5f) {
            start.y = step + start.y;
            end.y = end.y + step;
            if (probe_touches_zone(0, 8)) {
                found = true;
            } else {
                start.y -= step;
                end.y -= step;
                step *= 0.5f;
            }
        }
        if (!found && probe_touches_zone(0, 8))
            found = true;
        for (int i = 0; !found && i < 10; ++i) {
            start.y -= 3.0f;
            end.y -= 3.0f;
            if (probe_touches_zone(0, 8)) {
                get_at<float>(this, 0x26C) = vector_at(this, kHitPoint).y;
                found = true;
            }
        }
        const maths::Vec4& point = vector_at(this, kHitPoint);
        const maths::Vec4& normal = vector_at(this, kHitNormal);
        if (found && std::sqrt(std::fabs(static_cast<double>(wall.z) * normal.z +
                                         static_cast<double>(wall.x) * normal.x)) > 0.02f) {
            // Still along the wall: stay on it (not below the height so far); its normal is the new wall normal,
            // and the skater is pushed off it by a script value.
            float height = point.y < get_at<float>(this, 0x26C) ? get_at<float>(this, 0x26C) : point.y;
            float hz = point.z, hw = point.w;
            get_at<float>(this, 0x268) = point.x;
            get_at<float>(this, 0x274) = hw;
            get_at<float>(this, 0x26C) = height;
            get_at<float>(this, 0x270) = hz;
            vector_at(object_of(this), kPosition).x = point.x;
            vector_at(object_of(this), kPosition).z = hz;
            maths::Vec4& new_wall = vector_at(this, kWallFaceVector);
            new_wall = normal;
            float push = script::fetch_float(0x78384871);
            maths::Vec4& position = vector_at(object_of(this), kPosition);
            float py = push * new_wall.y, pz = push * new_wall.z, pw = push * new_wall.w;
            position.x = static_cast<float>(static_cast<double>(push) * new_wall.x + position.x);
            position.y += py;
            position.z += pz;
            position.w += pw;
            maths::Vec4 flat = {new_wall.x, 0.0f, new_wall.z, new_wall.w};
            flat.make_unit();
            turn_to_normal(flat);
            vector_at(object_of(this), kVelocity).align_to_plane(flat);
        } else {
            set_timed_flag(get_at<void*>(this, kTimedFlags), 0x38, 0);
        }
    }

    {
        uint8_t* other = get_at<uint8_t*>(this, kOtherFlags);
        if (!bit_mark(this, 0x30) || bit_mark(this, 0x78) || get_at<uint8_t>(this, 0x2DB) || other[0x28] || other[0x50])
            get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
        else
            update_ground_normal();
    }
    if (bike) // (not original: THAW 0x005E0476 - on the bike the wheel points follow it through the air)
        thaw_bmx::air_wheel_points(this);

    // Landing: a feeler from the last position (carried along) to the new one.
    auto last_position = [&] { return carried + vector_at(object_of(this), kOldPosition); };
    maths::Vec4 from = last_position();
    bool stopped = check_air_forward_collision(&from);
    int state = get_at<int>(get_at<void*>(this, kTimedFlags), 0x18);
    if (state == 2 || state == 5)
        return;
    vector_at(this, kProbeOrigin) = last_position();
    vector_at(this, kProbeTip) = vector_at(object_of(this), kPosition);
    if (!stopped && probe_touches_zone(0x10, 0)) {
        bool steep = vector_at(this, kHitNormal).y < 0.1f;
        from = last_position();
        // (Not yet, while something above says so - fast or steep, and the ride's timer past 500.)
        bool not_yet =
            step_up(&from) && (vector_at(object_of(this), kVelocity).y > 10.0f || steep) &&
            static_cast<uint32_t>(
                static_cast<objects::MovingSurfaceModule*>(get_at<void*>(this, kRide))->time_since_stop()) > 500;
        if (!not_yet) {
            maths::Vec4 normal = vector_at(this, kHitNormal);
            vector_at(object_of(this), kPosition) = vector_at(this, kHitPoint);
            vector_at(object_of(this), kPosition) += normal;
            if (get_at<uint8_t>(this, kHitSkatable)) {
                land_from_air();
                return;
            }
            // Not something to land on: turn along it and push off it.
            vector_at(object_of(this), kVelocity).align_to_plane(normal);
            vector_at(object_of(this), kFacing).align_to_plane(normal);
            get_at<maths::Mat4>(object_of(this), kMatrix).square_up_axes(2);
            get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
            float push = tuning_float_lookup(0x23410C14, object);
            vector_at(object_of(this), kPosition) += normal * push;
            set_timed_flag(get_at<void*>(this, kTimedFlags), 0xB0, 1);
        }
    } else if (!bit_mark(this, 0x30)) {
        test_lateral_hits();
    }

    if (keep_off_ceiling())
        set_timed_flag(get_at<void*>(this, kTimedFlags), 0xB0, 1);
    jump_released();
    vert_wallplant();
    if (bit_mark(this, 0xB0))
        return;
    // An acid drop: both triggers (the original; PartyMod's JZ -> JNZ at 0x0050DA64 makes it either).
    if (!triggers_held(input))
        return;
    bool recent = false;
    if (get_at<uint32_t>(this, 0x380) > get_at<uint32_t>(this, 0x4EC))
        recent = static_cast<uint32_t>(frame_clock::game_clock_time_since(get_at<int>(this, 0x380))) < 0xFA;
    uint8_t drop[40];
    void* o = object_of(this);
    if (find_acid_drop(recent, &vector_at(o, kPosition), &vector_at(o, kOldPosition), &vector_at(o, kVelocity),
                       reinterpret_cast<AcidDrop*>(drop)))
        start_acid_drop(reinterpret_cast<const AcidDrop*>(drop));
}

// (Part of 0x0050CA00: landing on something skatable.)
void SkaterPhysicsModule::land_from_air()
{
    set_physics_state(0);
    start_riding_hit_object();
    void* flags = get_at<void*>(this, kTimedFlags);
    if (get_at<uint8_t>(flags, 0x30) || get_at<uint8_t>(flags, 0x78)) {
        get_at<uint8_t>(this, 0x373) = 1;
        get_at<uint8_t>(this, 0x374) = 1;
    } else {
        get_at<uint8_t>(this, 0x374) = 0;
    }
    get_at<uint8_t>(this, 0x372) = get_at<uint8_t>(flags, 0x78);
    set_surface_kind(get_at<uint8_t>(this, kSurfaceFlags));
    if (!get_at<uint8_t>(this, 0x2ED)) {
        uint32_t which = bit_mark(this, 0xE1);
        void* object = object_of(this);
        float speed = vector_at(object, kVelocity).len_of();
        float scale = static_cast<objects::Rider*>(object)->get_scaled_physics(0xCC5F87AA);
        static_cast<objects::RiderSfxPlayer*>(get_at<void*>(this, kOtherComponent))
            ->play_land(speed / scale, static_cast<int>(which));
    }
    void* skater = get_at<void*>(this, kSkaterComponent);
    if (!get_at<uint8_t>(skater, 0x13B0))
        static_cast<objects::MoveExecutor*>(skater)->set_trick_objects(false);
    remember_floor_probe(&get_at<uint8_t>(this, kFeeler));
    trigger_of(this)->probe_touches_zone(kSkaterEvents0064bB98, feeler_at(this, kOtherFeeler));
    if (!get_at<uint8_t>(get_at<uint8_t*>(this, kTriggerState), 0x26)) {
        const maths::Vec4& normal = vector_at(this, kHitNormal);
        maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
        uint8_t* input = get_at<uint8_t*>(this, kInput);
        if (!get_at<uint8_t>(this, 0x374) && !get_at<uint8_t>(this, 0x373) && velocity.x == 0.0f &&
            velocity.z == 0.0f && input[kButton40]) {
            // (Landing straight down with button +0x40 held: no bounce.)
            get_at<uint32_t>(&velocity, 4) = 0;
        } else {
            if (!bit_mark(this, 0x78)) {
                velocity.flatten_onto_plane(normal);
            } else {
                // Landing from a wall ride: along +0x490's direction in the ground's plane, unless that turns
                // the skater upwards (0x004FF590) - then the old velocity in the plane; at least a script speed.
                maths::Vec4 saved = velocity;
                velocity.change_heading(vector_at(this, 0x490));
                velocity.align_to_plane(normal);
                maths::Vec4& v = vector_at(object_of(this), kVelocity);
                maths::Vec4 probe = v;
                get_along_facing(&probe);
                if (probe.y > 0.0f) {
                    v = saved;
                    vector_at(object_of(this), kVelocity).flatten_onto_plane(normal);
                }
                float least = script::fetch_float(0x59484878);
                if (static_cast<double>(least) * least > vector_at(object_of(this), kVelocity).magnitude_squared())
                    vector_at(object_of(this), kVelocity).set_length(script::fetch_float(0x59484878));
            }
            vector_at(object_of(this), kVelocity).clamp_tiny_to_nothing(10.0f);
        }
        set_timed_flag(get_at<void*>(this, kTimedFlags), 0x30, 0);
        get_at<uint8_t>(this, 0x2D0) = 0;
        if (thaw_bmx::live_now(this)) {
            // Not original: THAW 0x005E0AA4 - on the bike the landing keeps the
            // bike's own up as the display / ground normal, the display matrix the skater's as it landed, and then
            // sets the ground normal to the hit's (0x004FE860: the skater's up turned to it, the display normal's
            // lerp started) - the bike eases into the landing instead of snapping to the ground.
            const maths::Vec4 up = vector_at(object_of(this), kUp);
            const maths::Vec4 hit = normal;
            vector_at(this, 0x58) = up;
            vector_at(this, kFloorUpVector) = up;
            vector_at(this, 0x78) = vector_at(this, 0x58);
            find_nearest_wall_side();
            get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
            turn_to_normal(hit);
            static_cast<objects::Entity*>(object_of(this))->post_own_event(0x532B16EF, nullptr);
            return;
        }
        maths::Vec4& ground = vector_at(this, 0x58);
        ground = normal;
        vector_at(this, kFloorUpVector) = ground;
        vector_at(this, 0x78) = ground;
        find_nearest_wall_side();
        vector_at(object_of(this), kUp) = ground;
        get_at<maths::Mat4>(object_of(this), kMatrix).square_up_axes(1);
        get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    }
    static_cast<objects::Entity*>(object_of(this))->post_own_event(0x532B16EF, nullptr);
}

} // namespace skater
