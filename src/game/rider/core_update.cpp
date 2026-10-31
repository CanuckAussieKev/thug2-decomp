// The skater's core physics component: the frame's update (the state's own update and what runs around it).

#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "game/rider/collision_events.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/thaw_bmx.h"
#include "platform/stopwatch.h"
#include "engine/render/probe_ray.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kTouchedDownNow = 0x254;   // set when the air's update lands the skater on the ground
constexpr uint32_t kFrameFlag255 = 0x255;      // the timed flags' +0x78 when the frame began
constexpr uint32_t kFrameBeganInLip = 0x256;
constexpr uint32_t kGroundStart = 0x338;       // the position the ground's update starts from
constexpr uint32_t kAirFromJump = 0x4F0;       // in the air with +0x30 set and +0x78 not (told by event when it changes)
constexpr uint32_t kAirborneClock = 0x14CC;         // (cleared whenever the skater is out of the air)

constexpr uint32_t kEnteredAir = 0xF225FE69;   // the events sent when +0x4F0 changes (names unknown)
constexpr uint32_t kLeftAir = 0x5E27200A;

} // namespace

// 0x00510830
void SkaterPhysicsModule::tick_frame()
{
    get_at<float>(this, kFrameLength) = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    collision_events::checkpoint(this, "update-start"); // (not original)
    bool bike = thaw_bmx::live_now(this); // (not original: THAW's bike, thaw_bmx_physics.cpp)
    if (bike)
        thaw_bmx::begin_frame(this);
    get_at<uint8_t>(this, kTouchedDownNow) = 0;
    get_at<uint8_t>(this, kFrameBeganInLip) = state_of(this) == kLip;
    get_at<uint8_t>(this, kFrameFlag255) = bit_mark(this, 0x78);
    if (!bit_mark(this, 0x20) && input_of(this)[0x1C0])
        set_flag(this, 0x20, 1);
    clamp_skater_speed();
    set_flag(this, 0x98, 0);
    set_flag(this, 0xA0, 0);
    refresh_hit_cache();
    update_model_rotation();

    switch (state_of(this)) {
    case kGround:
        vector_at(this, kGroundStart) = vector_at(object_of(this), kPosition);
        do_ground_physics();
        try_start_skitch();
        break;
    case kAir:
        do_air_physics();
        if (state_of(this) == kGround)
            get_at<uint8_t>(this, kTouchedDownNow) = 1;
        break;
    case kWallRide:
        step_wall_ride();
        break;
    case kLip:
        step_coping_move();
        break;
    case kRail:
        step_grind_move();
        break;
    case kWallplant:
        step_wallplant();
        break;
    case kBikeStall: // (not original: THAW's stall state - only THAW's bike gets into it, thaw_bmx.h)
        if (thaw_bmx::is_switched_on())
            do_bike_stall_physics();
        break;
    default:
        break;
    }

    collision_events::checkpoint(this, "state-physics"); // (not original)
    update_speed_override();
    collision_events::checkpoint(this, "after-4FF200"); // (not original)
    try_grind(false); // (inlined in the original)
    collision_events::checkpoint(this, "try-grind"); // (not original)
    update_free_revert();
    collision_events::checkpoint(this, "free-revert"); // (not original)
    update_focus_button();
    update_projectiles();
    update_special_time();
    collision_events::checkpoint(this, "update-rest"); // (not original)

    uint8_t in_air = state_of(this) == kAir && bit_mark(this, 0x30) && !bit_mark(this, 0x78);
    uint8_t& air_from_jump = get_at<uint8_t>(this, kAirFromJump);
    if (air_from_jump != in_air) {
        air_from_jump = !air_from_jump;
        static_cast<objects::Entity*>(object_of(this))->announce(air_from_jump ? kEnteredAir : kLeftAir, nullptr, 0.0f);
    }
    if (state_of(this) != kAir)
        get_at<int>(this, kAirborneClock) = 0;
    if (bike && thaw_bmx::live_now(this))
        thaw_bmx::end_frame(this); // (not original: THAW 0x005C9210, the x-up let go)
    reinterpret_cast<void*&>(render::g_current_cache) = nullptr;
    collision_events::end_frame(this); // (not original: [debug] collision_events)
}

} // namespace Skater
