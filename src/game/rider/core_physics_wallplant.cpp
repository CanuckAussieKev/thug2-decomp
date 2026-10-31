// The skater's wall plants and the start of a wall ride.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/core/dbg.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/entity.h"
#include "engine/objects/trigger_component.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/tagging.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "platform/game_clock.h"
#include "engine/objects/trigger_events.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kCanWallplant = 0x5310E7B5;          // canwallplant (a tag: a vehicle that may)
constexpr uint32_t kDisallowRewallplant = 0x82135DD7;   // Physics_Disallow_Rewallplant_Duration (ms)
constexpr uint32_t kWallplantDelay = 0x545840C4;        // Physics_Wallplant_Delay (seconds)
constexpr uint32_t kMinWallplantHeight = 0xD5349CC6;    // Physics_Min_Wallplant_Height
constexpr uint32_t kWallplantTrick = 0x5D1F84A7;        // Wallplant_Trick (the trick names that plant)
constexpr uint32_t kLateWallplantFrames = 0x3B389187;   // Physics_late_wallplant_frames
constexpr uint32_t kForwardHeight = 0xD4205C9B;         // Skater_First_Forward_Collision_Height
constexpr uint32_t kForwardLength = 0x20102726;         // Skater_First_Forward_Collision_Length
constexpr uint32_t kMinApproachAngle = 0x8F79CC1C;      // Physics_Wallplant_Min_Approach_Angle (degrees)
constexpr uint32_t kSpeedLoss = 0x09B2D4A3;             // Physics_Wallplant_Speed_Loss
constexpr uint32_t kMinExitSpeed = 0x7CEE396C;          // Physics_Wallplant_Min_Exit_Speed
constexpr uint32_t kWalkingWallplantSpeed = 0x5F768D84; // Physics_Walking_Wallplant_Speed
constexpr uint32_t kVerticalExitSpeed = 0x074957FA;     // Physics_Wallplant_Vertical_Exit_Speed
constexpr uint32_t kDistanceFromWall = 0x024BE8F0;      // Physics_Wallplant_Distance_From_Wall
constexpr uint32_t kWallplant = 0xCF74F6B7;             // Wallplant (an event)
constexpr uint32_t kTriangle = 0x20689278;              // triangle (a button)
constexpr uint32_t kWallRideWindow = 0x5D241B00;        // Wall_Ride_Triangle_Window (seconds)
constexpr uint32_t kWallRideDelay = 0xF0C74EC2;         // Wall_Ride_Delay (seconds)
constexpr uint32_t kWallRideMinSpeed = 0xF0636A67;      // Wall_Ride_Min_Speed
constexpr uint32_t kUpsideDownAngle = 0xAE23E850;       // Wall_Ride_Upside_Down_Angle (degrees)
constexpr uint32_t kMaxIncidentAngle = 0x6AC9F64D;      // Wall_Ride_Max_Incident_Angle (degrees)
constexpr uint32_t kMaxTilt = 0xBFCBB7E8;               // Wall_Ride_Max_Tilt (degrees)
constexpr uint32_t kWallRideRight = 0x51372712;         // WallRideRight (an event)
constexpr uint32_t kWallRideLeft = 0x5DE19C83;          // WallRideLeft (an event)

constexpr uint32_t kDriving = 0x2ED;
constexpr uint32_t kModelRotating = 0x2DB;
constexpr uint32_t kHitObjectChecksum = 0x100;
constexpr uint32_t kWallplantTime = 0x4E0;
constexpr uint32_t kWallRideStartTime = 0x39C;
constexpr uint32_t kWallplantEndTime = 0x3A0;
constexpr uint32_t kWallDirection = 0x38C;  // the wall ridden (its normal)
constexpr uint32_t kLateFrames = 0x14CC;    // frames left for a late wall plant
constexpr uint32_t kSavedVelocity = 0x14D0; // (when the plant was missed)
constexpr uint32_t kSavedPosition = 0x14E0;
constexpr uint32_t kSavedWallNormal = 0x14F0;
constexpr uint32_t kTakeOffHeight = 0x11C; // (in the flags block)

constexpr float kMinUp = 0.1f;                             // 0x00645D74
constexpr float kToMilliseconds = 1000.0f;                 // 0x006453C0
constexpr float kUnsignedOffset = 4294967296.0f;           // 0x006453B4
constexpr float kMaxTilt01 = 0.1f;                         // 0x00645D74
constexpr float kBackOff = 24.0f;                          // 0x00645EE8
constexpr float kMinSpeed = 0.01f;                         // 0x0064544C
constexpr float kRadiansPerDegree = 0.0174532924f;         // 0x00645464
constexpr float kRiseScale = 0.15f;                        // 0x0064BD4C
constexpr float kProbe = 6.0f;                             // (0x40C00000)
constexpr float kWalkReach = 36.0f;                        // (0x42100000)
constexpr float kMinFlatSpeed = 0.0001f;                   // 0x006453D4
constexpr float kFromWall = 0.1f;                          // (0x3DCCCCCD)
const char* const kLateWallplantText = "late wallplant\n"; // "late wallplant\n"

objects::GameEntity* skater_object(void* component) { return static_cast<objects::GameEntity*>(object_of(component)); }

void check_triggers(void* component, const int* kind, uint32_t feeler)
{
    static_cast<objects::ActivationZoneModule*>(get_at<void*>(component, kActivationZoneSlot))
        ->probe_touches_zone(kind, &get_at<render::ProbeRay>(component, feeler));
}

bool trigger_took_over(void* component)
{
    return get_at<uint8_t>(get_at<void*>(component, kStateModuleSlot), 0x26) != 0;
}

// The trick button `button` pressed now, or within Wall_Ride_Triangle_Window (0x0051ECF0 / 0x0051F6B0 on the
// skater component).
bool button_pressed(void* component, uint32_t button)
{
    auto* skater = static_cast<objects::MoveExecutor*>(get_at<void*>(component, kSkaterComponent));
    if (skater->is_key_held(button))
        return true;
    uint32_t window = ftol(static_cast<double>(script::fetch_float(kWallRideWindow)) * kToMilliseconds);
    return static_cast<objects::MoveExecutor*>(get_at<void*>(component, kSkaterComponent))
        ->find_press(button, window, 0xFFFFFFFFu);
}

// Moving up the wall: whether there's wall 6 either side of the hit point raised by 0.15 of the upward speed (along
// the wall).
bool wall_above_hit(void* component)
{
    maths::Vec4 point = vector_at(component, kHitPoint);
    maths::Vec4 normal = vector_at(component, kHitNormal);
    maths::Vec4 rise = {0.0f, vector_at(object_of(component), kVelocity).y * kRiseScale, 0.0f, 1.0f};
    rise.align_to_plane(normal);
    point += rise;
    maths::Vec4 end = point - normal * kProbe;
    maths::Vec4 start = point + normal * kProbe;
    render::ProbeRay feeler;
    feeler.make_new(&start, &end);
    return feeler.get_hit_shape(true, false) != 0;
}

} // namespace

// 0x00507500
bool SkaterPhysicsModule::wall_plant(bool late, bool check_only, bool walking)
{
    if (get_at<uint8_t>(this, kDriving)) {
        int allowed = 0;
        if (!skater_object(this)->read_int_tag(kCanWallplant, &allowed))
            return false;
    }
    if (get_at<uint8_t>(this, kModelRotating) || get_at<uint8_t>(get_at<void*>(this, kStateModuleSlot), 0x24))
        return false;

    if (!walking) {
        void* object = object_of(this);
        if (vector_at(object, kUp).y < kMinUp)
            return false;
        int elapsed = static_cast<int>(get_since(get_at<uint32_t>(this, kWallplantTime)));
        double since_plant = elapsed;
        if (elapsed < 0)
            since_plant += kUnsignedOffset; // (unsigned)
        float since_plant_float = static_cast<float>(since_plant);
        if (script::fetch_float(kDisallowRewallplant) > since_plant_float)
            return false;
        float since_ride =
            static_cast<float>(frame_clock::game_clock_time_since(get_at<int>(this, kWallRideStartTime)));
        if (static_cast<double>(script::fetch_float(kWallplantDelay)) * kToMilliseconds > since_ride)
            return false;
        if (script::fetch_float(kMinWallplantHeight) > get_at<float>(flags_of(this), kTakeOffHeight))
            return false;
        // Not a steep enough wall: only for a late plant (unless just checking, or it's vert).
        if (!check_only && !(get_at<uint16_t>(this, kHitFlags) & 8) &&
            std::fabs(vector_at(this, kHitNormal).y) > kMaxTilt01 && !late)
            return false;
        // A wall plant trick pressed? (Not original: THAW 0x005DB1ED - on the bike square held plants without asking
        // the list, and the list is the bike's, bike_Wallplant_Trick.)
        bool bike = thaw_bmx::live_now(this);
        script::Array* tricks = script::fetch_list(bike ? thaw_bmx::wallplant_trick_list() : kWallplantTrick);
        bool pressed = bike && thaw_bmx::wallplant_by_square(this);
        for (uint32_t i = 0; !pressed && i < tricks->extent_; ++i) {
            uint32_t trick = tricks->fetch_part(i);
            if (static_cast<objects::MoveExecutor*>(get_at<void*>(this, kSkaterComponent))
                    ->check_trigger(reinterpret_cast<script::Struct*>(trick), 0, 0, true)) {
                pressed = true;
                break;
            }
        }
        if (!pressed) {
            if (late)
                return false;
            // Not yet: a few frames to press it late in (kept: the velocity and position; the skater backed off the
            // wall 24 the way it came).
            get_at<int>(this, kLateFrames) = script::fetch_int_value(kLateWallplantFrames);
            vector_at(this, kSavedVelocity) = vector_at(object_of(this), kVelocity);
            vector_at(this, kSavedPosition) = vector_at(object_of(this), kPosition);
            void* o = object_of(this);
            const maths::Vec4& v = vector_at(o, kVelocity);
            maths::Vec4 back = {-v.x, 0.0f, -v.z, v.w};
            back.make_unit();
            maths::Vec4& p = vector_at(o, kPosition);
            float y = back.y * kBackOff, z = back.z * kBackOff, w = back.w * kBackOff;
            p.z = z + p.z;
            p.w = w + p.w;
            p.x = static_cast<float>(static_cast<double>(back.x) * kBackOff + p.x);
            p.y = y + p.y;
            return false;
        }
        if (late) {
            // A late plant: back to where the skater was, and the wall looked for again along its move.
            debug::printf(kLateWallplantText);
            vector_at(object_of(this), kVelocity) = vector_at(this, kSavedVelocity);
            vector_at(object_of(this), kPosition) = vector_at(this, kSavedPosition);
            void* o = object_of(this);
            const maths::Vec4& position = vector_at(o, kPosition);
            const maths::Vec4& old = vector_at(o, kOldPosition);
            maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, position.w - old.w};
            move.make_unit();
            double height = tuning_float_lookup(kForwardHeight, o);
            const maths::Vec4& up = vector_at(o, kUp);
            float hw = static_cast<float>(height * up.w), hz = static_cast<float>(height * up.z),
                  hy = static_cast<float>(height * up.y), hx = static_cast<float>(height * up.x);
            const maths::Vec4& last = vector_at(object_of(this), kOldPosition);
            maths::Vec4& start = vector_at(this, kProbeOrigin);
            start = {hx + last.x, hy + last.y, hz + last.z, hw + last.w};
            double length = tuning_float_lookup(kForwardLength, object_of(this));
            float lw = static_cast<float>(move.w * length), lz = static_cast<float>(move.z * length),
                  ly = static_cast<float>(move.y * length), lx = static_cast<float>(length * move.x);
            const maths::Vec4& now = vector_at(object_of(this), kPosition);
            double w = static_cast<double>(hw) + now.w; // (left unrounded)
            float z = hz + now.z, y = hy + now.y, x = hx + now.x;
            maths::Vec4& end = vector_at(this, kProbeTip);
            end = {x + lx, y + ly, z + lz, static_cast<float>(w + lw)};
            if (!probe_touches_zone(0x10, 0))
                return false;
        }
    } else {
        // Walking: a feeler 36 the other way from the facing, at the forward collision height.
        void* object = object_of(this);
        const maths::Vec4& facing = vector_at(object, kForward);
        maths::Vec4 direction = {-facing.x, facing.y, -facing.z, facing.w};
        direction.make_unit();
        float height = tuning_float_lookup(kForwardHeight, object);
        maths::Vec4 lift = vector_at(object, kUp) * height;
        vector_at(this, kProbeOrigin) = vector_at(object_of(this), kPosition) + lift;
        maths::Vec4 reach = direction * kWalkReach;
        maths::Vec4 base = vector_at(object_of(this), kPosition) + lift;
        vector_at(this, kProbeTip) = base + reach;
        if (!probe_touches_zone(0x10, 0))
            return false;
    }

    if (check_only)
        return true;
    // The plant: off the wall (its normal flattened; a late plant's saved one) at the approach angle allowed.
    float speed = static_cast<float>(length_unrounded(vector_at(object_of(this), kVelocity)));
    if (speed < kMinSpeed)
        return false;
    maths::Vec4 heading = vector_at(object_of(this), kVelocity) / speed;
    heading.y = 0.0f;
    heading.make_unit();
    maths::Vec4 wall = vector_at(this, kHitNormal);
    if (late) {
        wall.x = vector_at(this, kSavedWallNormal).x;
        wall.z = vector_at(this, kSavedWallNormal).z;
        wall.w = vector_at(this, kSavedWallNormal).w;
    }
    wall.y = 0.0f;
    wall.make_unit();
    if (!walking) {
        double limit = x87_sin(static_cast<double>(script::fetch_float(kMinApproachAngle)) * kRadiansPerDegree);
        double approach = static_cast<double>(heading.x) * wall.x + static_cast<double>(heading.z) * wall.z +
                          static_cast<double>(wall.y) * heading.y;
        if (-limit < approach)
            return false;
    }
    if (vector_at(object_of(this), kVelocity).y > 0.0f && input_of(this)[0x1A0] && !wall_above_hit(this))
        return false;
    // (not original: THAW 0x005DBB9E - no sticker slap on the bike)
    if (!thaw_bmx::live_now(this))
        static_cast<objects::TaggingControlComponent*>(get_at<void*>(this, 0x1538))->slap_sticker(false, walking, late);
    vector_at(object_of(this), kVelocity).y = 0.0f;
    check_triggers(this, kSkaterEvents0064bBec, kFeeler);
    if (trigger_took_over(this))
        return true;

    // Bounced off the wall (the speed less Physics_Wallplant_Speed_Loss, at least the minimum exit speed), up at
    // the vertical exit speed.
    void* object = object_of(this);
    maths::Vec4& velocity = vector_at(object, kVelocity);
    float into =
        static_cast<float>(static_cast<double>(wall.z) * velocity.z + static_cast<double>(wall.y) * velocity.y +
                           static_cast<double>(wall.x) * velocity.x);
    maths::Vec4 bounce = into * wall;
    if (!walking) {
        velocity -= 2.0f * bounce;
        maths::Vec4& v = vector_at(object_of(this), kVelocity);
        float flat = static_cast<float>(std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.z) * v.z));
        if (flat > kMinFlatSpeed) {
            v.y = 0.0f;
            float reduced = static_cast<float>(flat - static_cast<double>(script::fetch_float(kSpeedLoss)));
            float least = script::fetch_float(kMinExitSpeed);
            set_length(vector_at(object_of(this), kVelocity), objects::larger_of(least, reduced));
        } else {
            vector_at(object_of(this), kVelocity) = -script::fetch_float(kMinExitSpeed) * heading;
        }
    } else {
        vector_at(object_of(this), kVelocity) = wall * script::fetch_float(kWalkingWallplantSpeed);
    }
    vector_at(object_of(this), kVelocity).y = script::fetch_float(kVerticalExitSpeed);

    // Riding what was hit (if it's an object still there).
    auto* feeler = reinterpret_cast<render::ProbeRay*>(&get_at<uint8_t>(this, kFeeler));
    if (feeler->hit_object_alive()) {
        bool riding = get_at<objects::MovingSurfaceModule*>(this, kRide)->is_riding();
        if (!riding || *get_at<void**>(get_at<void*>(this, kRide), 0x18) != feeler->get_hit_object()) {
            get_at<objects::MovingSurfaceModule*>(this, kRide)->stop_riding();
            get_at<walk::MovableContact*>(this, kRide)->start_riding(feeler->get_hit_object());
        }
    }
    if (!walking) {
        // Placed Physics_Wallplant_Distance_From_Wall off it (or 0.1 off anything nearer).
        maths::Vec4& point = vector_at(this, kHitPoint);
        maths::Vec4& normal = vector_at(this, kHitNormal);
        feeler->line_.start = point;
        feeler->line_.end = point + script::fetch_float(kDistanceFromWall) * normal;
        if (feeler->get_hit_shape(true, false))
            vector_at(object_of(this), kPosition) = point + kFromWall * normal;
        else
            vector_at(object_of(this), kPosition) = feeler->line_.end;
    }
    // Upright, facing along the new velocity.
    object = object_of(this);
    maths::Vec4& forward = vector_at(object, kForward);
    forward = vector_at(object, kVelocity);
    vector_at(object_of(this), kForward).y = 0.0f;
    vector_at(object_of(this), kForward).make_unit();
    vector_at(object_of(this), kUp) = {0.0f, 1.0f, 0.0f, 1.0f};
    object = object_of(this);
    vector_at(object, kMatrix) = maths::cross_vec(vector_at(object, kUp), vector_at(object, kForward));
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    get_at<int>(this, kWallplantTime) = get_now();
    skater_object(this)->post_own_event(kWallplant, nullptr);
    static_cast<objects::MoveExecutor*>(get_at<void*>(this, kSkaterComponent))
        ->pass_to_trick_objects(get_at<uint32_t>(this, kHitObjectChecksum));
    set_timed_flag(this, 0xC0, 0);
    set_timed_flag(this, 0xB0, 0);
    set_timed_flag(this, 0xA0, 1);
    physics::change_phase(this, 5);
    get_at<int>(this, kLateFrames) = 0;
    get_at<int>(this, kWallplantEndTime) = get_now();
    return true;
}

// 0x00508140
bool SkaterPhysicsModule::start_wall_ride()
{
    if (get_at<uint8_t>(this, kDriving) || !get_at<uint8_t>(this, kHitWall))
        return false;
    // (not original: THAW 0x005D6FAF - on the bike square held starts one too)
    if (!button_pressed(this, kTriangle) && !(thaw_bmx::live_now(this) && thaw_bmx::wallride_button(this)))
        return false;
    // Not again within Wall_Ride_Delay of the last ride or plant.
    float since_ride = static_cast<float>(static_cast<int>(get_since(get_at<uint32_t>(this, kWallRideStartTime))));
    if (!(static_cast<double>(script::fetch_float(kWallRideDelay)) * kToMilliseconds < since_ride))
        return false;
    float since_plant = static_cast<float>(static_cast<int>(get_since(get_at<uint32_t>(this, kWallplantEndTime))));
    if (!(static_cast<double>(script::fetch_float(kWallRideDelay)) * kToMilliseconds < since_plant))
        return false;
    if (vector_at(object_of(this), kVelocity).y > 0.0f && !wall_above_hit(this))
        return false;
    // Fast enough along the wall, not upside down, not head-on, not tilted too far.
    maths::Vec4& wall = vector_at(this, kWallDirection);
    wall = vector_at(this, kHitNormal);
    maths::Vec4 flat_wall = {wall.x, 0.0f, wall.z, wall.w};
    flat_wall.make_unit();
    void* object = object_of(this);
    const maths::Vec4& v = vector_at(object, kVelocity);
    maths::Vec4 heading = {v.x, 0.0f, v.z, v.w};
    heading.make_unit();
    maths::Vec4 along = {v.x, 0.0f, v.z, v.w};
    along.flatten_onto_plane(flat_wall);
    float speed = static_cast<float>(length_unrounded(along));
    if (script::fetch_float(kWallRideMinSpeed) > speed)
        return false;
    float facing =
        static_cast<float>(static_cast<double>(heading.x) * flat_wall.x + static_cast<double>(heading.z) * flat_wall.z +
                           static_cast<double>(heading.y) * flat_wall.y);
    if (!(-x87_sin(static_cast<double>(script::fetch_float(kUpsideDownAngle)) * kRadiansPerDegree) < wall.y))
        return false;
    float head_on = std::fabs(facing);
    if (!(x87_sin(static_cast<double>(script::fetch_float(kMaxIncidentAngle)) * kRadiansPerDegree) > head_on))
        return false;
    object = object_of(this);
    if (!(x87_cos(static_cast<double>(script::fetch_float(kMaxTilt)) * kRadiansPerDegree) < vector_at(object, kUp).y))
        return false;
    void* skater = get_at<void*>(this, kSkaterComponent);
    if (!get_at<uint8_t>(skater, 0x13B0))
        static_cast<objects::MoveExecutor*>(skater)->set_trick_objects(false);
    remember_floor_probe(&get_at<uint8_t>(this, kFeeler));
    check_triggers(this, kSkaterEvents0064bB98, kLastFloorProbe);
    if (trigger_took_over(this))
        return false;
    // Nothing in the way along the move (turned along the wall, three times as far).
    maths::Vec4 hit = vector_at(this, kHitPoint);
    object = object_of(this);
    maths::Vec4 move = vector_at(object, kPosition) - vector_at(object, kOldPosition);
    move.align_to_plane(wall);
    vector_at(this, kProbeOrigin) = vector_at(object_of(this), kPosition);
    vector_at(this, kProbeTip) = vector_at(object_of(this), kPosition) + move * 3.0f;
    if (probe_touches_zone(0x10, 0))
        return false;
    // On the wall: at the hit, one off it; moving and facing along it, up its normal.
    get_at<int>(this, kWallRideStartTime) = get_now();
    vector_at(object_of(this), kPosition) = hit;
    vector_at(object_of(this), kPosition) += wall;
    vector_at(object_of(this), kVelocity).align_to_plane(wall);
    vector_at(object_of(this), kForward) = vector_at(object_of(this), kVelocity);
    vector_at(object_of(this), kForward).make_unit();
    vector_at(object_of(this), kUp) = wall;
    object = object_of(this);
    vector_at(object, kMatrix) = maths::cross_vec(vector_at(object, kUp), vector_at(object, kForward));
    vector_at(object_of(this), kMatrix).make_unit();
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    const maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    double side = static_cast<double>(wall.x) * velocity.z + -static_cast<double>(wall.z) * velocity.x +
                  static_cast<double>(velocity.y) * 0.0f;
    skater_object(this)->post_own_event(side < 0.0 ? kWallRideLeft : kWallRideRight, nullptr);
    physics::change_phase(this, 2);
    start_riding_hit_object();
    get_at<int>(this, kLateFrames) = 0;
    return true;
}

} // namespace skater
