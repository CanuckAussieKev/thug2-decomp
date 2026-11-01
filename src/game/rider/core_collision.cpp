// The skater's core physics component: the skater against walls ahead (in the air) and to the sides.

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/maths/vec4.h"
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
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kForwardHeight = 0xD4205C9B;     // Skater_First_Forward_Collision_Height
constexpr uint32_t kForwardLength = 0x20102726;     // Skater_First_Forward_Collision_Length
constexpr uint32_t kAirSnapUp = 0x786B3272;         // Physics_Air_Snap_Up
constexpr uint32_t kMinDistanceToWall = 0x23410C14; // Skater_Min_Distance_To_Wall
constexpr uint32_t kSpeedCapStat = 0xCC5F87AA;      // Skater_Max_Max_Speed_Stat
constexpr uint32_t kVehicleLandSound = 0xD478C96D;  // vehicle_play_land_sound (a script)
constexpr uint32_t kSideHeight = 0xB45B39E2;        // Skater_side_collide_height
constexpr uint32_t kSideLength = 0x406B425F;        // Skater_side_collide_length
constexpr uint32_t kAirExtraSide = 0x1C58C8B4;      // Skater_air_extra_side_col

// A stack feeler: raw storage (see copy_feeler).
struct alignas(16) FeelerStorage {
    std::array<uint8_t, sizeof(render::ProbeRay)> bytes;
    render::ProbeRay* operator->() { return raw::as<render::ProbeRay>(bytes.data()); }
    render::ProbeRay* get() { return raw::as<render::ProbeRay>(bytes.data()); }
};

// The smart pointer of a stack feeler let go inline (the original's code on some paths instead of the destructor).
void release_feeler_object(FeelerStorage& feeler)
{
    feeler->entity_.get_release(); // (the original left the pointer set: the storage is let go next)
}

} // namespace

// 0x00508780
bool SkaterPhysicsModule::check_air_forward_collision(const maths::Vec4* from)
{
    objects::GameEntity* object = object_of(this);
    const maths::Vec4 entry_velocity = object->vel_; // (not original: for note_path)
    // The move's direction (x, y, z normalised - z's difference kept unrounded, as the original does; w as it is).
    const maths::Vec4& position = object->at_point_;
    const float move_w = position.w - from->w;
    const double dz = static_cast<double>(position.z) - from->z;
    const float dy = position.y - from->y;
    const float dx = position.x - from->x;
    maths::Vec4 direction = {dx, dy, static_cast<float>(dz), 0.0f};
    const double length = std::sqrt((static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) + dz * dz);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        direction.x = static_cast<float>(dx * scale);
        direction.y = static_cast<float>(dy * scale);
        direction.z = static_cast<float>(dz * scale);
    }

    // The feeler: from `from` raised to the collision height, to the new position raised, plus the length along the
    // move.
    const float height = tuning_float_lookup(kForwardHeight, object);
    const maths::Vec4& up = core::at(object->basis_frame_.row, 1);
    const maths::Vec4 raise = {height * up.x, height * up.y, height * up.z, height * up.w};
    maths::Vec4& start = probe_start_;
    start.x = raise.x + from->x;
    start.y = raise.y + from->y;
    start.z = raise.z + from->z;
    start.w = raise.w + from->w;
    const float reach = tuning_float_lookup(kForwardLength, object);
    const float ahead_w = move_w * reach;
    const float ahead_z = direction.z * reach;
    const float ahead_y = direction.y * reach;
    const float ahead_x = reach * direction.x;
    const maths::Vec4& now = entity_->at_point_;
    const double raised_w = static_cast<double>(raise.w) + now.w; // (unrounded)
    const float raised_z = raise.z + now.z;
    const float raised_y = raise.y + now.y;
    const float raised_x = raise.x + now.x;
    maths::Vec4& end = probe_end_;
    end.x = raised_x + ahead_x;
    end.y = raised_y + ahead_y;
    end.z = raised_z + ahead_z;
    end.w = static_cast<float>(raised_w + ahead_w);
    if (!get_feel(this, 0x10, 0))
        return false;

    const maths::Vec4 normal = feeler_.surface_dir_;
    const maths::Vec4* hit_normal = &feeler_.surface_dir_;
    if (hit_skatable_) {
        // Something to skate on: only a wall when it's unlike the ground (and not facing up).
        const maths::Vec4& ground = floor_normal_;
        const double dot = (static_cast<double>(normal.z) * ground.z + static_cast<double>(normal.y) * ground.y) +
                           static_cast<double>(normal.x) * ground.x;
        if (!(dot < static_cast<double>(0.8f)) || !(normal.y < 0.5f))
            return false;
    }

    FeelerStorage hit{};
    copy_feeler(hit.get(), &feeler_);
    const bool steep = feeler_.surface_dir_.y < 0.1f;
    // Try the skater at the feeler's end (lowered back): 0x004FFDB0 may take it there - then stopped, when going up
    // fast or the wall is steep (left there).
    maths::Vec4& p = entity_->at_point_;
    const maths::Vec4 saved = p;
    p.x = end.x - raise.x;
    p.y = end.y - raise.y;
    p.z = end.z - raise.z;
    p.w = end.w - raise.w;
    if (step_up(from) && (entity_->vel_.y > 10.0f || steep)) {
        release_feeler_object(hit);
        return true;
    }
    entity_->at_point_ = saved;
    assign_feeler(&feeler_, hit.get());

    // A ledge: snap up onto it (going up, upright, against a flat-topped wall) - the feeler raised by the snap height,
    // then lowered 2 at a time until it hits (the snap height going down with it, to 4).
    const objects::GameEntity* o = object_of(this);
    if (core::at(o->basis_frame_.row, 1).y > 0.5f && o->vel_.y > 0.0f && normal.y < 0.01f) {
        float snap = tuning_float_lookup(kAirSnapUp, object);
        hit->line_.start.y = hit->line_.start.y + snap;
        hit->line_.end.y = hit->line_.end.y + snap;
        if (!hit->get_hit_shape(true, false)) {
            if (snap > 4.0f) {
                double lower = 0.0;
                do {
                    hit->line_.start.y = hit->line_.start.y - 2.0f;
                    hit->line_.end.y = hit->line_.end.y - 2.0f;
                    if (hit->get_hit_shape(true, false))
                        break;
                    lower = static_cast<double>(snap) - 2.0;
                    snap = static_cast<float>(lower);
                } while (lower > 4.0); // (the unrounded value compared)
            }
            maths::Vec4& position2 = entity_->at_point_;
            position2.y = snap + position2.y;
            destroy_feeler(hit.get());
            return true;
        }
    }

    if (bit_mark(this, PoseFlag::kVertAir)) {
        // On vert: back at the hit, kept the min distance off it, the velocity along it.
        const maths::Vec4& point = feeler_.contact_pos_;
        maths::Vec4& q = entity_->at_point_;
        q.x = point.x - raise.x;
        q.y = point.y - raise.y;
        q.z = point.z - raise.z;
        q.w = point.w - raise.w;
        const float distance = tuning_float_lookup(kMinDistanceToWall, object);
        maths::Vec4& r = entity_->at_point_;
        r.x = static_cast<float>(static_cast<double>(distance) * normal.x + r.x);
        r.y = static_cast<float>(static_cast<double>(normal.y) * distance + r.y);
        r.z = static_cast<float>(static_cast<double>(normal.z) * distance + r.z);
        r.w = static_cast<float>(static_cast<double>(normal.w) * distance + r.w);
        maths::Vec4& velocity = entity_->vel_;
        if (bit_mark(this, PoseFlag::kAcidDrop))
            velocity.align_to_plane(*hit_normal);
        else
            velocity.flatten_onto_plane(normal);
        collision_events::note_path(this, collision_events::RouteWay::kAirWallVert, entry_velocity); // (not original)
        release_feeler_object(hit);
        return false;
    }

    // A bonk - unless a wallplant (not with +0x302) or 0x00508140 takes over.
    bool done = false;
    if (!state_->in_trick_move_ && !bit_mark(this, PoseFlag::kBailing)) {
        if (!motoskateboard_ && wall_plant(false, false, false))
            // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
            done = true;
        else if (start_wall_ride())
            done = true;
    }
    if (!done) {
        set_flag(this, PoseFlag::kNoAcidDrop, 1);
        trigger_feeler(this, kBonkEvents.data(), &feeler_);
        done = trigger_took_over(this);
    }
    if (!done) {
        if (feeler_.surface_dir_.y < 0.05f) {
            // The bonk's sound: by speed and terrain - on a vehicle its own script.
            if (!on_vehicle_) {
                const uint8_t terrain = feeler_.hit_.terrain;
                objects::GameEntity* skater = object_of(this);
                auto speed = static_cast<float>(length_of(skater->vel_));
                const float fraction = speed / core::down_cast<objects::Rider>(skater)->get_scaled_physics(kSpeedCapStat);
                static_cast<objects::RiderSfxPlayer*>(sound_)->play_bonk(fraction, terrain);
            } else {
                script::launch_script(kVehicleLandSound, nullptr, static_cast<objects::Entity*>(object_of(this)), false,
                                      nullptr);
            }
        }
        // The velocity along the wall (keeping its height when the wall isn't facing down), pushed off it by a
        // tenth of the speed.
        float kept_y = 0.0f;
        if (normal.y > -0.1f) {
            maths::Vec4& v = entity_->vel_;
            kept_y = v.y;
            v.y = 0.0f;
        }
        entity_->vel_.flatten_onto_plane(normal);
        maths::Vec4& velocity = entity_->vel_;
        auto push = static_cast<float>(length_of(velocity) * 0.1f);
        velocity += normal * push;
        if (normal.y > -0.1f)
            entity_->vel_.y = kept_y;
        // (The original clears the facing's y around this copy and puts it back.)
        late_wall_normal_ = normal;
        // The facing turned a little away from the wall; the matrix made square again around it.
        objects::GameEntity* s = object_of(this);
        core::at(s->basis_frame_.row, 2) += 0.05f * normal;
        core::at(entity_->basis_frame_.row, 2).make_unit();
        s = object_of(this);
        core::at(s->basis_frame_.row, 0) = maths::cross_vec(core::at(s->basis_frame_.row, 1), core::at(s->basis_frame_.row, 2));
        core::at(entity_->basis_frame_.row, 0).make_unit();
        s = object_of(this);
        core::at(s->basis_frame_.row, 1) = maths::cross_vec(core::at(s->basis_frame_.row, 2), core::at(s->basis_frame_.row, 0));
        core::at(entity_->basis_frame_.row, 1).make_unit();
        // A moving object that was hit pushes the skater along (when it's moving into the skater).
        auto& feeler = feeler_;
        if (feeler.hit_object_alive()) {
            const maths::Vec4 carried = feeler.get_hit_object()->vel_;
            const double into =
                (static_cast<double>(carried.z) * hit_normal->z + static_cast<double>(carried.y) * hit_normal->y) +
                static_cast<double>(carried.x) * hit_normal->x;
            if (into > 0.0) {
                const float frame = frame_length_;
                objects::GameEntity* t = object_of(this);
                t->at_point_ += (carried * frame) * 2.0f;
                entity_->vel_ += carried;
            }
        }
        collision_events::note_path(this, collision_events::RouteWay::kAirBonk, entry_velocity); // (not original)
    }
    destroy_feeler(hit.get());
    return false;
}

// 0x0050A2C0
bool SkaterPhysicsModule::check_side_collision(float side, float length)
{
    if (state_of(this) == kWallRide)
        return false;
    const objects::GameEntity* object = object_of(this);
    const float height = script::fetch_float(kSideHeight);
    const maths::Vec4& up = core::at(object->basis_frame_.row, 1);
    const float raise_w = height * up.w;
    const float raise_z = height * up.z;
    const float raise_y = height * up.y;
    const float raise_x = height * up.x;
    const maths::Vec4& position = entity_->at_point_;
    maths::Vec4& start = probe_start_;
    start.x = raise_x + position.x;
    start.y = raise_y + position.y;
    start.z = raise_z + position.z;
    start.w = raise_w + position.w;
    float reach = length;
    if (state_of(this) == kAir)
        reach = script::fetch_float(kAirExtraSide) + length;
    // Out along the right (z and w from the unrounded side products, as the original).
    const maths::Vec4& right = core::at(entity_->basis_frame_.row, 0);
    const float out_y = (side * right.y) * reach;
    const float out_x = (side * right.x) * reach;
    auto out_w = static_cast<float>(static_cast<double>(side) * right.w * reach);
    auto out_z = static_cast<float>(static_cast<double>(side) * right.z * reach);
    maths::Vec4& end = probe_end_;
    end.x = out_x + start.x;
    end.y = out_y + start.y;
    end.z = out_z + start.z;
    end.w = out_w + start.w;
    if (!get_feel(this, 0x10, 0))
        return false;

    const maths::Vec4 normal = feeler_.surface_dir_;
    float overlap = 0.0f; // (uninitialised in the original when not in the air)
    if (state_of(this) == kAir) {
        if (!state_->in_trick_move_ && !bit_mark(this, PoseFlag::kBailing) && start_wall_ride())
            return true;
        // Pushed out of it by a tenth of how far into it the skater is (the hit's distance under the reach, less
        // the extra air reach).
        const maths::Vec4& point = feeler_.contact_pos_;
        maths::Vec4 into = {point.x - start.x, point.y - start.y, point.z - start.z, point.w - start.w};
        const double distance =
            std::sqrt((static_cast<double>(into.z) * into.z + static_cast<double>(into.y) * into.y) +
                      static_cast<double>(into.x) * into.x);
        const double depth = distance - (static_cast<double>(reach) - length);
        overlap = static_cast<float>(depth);
        if (!(depth > 0.0))
            return false;
        into.set_length(overlap);
        entity_->at_point_ -= into / 10.0f;
        const objects::PadInputModule* input = input_;
        if (state_of(this) != kAir && !input->pad_primary_.shoulder_right_a_.held_down_ &&
            !input->pad_primary_.shoulder_left_a_.held_down_ && !input->pad_primary_.left_side_.held_down_ &&
            !input->pad_primary_.right_side_.held_down_)
            deflect_from_wall(&normal, &overlap, 0.2f); // (never: the state is the air's)
        return false;
    }
    // On the ground: a near-vertical wall puts the skater back where it was (+0x98) ...
    const double facing_up = (static_cast<double>(normal.z) * up.z + static_cast<double>(normal.y) * up.y) +
                             static_cast<double>(normal.x) * up.x;
    if (!(facing_up < static_cast<double>(0.25f)) || !(facing_up > static_cast<double>(-0.25f)))
        return false;
    const maths::Vec4 entry_velocity = entity_->vel_; // (not original: for note_path)
    entity_->at_point_ = safe_pos_;
    deflect_from_wall(&normal, &overlap, 0.2f);
    collision_events::note_path(this, collision_events::RouteWay::kSideWall, entry_velocity); // (not original)
    // ... then, unless there's something between (a feeler from just off the hit out to the length) or a trigger's
    // script takes over, onto the wall: at that feeler's end, lowered by the side height.
    const maths::Vec4& point = feeler_.contact_pos_;
    const maths::Vec4 outer = point + normal * length;
    const maths::Vec4 inner = point + normal;
    FeelerStorage feeler{};
    new (feeler.bytes.data()) render::ProbeRay(&inner, &outer);
    if (feeler->get_hit_shape(true, false)) {
        destroy_feeler(feeler.get());
        return false;
    }
    trigger_feeler(this, kBonkEvents.data(), &feeler_);
    if (trigger_took_over(this)) {
        destroy_feeler(feeler.get());
        return false;
    }
    objects::GameEntity* skater = object_of(this);
    const float lower = script::fetch_float(kSideHeight);
    skater->at_point_ = feeler->line_.end - core::at(skater->basis_frame_.row, 1) * lower;
    destroy_feeler(feeler.get());
    return true;
}

// 0x0050C930
void SkaterPhysicsModule::test_lateral_hits()
{
    const maths::Vec4 saved = entity_->at_point_;
    const float length = script::fetch_float(kSideLength);
    bool both = false;
    if (check_side_collision(-1.0f, length))
        both = check_side_collision(1.0f, length);
    else
        both = check_side_collision(1.0f, length) && check_side_collision(-1.0f, length);
    if (both)
        entity_->at_point_ = saved;
}

} // namespace skater
