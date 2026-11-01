// The skater's core physics component: jumping, skitching, the script member functions around them and the
// wallplant state's update.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/trigger_component.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/rider.h"
#include "game/rider/skater_sound.h"
#include "game/rider/thaw_bmx.h"
#include "game/skate/skate_module.h"
#include "engine/objects/trigger_events.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

namespace skater {

using namespace core_fields;

namespace {

// The checksums used here.
constexpr uint32_t kTriggerScript = 0x2CA8A299;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kNoSound = 0xE13620A8;           // no_sound
constexpr uint32_t kBonelessHeight = 0xEC6B7FC7;    // BonelessHeight
constexpr uint32_t kSpeed = 0xF0D90109;             // speed
constexpr uint32_t kChargeCapMs = 0xF733C097;       // Skater_max_tense_time
constexpr uint32_t kSpeedCapStat = 0xCC5F87AA;      // Skater_Max_Max_Speed_Stat
constexpr uint32_t kWallplantDuration = 0xA06E446B; // Physics_Wallplant_Duration
constexpr uint32_t kSkaterJump = 0x8687163A;        // SkaterJump (event)
constexpr uint32_t kAirName = 0x439F4704;           // AIR
constexpr uint32_t kGroundName = 0x58007C97;        // Ground
constexpr uint32_t kLipName = 0xA549B57B;           // Lip

// The trigger events (const int[3] each) the rail's TriggerScript is run with.

// The jump speeds: (the max, the min) for boneless or not, from vert (or a wallride from the ground) or not.
constexpr std::array<std::array<std::array<uint32_t, 2>, 2>, 2> kJumpSpeed = {{
    {{{{0x02ADE3AD, 0xC8815E43}}, {{0x9F79C8EA, 0x91B07824}}}}, // Physics_Jump_Speed_Stat / _min; Physics_Air_Jump_*
    {{{{0x8851A76E, 0x6E8EFE38}}, {{0x39766891, 0x4CDB18CB}}}}, // Physics_Boneless_Jump_Speed_*; Physics_Boneless_Air_*
}};

constexpr float kFeelerAhead = 12.0f;   // 0x0064546C (also how far a jump off a steep ramp lifts the skater)
constexpr float kFeelerBehind = -36.0f; // 0x0064BD68
constexpr float kSteepUp = -0.1f;       // 0x0064BD44: the up's y below this: a ramp too steep to jump straight off
constexpr float kSteepDrop = 1.5f;      // 0x00646744

float scaled_physics(objects::GameEntity* object, uint32_t name)
{
    return core::down_cast<objects::Rider>(object)->get_scaled_physics(name);
}

// 0x00518EE0: the jump sound, by how fast the skater goes (a fraction of the max max speed) and a terrain.
void play_jump_sound(SkaterPhysicsModule* component, uint8_t terrain)
{
    objects::GameEntity* object = object_of(component);
    const maths::Vec4& v = object->vel_;
    auto speed = static_cast<float>(
        std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z));
    const float fraction = speed / scaled_physics(object, kSpeedCapStat);
    static_cast<objects::RiderSfxPlayer*>(component->sound_)->play_jump_in_state(fraction, terrain);
}

} // namespace

// 0x005092E0
void SkaterPhysicsModule::trigger_rail_script(const int* events)
{
    auto* node = rail_node_;
    if (!node)
        return;
    if (!rail_manager_)
        rail_manager_ = modules::g_skate_module->rail_manager_.get(); // (the skate module's)
    script::Struct* script = level_nodes::fetch_vertex(node->label_, false);
    if (!script)
        return;
    // The node's TriggerScript, else the first one back along the rail (the walk stops at a loop: a second
    // pointer going half as fast).
    uint32_t trigger_script = 0;
    if (!script->read_hash_value(kTriggerScript, &trigger_script, 0)) {
        const objects::GrindPoint* slow = node;
        bool step = false;
        for (const objects::GrindPoint* p = node->last_value_; p && p != node && p != slow;) {
            script = level_nodes::fetch_vertex(p->label_, true);
            if (script->read_hash_value(kTriggerScript, &trigger_script, 0)) // (a node without a script: a crash)
                break;
            p = p->last_value_;
            if (step)
                slow = slow->last_value_;
            step = !step;
        }
    }
    if (!trigger_script)
        return;
    uint32_t name = 0; // (the original's is a stale stack word - a node pointer - if a node had no Name)
    script->read_hash_value(kName, &name, 1);
    set_flag(this, PoseFlag::kFirstTimeOnRail, name != last_rail_script_);
    rail_script_node_ = name;
    last_rail_script_ = name;
    const objects::Entity* ride_object = nullptr;
    if (const objects::RideTracker* tracker = get_tracker(this)) {
        if (tracker->entity_.owner_object_)
            ride_object = tracker->entity_.owner_object_;
        else
            stop_riding(this);
    }
    static_cast<objects::ActivationZoneModule*>(trigger_)->get_trigger_node(events, rail_script_node_, nullptr,
                                                                            ride_object);
}

// 0x00509450
void SkaterPhysicsModule::perform_leap(script::Struct* params)
{
    const bool sound = !params->has_bare_flag(kNoSound);
    switch (state_of(this)) {
    case kGround:
        trigger_feeler(this, kJumpTriggerEvents.data(), &floor_feeler_);
        if (trigger_took_over(this))
            return;
        std::memcpy(&last_ground_pos_, &entity_->at_point_, 16); // (where it jumped)
        if (sound)
            play_jump_sound(this, floor_feeler_.hit_.terrain);
        start_vert_air();
        set_flag(this, PoseFlag::kLeftVert, 1);
        break;
    case kAir:
        if (sound)
            play_jump_sound(this, floor_feeler_.hit_.terrain);
        break;
    case kRail:
        if (sound)
            play_jump_sound(this, state_->surface_type_);
        break;
    default:
        break;
    }

    // The speed: between the min and the max by how long the jump was held.
    const int max_tense = script::fetch_int_value(kChargeCapMs);
    int& tense = jump_charge_ms_;
    if (tense >= max_tense)
        tense = max_tense;
    const bool from_vert =
        bit_mark(this, PoseFlag::kVertAir) || (state_of(this) == kGround && bit_mark(this, PoseFlag::kOnVert));
    const uint32_t* names = kJumpSpeed[params->has_bare_flag(kBonelessHeight)][from_vert].data();
    objects::GameEntity* object = object_of(this);
    const float most = scaled_physics(object, names[0]);
    const float least = scaled_physics(object_of(this), names[1]);
    const double held = static_cast<double>(tense) / static_cast<double>(max_tense);
    auto speed = static_cast<float>((static_cast<double>(most) - least) * held + least);
    params->fetch_float(kSpeed, &speed, 0);

    set_flag(this, PoseFlag::kCrouched, 0);
    maths::Vec4& velocity = entity_->vel_;
    if (bit_mark(this, PoseFlag::kVertAir) && velocity.y < 0.0f) {
        // Off vert, going down: pushed off the ramp (along +0x58) instead; no longer on vert.
        const maths::Vec4& away = drawn_normal_;
        const float z = speed * away.z;
        const float w = speed * away.w;
        velocity.x = static_cast<float>(static_cast<double>(speed) * away.x + velocity.x);
        velocity.y = static_cast<float>(static_cast<double>(speed) * away.y + velocity.y);
        velocity.z += z;
        velocity.w += w;
        speed = 0.0f;
        set_flag(this, PoseFlag::kVertAir, 0);
        transfer_blocked_ = 0;
    } else if (velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }
    entity_->vel_.y += speed;
    const bool bike = thaw_bmx::live_now(this); // (not original: THAW 0x005DDDC0's bike branches, thaw_bmx_physics.cpp)
    if (bike)
        thaw_bmx::jump_boost(this);
    object = object_of(this);
    if (object->basis_frame_.row[1].y < kSteepUp) {
        // A ramp too steep (upside down): less up, and lifted off it along the up.
        maths::Vec4& v = object->vel_;
        v.y = static_cast<float>(v.y - static_cast<double>(speed) * kSteepDrop);
        const maths::Vec4& up = object->basis_frame_.row[1];
        maths::Vec4& position = object->at_point_;
        const float z = up.z * kFeelerAhead;
        const float w = up.w * kFeelerAhead;
        position.x = static_cast<float>(static_cast<double>(up.x) * kFeelerAhead + position.x);
        position.y = static_cast<float>(static_cast<double>(up.y) * kFeelerAhead + position.y);
        position.z += z;
        position.w += w;
    }
    const int state = state_of(this);
    if (state == kRail || (state == kAir && old_state_ == kRail))
        turn_for_rail_jump();
    if (point_rail_spin_)
        trigger_rail_script(kJumpEvents.data());
    if (bike)
        thaw_bmx::jump_pitch(this);
    set_physics_state(kAir);
    leap_time_ = get_now();
    static_cast<objects::Entity*>(object_of(this))->announce(kSkaterJump, nullptr, 0.0f);
}

// 0x00509920
void SkaterPhysicsModule::begin_skitch()
{
    auto* object = skitch_target_.owner_object_;
    if (!object)
        return;
    raw::as<walk::MovableContact>(ride_)->start_riding(object);
    set_flag(this, PoseFlag::kSkitching, 1);
    skitch_moving_ = 1;
    get_skitch();
    if (objects::Entity* skitched = skitch_target_.owner_object_)
        skitched->post_own_event(0x35224F25, nullptr);
}

// 0x00509990
void SkaterPhysicsModule::end_skitch()
{
    if (!bit_mark(this, PoseFlag::kSkitching))
        return;
    set_flag(this, PoseFlag::kSkitching, 0);
    auto& skitched = skitch_target_;
    if (skitched.owner_object_) {
        static_cast<objects::Entity*>(skitched.owner_object_)->post_own_event(0x2739B86D, nullptr);
        skitched.get_release();
        skitched.owner_object_ = nullptr;
    }
    objects::MovingSurfaceModule* ride = ride_;
    if (objects::RideTracker* tracker = ride->ride_) {
        ride->last_object_ = tracker->entity_.owner_object_; // (what it rode last)
        raw::as<objects::SharedRef<objects::SharedBase>>(&tracker->entity_)->get_release();
        memory::Hub::get_pull()->dealloc(tracker);
        ride->ride_ = nullptr;
        ride->stop_time_ = get_now();
    }
}

// 0x00509050
void SkaterPhysicsModule::step_wallplant()
{
    const int planted = state_changed_at_;
    flip_spin_started_ = 0;
    const uint32_t elapsed = get_since(static_cast<uint32_t>(planted));
    if (static_cast<float>(elapsed) > script::fetch_float(kWallplantDuration)) {
        // Over: into the air, with a feeler along the facing (12 ahead to 36 behind) for a trigger.
        set_physics_state(kAir);
        maths::Vec4& start = probe_start_;
        maths::Vec4& end = probe_end_;
        start = entity_->basis_frame_.row[2] * kFeelerAhead;
        end = entity_->basis_frame_.row[2] * kFeelerBehind;
        start += entity_->at_point_;
        end += entity_->at_point_;
        if (get_feel(this, 0x10, 0))
            trigger_feeler(this, kWallplantEvents.data(), &feeler_);
        return;
    }
    // Still on the wall: no rail; carried along by what the skater rides on (its turn too, when it turned).
    rail_node_ = nullptr;
    last_rail_script_ = 0;
    objects::RideTracker* tracker = get_tracker(this);
    if (!tracker)
        return;
    if (!tracker->entity_.owner_object_ || !tracker->tick_frame(&entity_->at_point_)) {
        stop_riding(this);
        return;
    }
    entity_->at_point_ += get_tracker(this)->carried_;
    tracker = get_tracker(this);
    if (tracker->turned_) {
        auto multiply = d3dx::matrix_multiply;
        maths::Mat4& matrix = entity_->basis_frame_;
        multiply(&matrix, &matrix, &tracker->turn_);
        multiply(&display_matrix_, &display_matrix_, &get_tracker(this)->turn_);
    }
}

// 0x00509A40
int SkaterPhysicsModule::get_jump(script::Struct* params, script::Script*)
{
    perform_leap(params);
    return 1;
}

// 0x00509A60
int SkaterPhysicsModule::start_skitch(script::Struct*, script::Script*)
{
    begin_skitch();
    return 1;
}

// 0x00509A70
int SkaterPhysicsModule::end_tow_ride(script::Struct*, script::Script*)
{
    end_skitch();
    return 1;
}

// 0x00509A80
int SkaterPhysicsModule::change_phase(script::Struct* params, script::Script*)
{
    uint32_t state = 0;
    params->read_hash_value(0u, &state, 0);
    if (state_of(this) == kLip && state != kLipName)
        trigger_rail_script(kOffLipEvents.data());
    if (state_of(this) != kLip && state == kLipName)
        rail_node_ = nullptr;
    if (state_of(this) == kWallRide) {
        turn_to_normal(maths::Vec4{0.0f, 1.0f, 0.0f, 1.0f});
        display_matrix_ = entity_->basis_frame_;
    }
    switch (state) {
    case kAirName:
        set_physics_state(kAir);
        break;
    case kGroundName:
        floor_feeler_.hit_.face_flag40_ = 0;
        set_physics_state(kGround);
        break;
    case kLipName:
        set_physics_state(kLip);
        break;
    default:
        break;
    }
    return 1;
}

// 0x00509B70
int SkaterPhysicsModule::trigger_grind_off(script::Struct*, script::Script*)
{
    trigger_rail_script(kGrindOffEvents.data());
    return 1;
}

// 0x00509B90
int SkaterPhysicsModule::force_break_vert(script::Struct*, script::Script*)
{
    break_vert(true);
    keep_upright();
    return 1;
}

// 0x00509BB0
int SkaterPhysicsModule::walking_wallplant(script::Struct*, script::Script*)
{
    wall_plant(false, false, true);
    return 1;
}

} // namespace skater
