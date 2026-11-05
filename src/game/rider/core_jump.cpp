// The skater's core physics component: jumping, skitching, the script member functions around them and the
// wallplant state's update.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/memory/manager.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/trigger_component.h"
#include "game/rider/walk_hang.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/rider.h"
#include "game/rider/skater_sound.h"
#include "game/rider/thaw_bmx.h"
#include "game/skate/skate_module.h"
#include "engine/objects/trigger_events.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/render/coll_obj.h"

namespace skater {

using namespace core_fields;

namespace {
constexpr std::size_t kPointBytes = 16;
constexpr uint32_t kSkitchOn = 0x35224F25; // skitchon (letter case unknown)
constexpr uint32_t kSkitchOff = 0x2739B86D; // skitchoff (letter case unknown)

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

constexpr float kFeelerAhead = 12.0F;   // 0x0064546C (also how far a jump off a steep ramp lifts the skater)
constexpr float kFeelerBehind = -36.0F; // 0x0064BD68
constexpr float kSteepUp = -0.1F;       // 0x0064BD44: the up's y below this: a ramp too steep to jump straight off
constexpr float kSteepDrop = 1.5F;      // 0x00646744

float scaled_physics(objects::GameEntity* object, uint32_t name)
{
    return core::down_cast<objects::Rider>(object)->get_scaled_physics(name);
}

} // namespace

// 0x00518EE0: the jump sound, by how fast the skater goes (a fraction of the max max speed) and a terrain.
void SkaterPhysicsModule::play_jump_sound(uint8_t terrain)
{
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& v = object->vel();
    auto speed = static_cast<float>(std::sqrt((static_cast<double>(v.x) * v.x) + (static_cast<double>(v.y) * v.y) +
                                              (static_cast<double>(v.z) * v.z)));
    const float fraction = speed / scaled_physics(object, kSpeedCapStat);
    static_cast<objects::RiderSfxPlayer*>(sound_)->play_jump_in_state(fraction, terrain);
}

// 0x005092E0
void SkaterPhysicsModule::trigger_rail_script(const std::array<int, 3>& events)
{
    auto* node = rail_node_;
    if (!node) {
        return;
    }
    if (!rail_manager_) {
        rail_manager_ = modules::Skate::sole()->rail_manager().get(); // (the skate module's)
    }
    script::Struct* script = level_nodes::fetch_vertex(node->label(), false);
    if (!script) {
        return;
    }
    // The node's TriggerScript, else the first one back along the rail (the walk stops at a loop: a second
    // pointer going half as fast).
    uint32_t trigger_script = 0;
    if (!script->read_hash_value(kTriggerScript, &trigger_script, 0)) {
        const objects::GrindPoint* slow = node;
        bool step = false;
        for (const objects::GrindPoint* grind_point = node->last_value();
             grind_point && grind_point != node && grind_point != slow;) {
            script = level_nodes::fetch_vertex(grind_point->label(), true);
            if (script->read_hash_value(kTriggerScript, &trigger_script, 0)) { // (a node without a script: a crash)
                break;
            }
            grind_point = grind_point->last_value();
            if (step) {
                slow = slow->last_value();
            }
            step = !step;
        }
    }
    if (trigger_script == 0U) {
        return;
    }
    uint32_t name = 0; // (the original's is a stale stack word - a node pointer - if a node had no Name)
    script->read_hash_value(kName, &name, 1);
    set_flag(this, PoseFlag::kFirstTimeOnRail, static_cast<uint8_t>(name != last_rail_script_));
    rail_script_node_ = name;
    last_rail_script_ = name;
    const objects::Entity* ride_object = nullptr;
    if (const objects::RideTracker* tracker = get_tracker(this)) {
        if (tracker->entity().get_pull()) {
            ride_object = tracker->entity().get_pull();
        } else {
            stop_riding(this);
        }
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
        trigger_feeler(this, kJumpTriggerEvents, &feelers_.floor_feeler);
        if (trigger_took_over(this)) {
            return;
        }
        std::memcpy(&last_ground_pos_, &entity()->at_point(), kPointBytes); // (where it jumped)
        if (sound) {
            play_jump_sound(feelers_.floor_feeler.hit().terrain);
        }
        start_vert_air();
        set_flag(this, PoseFlag::kLeftVert, 1);
        break;
    case kAir:
        if (sound) {
            play_jump_sound(feelers_.floor_feeler.hit().terrain);
        }
        break;
    case kRail:
        if (sound) {
            play_jump_sound(state_->surface_type());
        }
        break;
    default:
        break;
    }

    // The speed: between the min and the max by how long the jump was held.
    const int max_tense = script::fetch_int_value(kChargeCapMs);
    int& tense = jump_charge_ms_;
    tense = std::min(tense, max_tense);
    const bool from_vert = (bit_mark(this, PoseFlag::kVertAir) != 0U) ||
                           (state_of(this) == kGround && (bit_mark(this, PoseFlag::kOnVert) != 0U));
    const auto& names = core::at(core::at(kJumpSpeed, params->has_bare_flag(kBonelessHeight)), from_vert);
    objects::GameEntity* object = object_of(this);
    const float most = scaled_physics(object, core::at(names, 0));
    const float least = scaled_physics(object_of(this), core::at(names, 1));
    const double held = static_cast<double>(tense) / static_cast<double>(max_tense);
    auto speed = static_cast<float>(((static_cast<double>(most) - least) * held) + least);
    params->fetch_float(kSpeed, &speed, 0);

    set_flag(this, PoseFlag::kCrouched, 0);
    maths::Vec4& velocity = entity()->vel();
    if ((bit_mark(this, PoseFlag::kVertAir) != 0U) && velocity.y < 0.0F) {
        // Off vert, going down: pushed off the ramp (along +0x58) instead; no longer on vert.
        const maths::Vec4& away = drawn_normal_;
        const float z = speed * away.z;
        const float w = speed * away.w;
        velocity.x = static_cast<float>((static_cast<double>(speed) * away.x) + velocity.x);
        velocity.y = static_cast<float>((static_cast<double>(speed) * away.y) + velocity.y);
        velocity.z += z;
        velocity.w += w;
        speed = 0.0F;
        set_flag(this, PoseFlag::kVertAir, 0);
        transfer_blocked_ = 0;
    } else if (velocity.y < 0.0F) {
        velocity.y = 0.0F;
    }
    entity()->vel().y += speed;
    const bool bike = thaw_bmx::live_now(this); // (not original: THAW 0x005DDDC0's bike branches, thaw_bmx_physics.cpp)
    if (bike) {
        thaw_bmx::jump_boost(this);
    }
    object = object_of(this);
    if (core::at(object->basis_frame().row, 1).y < kSteepUp) {
        // A ramp too steep (upside down): less up, and lifted off it along the up.
        maths::Vec4& v = object->vel();
        v.y = static_cast<float>(v.y - (static_cast<double>(speed) * kSteepDrop));
        const maths::Vec4& up = core::at(object->basis_frame().row, 1);
        maths::Vec4& position = object->at_point();
        const float z = up.z * kFeelerAhead;
        const float w = up.w * kFeelerAhead;
        position.x = static_cast<float>((static_cast<double>(up.x) * kFeelerAhead) + position.x);
        position.y = static_cast<float>((static_cast<double>(up.y) * kFeelerAhead) + position.y);
        position.z += z;
        position.w += w;
    }
    const int state = state_of(this);
    if (state == kRail || (state == kAir && old_state_ == kRail)) {
        turn_for_rail_jump();
    }
    if (point_rail_spin_ != 0U) {
        trigger_rail_script(kJumpEvents);
    }
    if (bike) {
        thaw_bmx::jump_pitch(this);
    }
    set_physics_state(kAir);
    leap_time_ = get_now();
    static_cast<objects::Entity*>(object_of(this))->announce(kSkaterJump, nullptr, 0.0F);
}

// 0x00509920
void SkaterPhysicsModule::begin_skitch()
{
    auto* object = skitch_target_.get_pull();
    if (!object) {
        return;
    }
    ride_->ride_on(object);
    set_flag(this, PoseFlag::kSkitching, 1);
    skitch_moving_ = 1;
    get_skitch();
    if (const objects::Entity* skitched = skitch_target_.get_pull()) {
        skitched->post_own_event(kSkitchOn, nullptr);
    }
}

// 0x00509990
void SkaterPhysicsModule::end_skitch()
{
    if (bit_mark(this, PoseFlag::kSkitching) == 0U) {
        return;
    }
    set_flag(this, PoseFlag::kSkitching, 0);
    auto& skitched = skitch_target_;
    if (skitched.get_pull()) {
        static_cast<objects::Entity*>(skitched.get_pull())->post_own_event(kSkitchOff, nullptr);
        skitched.get_release();
        skitched.target_ = nullptr;
    }
    objects::MovingSurfaceModule* ride = ride_;
    if (objects::RideTracker* tracker = ride->ride().get()) {
        ride->set_last_object(tracker->entity().get_pull()); // (what it rode last)
        tracker->entity().get_release();
        memory::Hub::dealloc(tracker);
        ride->ride() = nullptr;
        ride->set_stop_time(get_now());
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
        maths::Vec4& start = feelers_.probe_start;
        maths::Vec4& end = feelers_.probe_end;
        start = core::at(entity()->basis_frame().row, 2) * kFeelerAhead;
        end = core::at(entity()->basis_frame().row, 2) * kFeelerBehind;
        start += entity()->at_point();
        end += entity()->at_point();
        if (get_feel(this, render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
            trigger_feeler(this, kWallplantEvents, &feelers_.feeler);
        }
        return;
    }
    // Still on the wall: no rail; carried along by what the skater rides on (its turn too, when it turned).
    rail_node_ = nullptr;
    last_rail_script_ = 0;
    objects::RideTracker* tracker = get_tracker(this);
    if (!tracker) {
        return;
    }
    if (!tracker->entity().get_pull() || !tracker->tick_frame(&entity()->at_point())) {
        stop_riding(this);
        return;
    }
    entity()->at_point() += get_tracker(this)->carried();
    tracker = get_tracker(this);
    if (tracker->turned()) {
        maths::Mat4& matrix = entity()->basis_frame();
        maths::matrix_multiply(&matrix, &matrix, &tracker->turn());
        maths::matrix_multiply(&display_matrix_, &display_matrix_, &get_tracker(this)->turn());
    }
}

// 0x00509A40
int SkaterPhysicsModule::get_jump(script::Struct* params, script::Script* /*script*/)
{
    perform_leap(params);
    return 1;
}

// 0x00509A60
int SkaterPhysicsModule::start_skitch(script::Struct* /*params*/, script::Script* /*script*/)
{
    begin_skitch();
    return 1;
}

// 0x00509A70
int SkaterPhysicsModule::end_tow_ride(script::Struct* /*params*/, script::Script* /*script*/)
{
    end_skitch();
    return 1;
}

// 0x00509A80
int SkaterPhysicsModule::change_phase(script::Struct* params, script::Script* /*script*/)
{
    uint32_t state = 0;
    params->read_hash_value(0U, &state, 0);
    if (state_of(this) == kLip && state != kLipName) {
        trigger_rail_script(kOffLipEvents);
    }
    if (state_of(this) != kLip && state == kLipName) {
        rail_node_ = nullptr;
    }
    if (state_of(this) == kWallRide) {
        turn_to_normal(maths::Vec4{.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F});
        display_matrix_ = entity()->basis_frame();
    }
    switch (state) {
    case kAirName:
        set_physics_state(kAir);
        break;
    case kGroundName:
        feelers_.floor_feeler.hit().face_flag40_ = 0;
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
int SkaterPhysicsModule::trigger_grind_off(script::Struct* /*params*/, script::Script* /*script*/)
{
    trigger_rail_script(kGrindOffEvents);
    return 1;
}

// 0x00509B90
int SkaterPhysicsModule::force_break_vert(script::Struct* /*params*/, script::Script* /*script*/)
{
    break_vert(true);
    keep_upright();
    return 1;
}

// 0x00509BB0
int SkaterPhysicsModule::walking_wallplant(script::Struct* /*params*/, script::Script* /*script*/)
{
    wall_plant(false, false, true);
    return 1;
}

} // namespace skater
