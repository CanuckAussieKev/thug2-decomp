// The skater's adjust-physics component (adjust_physics.h). From the disassembly of 0x004F8420.

#include "game/rider/adjust_physics.h"

#include <cstdint>

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

namespace objects {

namespace {

// The core physics component's fields read here.
constexpr uint32_t kFirstTick = 0x98; // the position as the frame began
constexpr uint32_t kFeeler = 0xA8;    // its feeler (the last ground hit)
constexpr uint32_t kHitPoint = 0x120;
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kProbeOrigin = 0x208;
constexpr uint32_t kProbeTip = 0x218;
constexpr uint32_t kTimedFlags = 0x1528; // the skater's state component (+0x18 the physics state)
// The skater object's.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kUp = 0x7C;
constexpr uint32_t kVelocity = 0xAC;
// The state component's: the height over the ground the skater stands on.
constexpr uint32_t kHeightOverGround = 0x11C;

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
constexpr float kUberFrigOffsets[5][4] = {
    {0.0f, 0.0f, 0.0f, 1.0f},  {0.1f, 0.1f, 0.1f, 1.0f}, {-0.2f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -0.2f, 1.0f}, {0.2f, 0.0f, 0.0f, 1.0f},
};

template <typename T>
T& get_at(const void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
maths::Vec4& vector_at(const void* base, uint32_t offset) { return get_at<maths::Vec4>(base, offset); }

int state_of(void* core) { return get_at<int>(get_at<void*>(core, kTimedFlags), 0x18); }
void change_phase(void* core, int state) { static_cast<skater::SkaterPhysicsModule*>(core)->set_physics_state(state); }

} // namespace

// 0x004F8420
void RiderPhysicsTweaks::keep_above_ground()
{
    void* core = core_ptr_;
    void* object = entity_;
    uber_frigged_ = 0;
    maths::Vec4& position = vector_at(object, kPosition);
    const maths::Vec4& frame_start = vector_at(core, kFirstTick);
    if (position.x == frame_start.x && position.y == frame_start.y && position.z == frame_start.z &&
        position.w == frame_start.w)
        return; // (not moved this frame)

    // The ground below: a feeler from just over the skater (8 up) to uber_frig_current_height feet under it.
    maths::Vec4& start = vector_at(core, kProbeOrigin);
    maths::Vec4& end = vector_at(core, kProbeTip);
    start = position;
    end = position;
    const maths::Vec4& up = vector_at(object, kUp);
    start.x = static_cast<float>(static_cast<double>(up.x) * kUpNudge + start.x);
    start.y = static_cast<float>(static_cast<double>(up.y) * kUpNudge + start.y);
    start.z = static_cast<float>(static_cast<double>(up.z) * kUpNudge + start.z);
    start.w = static_cast<float>(static_cast<double>(up.w) * kUpNudge + start.w);
    start.y = static_cast<float>(static_cast<double>(start.y) + kFeelerAbove);
    end.y = static_cast<float>(static_cast<double>(end.y) -
                               static_cast<double>(script::fetch_float(kUberFrigCurrentHeight)) * kFeet);
    // Not original: THAW 0x005C2820 - on the bike the feeler starts higher (thaw_bmx_physics.cpp).
    float raise = skater::thaw_bmx::live_now(core) ? skater::thaw_bmx::stuck_check_raise(core) : 0.0f;
    if (raise != 0.0f)
        start.y += raise;
    auto* physics = static_cast<skater::SkaterPhysicsModule*>(core);
    uint8_t* park_editor = reinterpret_cast<uint8_t*>(static_cast<void*>(park_editing::g_park_editor));

    if (!physics->probe_touches_zone(0x10, 0)) {
        // Nothing at all: the uber frig (not in the park editor's play while in states 3 / 4): back where the frame
        // began (a little off it each try), going the other way; out of the air states, into the air.
        int state = state_of(core);
        if (!((state == 4 || state == 3) && get_at<uint32_t>(park_editor, 0x40) == kParkEditorTestPlay)) {
            uber_frigged_ = 1;
            if (uber_frig_tries_ < 5) {
                const float* offset = kUberFrigOffsets[uber_frig_tries_++];
                const maths::Vec4& from = vector_at(core, kFirstTick);
                position = {offset[0] + from.x, from.y + offset[1], from.z + offset[2], from.w + offset[3]};
            } else {
                uber_frig_tries_ = 0;
            }
            maths::Vec4& velocity = vector_at(object, kVelocity);
            velocity = {-velocity.x + kNudgeXy, -velocity.y + kNudgeXy, -velocity.z + kNudgeZ, -velocity.w + kOne};
            state = state_of(core);
            if (state != 0 && state != 1)
                change_phase(core, 1);
        }
        get_at<float>(state_ref_, kHeightOverGround) = 0.0f;
        return;
    }

    // Ground. Standing on a moving object (the skater on the ground or in the air, not skitching (+0x88) nor
    // wallriding (+0x78)) that isn't what the feeler hit: off it, with its velocity.
    uint8_t* flags = get_at<uint8_t*>(core, kTimedFlags);
    if (!flags[0x88] && !flags[0x78] && ride_->ride_) {
        RideTracker* ride = ride_->ride_;
        bool leave = true;
        if (ride->entity_.owner_object_) {
            int state = get_at<int>(flags, 0x18);
            if (state == 0 || state == 1) {
                render::ProbeRay& feeler = get_at<render::ProbeRay>(core, kFeeler);
                if (feeler.hit_object_alive() && feeler.get_hit_object() == ride->entity_.owner_object_)
                    leave = false;
            } else {
                leave = false;
            }
            if (leave)
                vector_at(object, kVelocity) += vector_at(ride_->ride_->entity_.owner_object_, kVelocity);
        }
        if (leave)
            ride_->stop_riding();
    }

    // Kept just over the ground.
    {
        double over = static_cast<double>(position.y) - vector_at(core, kHitPoint).y;
        get_at<float>(state_ref_, kHeightOverGround) = static_cast<float>(over);
        if (over < kLeastHeight)
            position.y = static_cast<float>(static_cast<double>(vector_at(core, kHitPoint).y) + kLeastHeight);
    }
    shadow_handle_->change_place(&vector_at(core, kHitPoint));
    shadow_handle_->set_normal(&vector_at(core, kHitNormal));
    if (state_of(core) != 4)
        get_update_brightness(&get_at<render::ProbeRay>(core, kFeeler));

    if (get_at<uint32_t>(park_editor, 0x40) != kParkEditorTestPlay) {
        // Under the floor: a feeler from far above down to a unit over the skater (faces flagged 0x800 - "under ok" -
        // passed over) hits a floor. Within 6 of it: onto it. Further, moved this frame: back where the frame began
        // at half the speed the other way (a lip: out of it, into the air, 2 up); hardly moved: onto it.
        render::ProbeRay feeler;
        feeler.make_new();
        feeler.set_excluded(0x810, 0);
        maths::Vec4 from = {position.x, static_cast<float>(static_cast<double>(position.y) + kFloorProbeHeight),
                            position.z, position.w};
        feeler.change_origin(&from);
        maths::Vec4 to = {up.x + position.x, position.y + up.y, position.z + up.z, position.w + up.w};
        // Not original: on the bike as far up as the ground check's start (thaw_bmx_physics.cpp stuck_check_raise).
        if (raise != 0.0f)
            to.y += raise;
        feeler.change_finish(&to);
        if (feeler.get_hit_shape(true, false)) {
            maths::Vec4 off = {position.x - feeler.contact_pos_.x, position.y - feeler.contact_pos_.y,
                               position.z - feeler.contact_pos_.z, position.w - feeler.contact_pos_.w};
            double near = static_cast<double>(off.x) * off.x + static_cast<double>(off.y) * off.y +
                          static_cast<double>(off.z) * off.z;
            if (near < kFloorNear) {
                position = feeler.contact_pos_;
            } else {
                const maths::Vec4& began = vector_at(core, kFirstTick);
                maths::Vec4 moved = {position.x - began.x, position.y - began.y, position.z - began.z,
                                     position.w - began.w};
                double moved_sqr = static_cast<double>(moved.x) * moved.x + static_cast<double>(moved.y) * moved.y +
                                   static_cast<double>(moved.z) * moved.z;
                position = moved_sqr < kOne ? feeler.contact_pos_ : began;
                maths::Vec4& velocity = vector_at(object, kVelocity);
                velocity = {kBounce * velocity.x, kBounce * velocity.y, kBounce * velocity.z, kBounce * velocity.w};
                if (state_of(core) == 4) {
                    change_phase(core, 1);
                    static_cast<Entity*>(object)->post_own_event(kUnderTheFloor, nullptr);
                    position.y = static_cast<float>(static_cast<double>(position.y) + kLift);
                }
            }
        }
        feeler.entity_.get_release();
    }
    uber_frig_tries_ = 0;
    vector_at(core, kFirstTick) = position;
}

} // namespace objects
