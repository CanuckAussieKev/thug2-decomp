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
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"

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

objects::GameEntity* skater_object(SkaterPhysicsModule* component)
{
    return static_cast<objects::GameEntity*>(object_of(component));
}

void check_triggers(SkaterPhysicsModule* component, const int* kind, render::ProbeRay* feeler)
{
    component->trigger_->probe_touches_zone(kind, feeler);
}

bool trigger_took_over(SkaterPhysicsModule* component) { return component->physics_control_->flag26_ != 0; }

// The trick button `button` pressed now, or within Wall_Ride_Triangle_Window (0x0051ECF0 / 0x0051F6B0 on the
// skater component).
bool button_pressed(SkaterPhysicsModule* component, uint32_t button)
{
    auto* skater = static_cast<objects::MoveExecutor*>(component->trick_);
    if (skater->is_key_held(button))
        return true;
    const uint32_t window = ftol(static_cast<double>(script::fetch_float(kWallRideWindow)) * kToMilliseconds);
    return static_cast<objects::MoveExecutor*>(component->trick_)->find_press(button, window, 0xFFFFFFFFu);
}

// Moving up the wall: whether there's wall 6 either side of the hit point raised by 0.15 of the upward speed (along
// the wall).
bool wall_above_hit(SkaterPhysicsModule* component)
{
    maths::Vec4 point = component->feeler_.contact_pos_;
    const maths::Vec4 normal = component->feeler_.surface_dir_;
    maths::Vec4 rise = {0.0f, object_of(component)->vel_.y * kRiseScale, 0.0f, 1.0f};
    rise.align_to_plane(normal);
    point += rise;
    const maths::Vec4 end = point - normal * kProbe;
    const maths::Vec4 start = point + normal * kProbe;
    render::ProbeRay feeler(&start, &end);
    return feeler.get_hit_shape(true, false) != 0;
}

} // namespace

// 0x00507500
bool SkaterPhysicsModule::wall_plant(bool late, bool check_only, bool walking)
{
    if (on_vehicle_) {
        int allowed = 0;
        if (!skater_object(this)->read_int_tag(kCanWallplant, &allowed))
            return false;
    }
    if (model_rotating_ || static_cast<uint8_t>(physics_control_->board_missing_))
        return false;

    if (!walking) {
        const objects::GameEntity* object = object_of(this);
        if (object->basis_frame_.row[1].y < kMinUp)
            return false;
        const int elapsed = static_cast<int>(get_since(wallplant_time_));
        double since_plant = elapsed;
        if (elapsed < 0)
            since_plant += kUnsignedOffset; // (unsigned)
        auto since_plant_float = static_cast<float>(since_plant);
        if (script::fetch_float(kDisallowRewallplant) > since_plant_float)
            return false;
        auto since_ride = static_cast<float>(frame_clock::game_clock_time_since(wall_ride_start_));
        if (static_cast<double>(script::fetch_float(kWallplantDelay)) * kToMilliseconds > since_ride)
            return false;
        if (script::fetch_float(kMinWallplantHeight) > state_->height_above_ground_)
            return false;
        // Not a steep enough wall: only for a late plant (unless just checking, or it's vert).
        if (!check_only && !(feeler_.hit_.face_flags_ & 8) && std::fabs(feeler_.surface_dir_.y) > kMaxTilt01 && !late)
            return false;
        // A wall plant trick pressed? (Not original: THAW 0x005DB1ED - on the bike square held plants without asking
        // the list, and the list is the bike's, bike_Wallplant_Trick.)
        const bool bike = thaw_bmx::live_now(this);
        script::Array* tricks = script::fetch_list(bike ? thaw_bmx::wallplant_trick_list() : kWallplantTrick);
        bool pressed = bike && thaw_bmx::wallplant_by_square(this);
        for (uint32_t i = 0; !pressed && i < tricks->extent_; ++i) {
            const uint32_t trick = tricks->fetch_part(i);
            if (static_cast<objects::MoveExecutor*>(trick_)->check_trigger(reinterpret_cast<script::Struct*>(trick), 0,
                                                                           0, true)) {
                pressed = true;
                break;
            }
        }
        if (!pressed) {
            if (late)
                return false;
            // Not yet: a few frames to press it late in (kept: the velocity and position; the skater backed off the
            // wall 24 the way it came).
            late_wallplant_frames_ = script::fetch_int_value(kLateWallplantFrames);
            late_velocity_ = entity_->vel_;
            late_pos_ = entity_->at_point_;
            objects::GameEntity* o = object_of(this);
            const maths::Vec4& v = o->vel_;
            maths::Vec4 back = {-v.x, 0.0f, -v.z, v.w};
            back.make_unit();
            maths::Vec4& p = o->at_point_;
            const float y = back.y * kBackOff;
            const float z = back.z * kBackOff;
            const float w = back.w * kBackOff;
            p.z = z + p.z;
            p.w = w + p.w;
            p.x = static_cast<float>(static_cast<double>(back.x) * kBackOff + p.x);
            p.y = y + p.y;
            return false;
        }
        if (late) {
            // A late plant: back to where the skater was, and the wall looked for again along its move.
            debug::printf(kLateWallplantText);
            entity_->vel_ = late_velocity_;
            entity_->at_point_ = late_pos_;
            objects::GameEntity* o = object_of(this);
            const maths::Vec4& position = o->at_point_;
            const maths::Vec4& old = o->old_pos_;
            maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, position.w - old.w};
            move.make_unit();
            const double height = tuning_float_lookup(kForwardHeight, o);
            const maths::Vec4& up = o->basis_frame_.row[1];
            auto hw = static_cast<float>(height * up.w), hz = static_cast<float>(height * up.z),
                 hy = static_cast<float>(height * up.y), hx = static_cast<float>(height * up.x);
            const maths::Vec4& last = entity_->old_pos_;
            maths::Vec4& start = probe_start_;
            start = {hx + last.x, hy + last.y, hz + last.z, hw + last.w};
            const double length = tuning_float_lookup(kForwardLength, object_of(this));
            auto lw = static_cast<float>(move.w * length), lz = static_cast<float>(move.z * length),
                 ly = static_cast<float>(move.y * length), lx = static_cast<float>(length * move.x);
            const maths::Vec4& now = entity_->at_point_;
            const double w = static_cast<double>(hw) + now.w; // (left unrounded)
            const float z = hz + now.z;
            const float y = hy + now.y;
            const float x = hx + now.x;
            maths::Vec4& end = probe_end_;
            end = {x + lx, y + ly, z + lz, static_cast<float>(w + lw)};
            if (!probe_touches_zone(0x10, 0))
                return false;
        }
    } else {
        // Walking: a feeler 36 the other way from the facing, at the forward collision height.
        objects::GameEntity* object = object_of(this);
        const maths::Vec4& facing = object->basis_frame_.row[2];
        maths::Vec4 direction = {-facing.x, facing.y, -facing.z, facing.w};
        direction.make_unit();
        const float height = tuning_float_lookup(kForwardHeight, object);
        const maths::Vec4 lift = object->basis_frame_.row[1] * height;
        probe_start_ = entity_->at_point_ + lift;
        const maths::Vec4 reach = direction * kWalkReach;
        const maths::Vec4 base = entity_->at_point_ + lift;
        probe_end_ = base + reach;
        if (!probe_touches_zone(0x10, 0))
            return false;
    }

    if (check_only)
        return true;
    // The plant: off the wall (its normal flattened; a late plant's saved one) at the approach angle allowed.
    auto speed = static_cast<float>(length_unrounded(entity_->vel_));
    if (speed < kMinSpeed)
        return false;
    maths::Vec4 heading = entity_->vel_ / speed;
    heading.y = 0.0f;
    heading.make_unit();
    maths::Vec4 wall = feeler_.surface_dir_;
    if (late) {
        wall.x = late_wall_normal_.x;
        wall.z = late_wall_normal_.z;
        wall.w = late_wall_normal_.w;
    }
    wall.y = 0.0f;
    wall.make_unit();
    if (!walking) {
        const double limit = std::sin(static_cast<double>(script::fetch_float(kMinApproachAngle)) * kRadiansPerDegree);
        const double approach = static_cast<double>(heading.x) * wall.x + static_cast<double>(heading.z) * wall.z +
                                static_cast<double>(wall.y) * heading.y;
        if (-limit < approach)
            return false;
    }
    if (entity_->vel_.y > 0.0f && input_->pad_primary_.button_wedge_.held_down_ && !wall_above_hit(this))
        return false;
    // (not original: THAW 0x005DBB9E - no sticker slap on the bike)
    if (!thaw_bmx::live_now(this))
        static_cast<objects::TaggingControlComponent*>(tagging_component_)->slap_sticker(false, walking, late);
    entity_->vel_.y = 0.0f;
    check_triggers(this, kSkaterEvents0064bBec, &feeler_);
    if (trigger_took_over(this))
        return true;

    // Bounced off the wall (the speed less Physics_Wallplant_Speed_Loss, at least the minimum exit speed), up at
    // the vertical exit speed.
    objects::GameEntity* object = object_of(this);
    maths::Vec4& velocity = object->vel_;
    auto into = static_cast<float>(static_cast<double>(wall.z) * velocity.z + static_cast<double>(wall.y) * velocity.y +
                                   static_cast<double>(wall.x) * velocity.x);
    const maths::Vec4 bounce = into * wall;
    if (!walking) {
        velocity -= 2.0f * bounce;
        maths::Vec4& v = entity_->vel_;
        auto flat = static_cast<float>(std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.z) * v.z));
        if (flat > kMinFlatSpeed) {
            v.y = 0.0f;
            auto reduced = static_cast<float>(flat - static_cast<double>(script::fetch_float(kSpeedLoss)));
            const float least = script::fetch_float(kMinExitSpeed);
            set_length(entity_->vel_, objects::larger_of(least, reduced));
        } else {
            entity_->vel_ = -script::fetch_float(kMinExitSpeed) * heading;
        }
    } else {
        entity_->vel_ = wall * script::fetch_float(kWalkingWallplantSpeed);
    }
    entity_->vel_.y = script::fetch_float(kVerticalExitSpeed);

    // Riding what was hit (if it's an object still there).
    auto* feeler = &feeler_;
    if (feeler->hit_object_alive()) {
        const bool riding = ride_->is_riding();
        if (!riding || *reinterpret_cast<void**>(ride_->ride_) != feeler->get_hit_object()) {
            ride_->stop_riding();
            reinterpret_cast<walk::MovableContact*>(ride_)->start_riding(feeler->get_hit_object());
        }
    }
    if (!walking) {
        // Placed Physics_Wallplant_Distance_From_Wall off it (or 0.1 off anything nearer).
        const maths::Vec4& point = feeler_.contact_pos_;
        const maths::Vec4& normal = feeler_.surface_dir_;
        feeler->line_.start = point;
        feeler->line_.end = point + script::fetch_float(kDistanceFromWall) * normal;
        if (feeler->get_hit_shape(true, false))
            entity_->at_point_ = point + kFromWall * normal;
        else
            entity_->at_point_ = feeler->line_.end;
    }
    // Upright, facing along the new velocity.
    object = object_of(this);
    maths::Vec4& forward = object->basis_frame_.row[2];
    forward = object->vel_;
    entity_->basis_frame_.row[2].y = 0.0f;
    entity_->basis_frame_.row[2].make_unit();
    entity_->basis_frame_.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    object = object_of(this);
    object->basis_frame_.row[0] = maths::cross_vec(object->basis_frame_.row[1], object->basis_frame_.row[2]);
    display_matrix_ = entity_->basis_frame_;
    wallplant_time_ = get_now();
    skater_object(this)->post_own_event(kWallplant, nullptr);
    static_cast<objects::MoveExecutor*>(trick_)->pass_to_trick_objects(feeler_.hit_.object_checksum_);
    set_timed_flag(this, PoseFlag::kNoSecondSpin, 0);
    set_timed_flag(this, PoseFlag::kNoAcidDrop, 0);
    set_timed_flag(this, PoseFlag::kWallplanted, 1);
    physics::change_phase(this, 5);
    late_wallplant_frames_ = 0;
    wallplant_end_ = get_now();
    return true;
}

// 0x00508140
bool SkaterPhysicsModule::start_wall_ride()
{
    if (on_vehicle_ || !hit_wall_)
        return false;
    // (not original: THAW 0x005D6FAF - on the bike square held starts one too)
    if (!button_pressed(this, kTriangle) && !(thaw_bmx::live_now(this) && thaw_bmx::wallride_button(this)))
        return false;
    // Not again within Wall_Ride_Delay of the last ride or plant.
    auto since_ride = static_cast<float>(static_cast<int>(get_since(static_cast<uint32_t>(wall_ride_start_))));
    if (!(static_cast<double>(script::fetch_float(kWallRideDelay)) * kToMilliseconds < since_ride))
        return false;
    auto since_plant = static_cast<float>(static_cast<int>(get_since(static_cast<uint32_t>(wallplant_end_))));
    if (!(static_cast<double>(script::fetch_float(kWallRideDelay)) * kToMilliseconds < since_plant))
        return false;
    if (entity_->vel_.y > 0.0f && !wall_above_hit(this))
        return false;
    // Fast enough along the wall, not upside down, not head-on, not tilted too far.
    maths::Vec4& wall = wall_normal_;
    wall = feeler_.surface_dir_;
    maths::Vec4 flat_wall = {wall.x, 0.0f, wall.z, wall.w};
    flat_wall.make_unit();
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& v = object->vel_;
    maths::Vec4 heading = {v.x, 0.0f, v.z, v.w};
    heading.make_unit();
    maths::Vec4 along = {v.x, 0.0f, v.z, v.w};
    along.flatten_onto_plane(flat_wall);
    auto speed = static_cast<float>(length_unrounded(along));
    if (script::fetch_float(kWallRideMinSpeed) > speed)
        return false;
    auto facing =
        static_cast<float>(static_cast<double>(heading.x) * flat_wall.x + static_cast<double>(heading.z) * flat_wall.z +
                           static_cast<double>(heading.y) * flat_wall.y);
    if (!(-std::sin(static_cast<double>(script::fetch_float(kUpsideDownAngle)) * kRadiansPerDegree) < wall.y))
        return false;
    const float head_on = std::fabs(facing);
    if (!(std::sin(static_cast<double>(script::fetch_float(kMaxIncidentAngle)) * kRadiansPerDegree) > head_on))
        return false;
    object = object_of(this);
    if (!(std::cos(static_cast<double>(script::fetch_float(kMaxTilt)) * kRadiansPerDegree) <
          object->basis_frame_.row[1].y))
        return false;
    objects::MoveExecutor* skater = trick_;
    if (!skater->trick_objects_on_)
        static_cast<objects::MoveExecutor*>(skater)->set_trick_objects(false);
    remember_floor_probe(&feeler_);
    check_triggers(this, kSkaterEvents0064bB98, &floor_feeler_);
    if (trigger_took_over(this))
        return false;
    // Nothing in the way along the move (turned along the wall, three times as far).
    const maths::Vec4 hit = feeler_.contact_pos_;
    object = object_of(this);
    maths::Vec4 move = object->at_point_ - object->old_pos_;
    move.align_to_plane(wall);
    probe_start_ = entity_->at_point_;
    probe_end_ = entity_->at_point_ + move * 3.0f;
    if (probe_touches_zone(0x10, 0))
        return false;
    // On the wall: at the hit, one off it; moving and facing along it, up its normal.
    wall_ride_start_ = get_now();
    entity_->at_point_ = hit;
    entity_->at_point_ += wall;
    entity_->vel_.align_to_plane(wall);
    entity_->basis_frame_.row[2] = entity_->vel_;
    entity_->basis_frame_.row[2].make_unit();
    entity_->basis_frame_.row[1] = wall;
    object = object_of(this);
    object->basis_frame_.row[0] = maths::cross_vec(object->basis_frame_.row[1], object->basis_frame_.row[2]);
    entity_->basis_frame_.row[0].make_unit();
    display_matrix_ = entity_->basis_frame_;
    const maths::Vec4& velocity = entity_->vel_;
    const double side = static_cast<double>(wall.x) * velocity.z + -static_cast<double>(wall.z) * velocity.x +
                        static_cast<double>(velocity.y) * 0.0f;
    skater_object(this)->post_own_event(side < 0.0 ? kWallRideLeft : kWallRideRight, nullptr);
    physics::change_phase(this, 2);
    start_riding_hit_object();
    late_wallplant_frames_ = 0;
    return true;
}

} // namespace skater
