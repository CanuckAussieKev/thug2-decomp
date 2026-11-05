// The skater's core physics component: the skater against walls ahead (in the air) and to the sides.

#include <cmath>
#include <cstdint>
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/collision_events.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/rider.h"
#include "game/rider/skater_sound.h"
#include "engine/objects/trigger_events.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/render/coll_obj.h"

namespace skater {

using namespace core_fields;

namespace {
constexpr float kSteepDot = 0.8F;
constexpr float kFloorLeanY = 0.5F;
constexpr float kStepUpVelocityY = 10.0F;
constexpr float kUprightY = 0.5F;
constexpr float kBarelyUpNormalY = 0.01F;
constexpr float kSnapLimit = 4.0F;
constexpr double kSnapLimitD = 4.0;
constexpr float kSnapDrop = 2.0F;
constexpr double kSnapDropD = 2.0;
constexpr float kMinSurfaceY = 0.05F;
constexpr float kCeilingNormalY = 0.1F;
constexpr float kPushFraction = 0.1F;
constexpr float kNudge = 0.05F;
constexpr float kCarryFactor = 2.0F;
constexpr float kOverlapDivisor = 10.0F;
constexpr float kDeflectFactor = 0.2F;
constexpr float kFacingUpLimit = 0.25F;

constexpr uint32_t kForwardHeight = 0xD4205C9B;     // Skater_First_Forward_Collision_Height
constexpr uint32_t kForwardLength = 0x20102726;     // Skater_First_Forward_Collision_Length
constexpr uint32_t kAirSnapUp = 0x786B3272;         // Physics_Air_Snap_Up
constexpr uint32_t kMinDistanceToWall = 0x23410C14; // Skater_Min_Distance_To_Wall
constexpr uint32_t kSpeedCapStat = 0xCC5F87AA;      // Skater_Max_Max_Speed_Stat
constexpr uint32_t kVehicleLandSound = 0xD478C96D;  // vehicle_play_land_sound (a script)
constexpr uint32_t kSideHeight = 0xB45B39E2;        // Skater_side_collide_height
constexpr uint32_t kSideLength = 0x406B425F;        // Skater_side_collide_length
constexpr uint32_t kAirExtraSide = 0x1C58C8B4;      // Skater_air_extra_side_col

// The smart pointer of a stack feeler let go inline (the original's code on some paths instead of the destructor).
void release_feeler_object(render::ProbeRay& feeler)
{
    feeler.entity().get_release();
}

} // namespace

// (Part of 0x00508780) A ledge: the feeler raised by the snap height, then lowered 2 at a time until it hits
// (the snap height going down with it, to 4); snapped up onto it (true) unless the raised feeler hits.
bool SkaterPhysicsModule::snap_up_ledge(render::ProbeRay& hit, objects::GameEntity* object)
{
    float snap = tuning_float_lookup(kAirSnapUp, object);
    hit.line().start.y = hit.line().start.y + snap;
    hit.line().end.y = hit.line().end.y + snap;
    if (hit.get_hit_shape(true, false) == 0) {
        if (snap > kSnapLimit) {
            double lower = 0.0;
            for (bool again = true; again; again = lower > kSnapLimitD) {
                hit.line().start.y = hit.line().start.y - kSnapDrop;
                hit.line().end.y = hit.line().end.y - kSnapDrop;
                if (hit.get_hit_shape(true, false) != 0) {
                    break;
                }
                lower = static_cast<double>(snap) - kSnapDropD;
                snap = static_cast<float>(lower);
            } // (the unrounded value compared)
        }
        maths::Vec4& position2 = entity()->at_point();
        position2.y = snap + position2.y;
        destroy_feeler(&hit);
        return true;
    }
    return false;
}

// 0x00508780
bool SkaterPhysicsModule::check_air_forward_collision(const maths::Vec4* from)
{
    objects::GameEntity* object = object_of(this);
    const maths::Vec4 entry_velocity = object->vel(); // (not original: for note_path)
    // The move's direction (x, y, z normalised - z's difference kept unrounded, as the original does; w as it is).
    const maths::Vec4& position = object->at_point();
    const float move_w = position.w - from->w;
    const double dz = static_cast<double>(position.z) - from->z;
    const float dy = position.y - from->y;
    const float dx = position.x - from->x;
    maths::Vec4 direction = {.x = dx, .y = dy, .z = static_cast<float>(dz), .w = 0.0F};
    const double length = std::sqrt(((static_cast<double>(dx) * dx) + (static_cast<double>(dy) * dy)) + (dz * dz));
    if (length > 0.0) {
        const double scale = 1.0 / length;
        direction.x = static_cast<float>(dx * scale);
        direction.y = static_cast<float>(dy * scale);
        direction.z = static_cast<float>(dz * scale);
    }

    // The feeler: from `from` raised to the collision height, to the new position raised, plus the length along the
    // move.
    const float height = tuning_float_lookup(kForwardHeight, object);
    const maths::Vec4& up = core::at(object->basis_frame().row, 1);
    const maths::Vec4 raise = {.x = height * up.x, .y = height * up.y, .z = height * up.z, .w = height * up.w};
    maths::Vec4& start = feelers_.probe_start;
    start.x = raise.x + from->x;
    start.y = raise.y + from->y;
    start.z = raise.z + from->z;
    start.w = raise.w + from->w;
    const float reach = tuning_float_lookup(kForwardLength, object);
    const float ahead_w = move_w * reach;
    const float ahead_z = direction.z * reach;
    const float ahead_y = direction.y * reach;
    const float ahead_x = reach * direction.x;
    const maths::Vec4& now = entity()->at_point();
    const double raised_w = static_cast<double>(raise.w) + now.w; // (unrounded)
    const float raised_z = raise.z + now.z;
    const float raised_y = raise.y + now.y;
    const float raised_x = raise.x + now.x;
    maths::Vec4& end = feelers_.probe_end;
    end.x = raised_x + ahead_x;
    end.y = raised_y + ahead_y;
    end.z = raised_z + ahead_z;
    end.w = static_cast<float>(raised_w + ahead_w);
    if (!get_feel(this, render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
        return false;
    }

    const maths::Vec4 normal = feelers_.feeler.surface_dir();
    const maths::Vec4* hit_normal = &feelers_.feeler.surface_dir();
    if (feelers_.hit_skatable != 0U) {
        // Something to skate on: only a wall when it's unlike the ground (and not facing up).
        const maths::Vec4& ground = ground_.floor_normal;
        const double dot = ((static_cast<double>(normal.z) * ground.z) + (static_cast<double>(normal.y) * ground.y)) +
                           (static_cast<double>(normal.x) * ground.x);
        if (!(dot < static_cast<double>(kSteepDot)) || !(normal.y < kFloorLeanY)) {
            return false;
        }
    }

    render::ProbeRay hit; // (the original: raw storage, copy-constructed in place)
    copy_feeler(&hit, &feelers_.feeler);
    const bool steep = feelers_.feeler.surface_dir().y < 0.1F;
    // Try the skater at the feeler's end (lowered back): 0x004FFDB0 may take it there - then stopped, when going up
    // fast or the wall is steep (left there).
    maths::Vec4& tried_position = entity()->at_point();
    const maths::Vec4 saved = tried_position;
    tried_position.x = end.x - raise.x;
    tried_position.y = end.y - raise.y;
    tried_position.z = end.z - raise.z;
    tried_position.w = end.w - raise.w;
    if (step_up(from) && (entity()->vel().y > kStepUpVelocityY || steep)) {
        release_feeler_object(hit);
        return true;
    }
    entity()->at_point() = saved;
    assign_feeler(&feelers_.feeler, &hit);

    // A ledge: snap up onto it (going up, upright, against a flat-topped wall) - the feeler raised by the snap height,
    // then lowered 2 at a time until it hits (the snap height going down with it, to 4).
    const objects::GameEntity* ledge_skater = object_of(this);
    if (core::at(ledge_skater->basis_frame().row, 1).y > kUprightY && ledge_skater->vel().y > 0.0F &&
        normal.y < kBarelyUpNormalY) {
        if (snap_up_ledge(hit, object)) {
            return true;
        }
    }

    if (bit_mark(this, PoseFlag::kVertAir) != 0U) {
        // On vert: back at the hit, kept the min distance off it, the velocity along it.
        const maths::Vec4& point = feelers_.feeler.contact_pos();
        maths::Vec4& skater_position = entity()->at_point();
        skater_position.x = point.x - raise.x;
        skater_position.y = point.y - raise.y;
        skater_position.z = point.z - raise.z;
        skater_position.w = point.w - raise.w;
        const float distance = tuning_float_lookup(kMinDistanceToWall, object);
        maths::Vec4& off_wall_position = entity()->at_point();
        off_wall_position.x = static_cast<float>((static_cast<double>(distance) * normal.x) + off_wall_position.x);
        off_wall_position.y = static_cast<float>((static_cast<double>(normal.y) * distance) + off_wall_position.y);
        off_wall_position.z = static_cast<float>((static_cast<double>(normal.z) * distance) + off_wall_position.z);
        off_wall_position.w = static_cast<float>((static_cast<double>(normal.w) * distance) + off_wall_position.w);
        maths::Vec4& velocity = entity()->vel();
        if (bit_mark(this, PoseFlag::kAcidDrop) != 0U) {
            velocity.align_to_plane(*hit_normal);
        } else {
            velocity.flatten_onto_plane(normal);
        }
        collision_events::note_path(this, collision_events::RouteWay::kAirWallVert, entry_velocity); // (not original)
        release_feeler_object(hit);
        return false;
    }

    // A bonk - unless a wallplant (not with +0x302) or 0x00508140 takes over.
    bool done = false;
    if ((siblings_.state->in_trick_move() == 0U) && (bit_mark(this, PoseFlag::kBailing) == 0U)) {
        if (((motoskateboard_ == 0U) && wall_plant(false, false, false)) || start_wall_ride()) {
            done = true;
        }
    }
    if (!done) {
        set_flag(this, PoseFlag::kNoAcidDrop, 1);
        trigger_feeler(this, kBonkEvents, &feelers_.feeler);
        done = trigger_took_over(this);
    }
    if (!done) {
        if (feelers_.feeler.surface_dir().y < kMinSurfaceY) {
            // The bonk's sound: by speed and terrain - on a vehicle its own script.
            if (on_vehicle_ == 0U) {
                const uint8_t terrain = feelers_.feeler.hit().terrain;
                objects::GameEntity* skater = object_of(this);
                auto speed = static_cast<float>(length_of(skater->vel()));
                const float fraction = speed / core::down_cast<objects::Rider>(skater)->get_scaled_physics(kSpeedCapStat);
                static_cast<objects::RiderSfxPlayer*>(siblings_.sound)
                    ->play_bonk({.speed = fraction, .terrain = terrain});
            } else {
                script::launch_script(kVehicleLandSound, nullptr, static_cast<objects::Entity*>(object_of(this)), false,
                                      nullptr);
            }
        }
        // The velocity along the wall (keeping its height when the wall isn't facing down), pushed off it by a
        // tenth of the speed.
        float kept_y = 0.0F;
        if (normal.y > -kCeilingNormalY) {
            maths::Vec4& v = entity()->vel();
            kept_y = v.y;
            v.y = 0.0F;
        }
        entity()->vel().flatten_onto_plane(normal);
        maths::Vec4& velocity = entity()->vel();
        auto push = static_cast<float>(length_of(velocity) * kPushFraction);
        velocity += normal * push;
        if (normal.y > -kCeilingNormalY) {
            entity()->vel().y = kept_y;
        }
        // (The original clears the facing's y around this copy and puts it back.)
        late_wallplant_.wall_normal = normal;
        // The facing turned a little away from the wall; the matrix made square again around it.
        objects::GameEntity* turned_skater = object_of(this);
        core::at(turned_skater->basis_frame().row, 2) += kNudge * normal;
        core::at(entity()->basis_frame().row, 2).make_unit();
        turned_skater = object_of(this);
        core::at(turned_skater->basis_frame().row, 0) = maths::cross_vec(core::at(turned_skater->basis_frame().row, 1),
                                                                         core::at(turned_skater->basis_frame().row, 2));
        core::at(entity()->basis_frame().row, 0).make_unit();
        turned_skater = object_of(this);
        core::at(turned_skater->basis_frame().row, 1) = maths::cross_vec(core::at(turned_skater->basis_frame().row, 2),
                                                                         core::at(turned_skater->basis_frame().row, 0));
        core::at(entity()->basis_frame().row, 1).make_unit();
        // A moving object that was hit pushes the skater along (when it's moving into the skater).
        auto& feeler = feelers_.feeler;
        if (feeler.hit_object_alive()) {
            const maths::Vec4 carried = feeler.get_hit_object()->vel();
            const double into =
                ((static_cast<double>(carried.z) * hit_normal->z) + (static_cast<double>(carried.y) * hit_normal->y)) +
                (static_cast<double>(carried.x) * hit_normal->x);
            if (into > 0.0) {
                const float frame = frame_.length;
                objects::GameEntity* carried_skater = object_of(this);
                carried_skater->at_point() += (carried * frame) * kCarryFactor;
                entity()->vel() += carried;
            }
        }
        collision_events::note_path(this, collision_events::RouteWay::kAirBonk, entry_velocity); // (not original)
    }
    destroy_feeler(&hit);
    return false;
}

// 0x0050A2C0
bool SkaterPhysicsModule::check_side_collision(SideProbe side_probe)
{
    const auto side = side_probe.side;
    const auto length = side_probe.length;
    if (state_of(this) == kWallRide) {
        return false;
    }
    const objects::GameEntity* object = object_of(this);
    const float height = script::fetch_float(kSideHeight);
    const maths::Vec4& up = core::at(object->basis_frame().row, 1);
    const float raise_w = height * up.w;
    const float raise_z = height * up.z;
    const float raise_y = height * up.y;
    const float raise_x = height * up.x;
    const maths::Vec4& position = entity()->at_point();
    maths::Vec4& start = feelers_.probe_start;
    start.x = raise_x + position.x;
    start.y = raise_y + position.y;
    start.z = raise_z + position.z;
    start.w = raise_w + position.w;
    float reach = length;
    if (state_of(this) == kAir) {
        reach = script::fetch_float(kAirExtraSide) + length;
    }
    // Out along the right (z and w from the unrounded side products, as the original).
    const maths::Vec4& right = core::at(entity()->basis_frame().row, 0);
    const float out_y = (side * right.y) * reach;
    const float out_x = (side * right.x) * reach;
    auto out_w = static_cast<float>(static_cast<double>(side) * right.w * reach);
    auto out_z = static_cast<float>(static_cast<double>(side) * right.z * reach);
    maths::Vec4& end = feelers_.probe_end;
    end.x = out_x + start.x;
    end.y = out_y + start.y;
    end.z = out_z + start.z;
    end.w = out_w + start.w;
    if (!get_feel(this, render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
        return false;
    }

    const maths::Vec4 normal = feelers_.feeler.surface_dir();
    float overlap = 0.0F; // (uninitialised in the original when not in the air)
    if (state_of(this) == kAir) {
        if ((siblings_.state->in_trick_move() == 0U) && (bit_mark(this, PoseFlag::kBailing) == 0U) &&
            start_wall_ride()) {
            return true;
        }
        // Pushed out of it by a tenth of how far into it the skater is (the hit's distance under the reach, less
        // the extra air reach).
        const maths::Vec4& point = feelers_.feeler.contact_pos();
        maths::Vec4 into = {.x = point.x - start.x, .y = point.y - start.y, .z = point.z - start.z, .w = point.w - start.w};
        const double distance =
            std::sqrt(((static_cast<double>(into.z) * into.z) + (static_cast<double>(into.y) * into.y)) +
                      (static_cast<double>(into.x) * into.x));
        const double depth = distance - (static_cast<double>(reach) - length);
        overlap = static_cast<float>(depth);
        if (!(depth > 0.0)) {
            return false;
        }
        into.set_length(overlap);
        entity()->at_point() -= into / kOverlapDivisor;
        const objects::PadInputModule* input = siblings_.input;
        if (state_of(this) != kAir && (input->pad_primary().shoulder_right_a().held_down() == 0U) &&
            (input->pad_primary().shoulder_left_a().held_down() == 0U) &&
            (input->pad_primary().left_side().held_down() == 0U) &&
            (input->pad_primary().right_side().held_down() == 0U)) {
            deflect_from_wall(&normal, &overlap, kDeflectFactor); // (never: the state is the air's)
        }
        return false;
    }
    // On the ground: a near-vertical wall puts the skater back where it was (+0x98) ...
    const double facing_up = ((static_cast<double>(normal.z) * up.z) + (static_cast<double>(normal.y) * up.y)) +
                             (static_cast<double>(normal.x) * up.x);
    if (!(facing_up < static_cast<double>(kFacingUpLimit)) || !(facing_up > static_cast<double>(-kFacingUpLimit))) {
        return false;
    }
    const maths::Vec4 entry_velocity = entity()->vel(); // (not original: for note_path)
    entity()->at_point() = frame_.safe_pos;
    deflect_from_wall(&normal, &overlap, kDeflectFactor);
    collision_events::note_path(this, collision_events::RouteWay::kSideWall, entry_velocity); // (not original)
    // ... then, unless there's something between (a feeler from just off the hit out to the length) or a trigger's
    // script takes over, onto the wall: at that feeler's end, lowered by the side height.
    const maths::Vec4& point = feelers_.feeler.contact_pos();
    const maths::Vec4 outer = point + normal * length;
    const maths::Vec4 inner = point + normal;
    render::ProbeRay feeler(&inner, &outer);
    if (feeler.get_hit_shape(true, false) != 0) {
        destroy_feeler(&feeler);
        return false;
    }
    trigger_feeler(this, kBonkEvents, &feelers_.feeler);
    if (trigger_took_over(this)) {
        destroy_feeler(&feeler);
        return false;
    }
    objects::GameEntity* skater = object_of(this);
    const float lower = script::fetch_float(kSideHeight);
    skater->at_point() = feeler.line().end - core::at(skater->basis_frame().row, 1) * lower;
    destroy_feeler(&feeler);
    return true;
}

// 0x0050C930
void SkaterPhysicsModule::test_lateral_hits()
{
    const maths::Vec4 saved = entity()->at_point();
    const float length = script::fetch_float(kSideLength);
    bool both = false;
    if (check_side_collision({.side = -1.0F, .length = length})) {
        both = check_side_collision({.side = 1.0F, .length = length});
    } else {
        both = check_side_collision({.side = 1.0F, .length = length}) &&
               check_side_collision({.side = -1.0F, .length = length});
    }
    if (both) {
        entity()->at_point() = saved;
    }
}

} // namespace skater
