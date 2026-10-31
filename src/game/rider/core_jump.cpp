// The skater's core physics component: jumping, skitching, the script member functions around them and the
// wallplant state's update.

#include <cmath>
#include <cstdint>
#include <cstring>

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
constexpr uint32_t kJumpSpeed[2][2][2] = {
    {{0x02ADE3AD, 0xC8815E43}, {0x9F79C8EA, 0x91B07824}}, // Physics_Jump_Speed_Stat / _min; Physics_Air_Jump_*
    {{0x8851A76E, 0x6E8EFE38}, {0x39766891, 0x4CDB18CB}}, // Physics_Boneless_Jump_Speed_*; Physics_Boneless_Air_*
};

constexpr float kFeelerAhead = 12.0f;   // 0x0064546C (also how far a jump off a steep ramp lifts the skater)
constexpr float kFeelerBehind = -36.0f; // 0x0064BD68
constexpr float kSteepUp = -0.1f;       // 0x0064BD44: the up's y below this: a ramp too steep to jump straight off
constexpr float kSteepDrop = 1.5f;      // 0x00646744

float scaled_physics(void* object, uint32_t name)
{
    return static_cast<objects::Rider*>(object)->get_scaled_physics(name);
}

// 0x00518EE0: the jump sound, by how fast the skater goes (a fraction of the max max speed) and a terrain.
void play_jump_sound(void* component, uint8_t terrain)
{
    void* object = object_of(component);
    const maths::Vec4& v = vector_at(object, kVelocity);
    float speed = static_cast<float>(
        std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z));
    float fraction = speed / scaled_physics(object, kSpeedCapStat);
    static_cast<objects::RiderSfxPlayer*>(get_at<void*>(component, kSfxModuleSlot))
        ->play_jump_in_state(fraction, terrain);
}

} // namespace

// 0x005092E0
void SkaterPhysicsModule::trigger_rail_script(const int* events)
{
    auto* node = get_at<objects::GrindPoint*>(this, kRailNode);
    if (!node)
        return;
    if (!get_at<void*>(this, kRailManager))
        get_at<void*>(this, kRailManager) =
            get_at<void*>(reinterpret_cast<uint8_t*>(modules::g_skate_module), 0x24); // (the skate module's)
    script::Struct* script = level_nodes::fetch_vertex(node->label_, false);
    if (!script)
        return;
    // The node's TriggerScript, else the first one back along the rail (the walk stops at a loop: a second
    // pointer going half as fast).
    uint32_t trigger_script = 0;
    if (!script->read_hash_value(kTriggerScript, &trigger_script, 0)) {
        objects::GrindPoint* slow = node;
        bool step = false;
        for (objects::GrindPoint* p = node->last_value_; p && p != node && p != slow;) {
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
    set_flag(this, 0xC8, name != get_at<uint32_t>(this, kLastRailScript));
    get_at<uint32_t>(this, kRailScriptNode) = name;
    get_at<uint32_t>(this, kLastRailScript) = name;
    const uint8_t* ride_object = nullptr;
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (tracker->entity_.owner_object_)
            ride_object = reinterpret_cast<const uint8_t*>(tracker->entity_.owner_object_);
        else
            stop_riding(this);
    }
    static_cast<objects::ActivationZoneModule*>(get_at<void*>(this, kActivationZoneSlot))
        ->get_trigger_node(events, get_at<uint32_t>(this, kRailScriptNode), nullptr, ride_object);
}

// 0x00509450
void SkaterPhysicsModule::perform_leap(script::Struct* params)
{
    bool sound = !params->has_bare_flag(kNoSound);
    switch (state_of(this)) {
    case kGround:
        trigger_feeler(this, kJumpTriggerEvents, kOtherFeeler);
        if (trigger_took_over(this))
            return;
        std::memcpy(&get_at<maths::Vec4>(this, 0x338), &vector_at(object_of(this), kPosition), 16); // (where it jumped)
        if (sound)
            play_jump_sound(this, get_at<uint8_t>(this, 0x1AA));
        start_vert_air();
        set_flag(this, 0x48, 1);
        break;
    case kAir:
        if (sound)
            play_jump_sound(this, get_at<uint8_t>(this, 0x1AA));
        break;
    case kRail:
        if (sound)
            play_jump_sound(this, bit_mark(this, 0xE1));
        break;
    default:
        break;
    }

    // The speed: between the min and the max by how long the jump was held.
    int max_tense = script::fetch_int_value(kChargeCapMs);
    int& tense = get_at<int>(this, kJumpChargeMs);
    if (tense >= max_tense)
        tense = max_tense;
    bool from_vert = bit_mark(this, 0x30) || (state_of(this) == kGround && bit_mark(this, 0x40));
    const uint32_t* names = kJumpSpeed[params->has_bare_flag(kBonelessHeight)][from_vert];
    void* object = object_of(this);
    float most = scaled_physics(object, names[0]);
    float least = scaled_physics(object_of(this), names[1]);
    double held = static_cast<double>(tense) / static_cast<double>(max_tense);
    float speed = static_cast<float>((static_cast<double>(most) - least) * held + least);
    params->fetch_float(kSpeed, &speed, 0);

    set_flag(this, 0x20, 0);
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    if (bit_mark(this, 0x30) && velocity.y < 0.0f) {
        // Off vert, going down: pushed off the ramp (along +0x58) instead; no longer on vert.
        const maths::Vec4& away = vector_at(this, 0x58);
        float z = speed * away.z, w = speed * away.w;
        velocity.x = static_cast<float>(static_cast<double>(speed) * away.x + velocity.x);
        velocity.y = static_cast<float>(static_cast<double>(speed) * away.y + velocity.y);
        velocity.z += z;
        velocity.w += w;
        speed = 0.0f;
        set_flag(this, 0x30, 0);
        get_at<uint8_t>(this, 0x2D0) = 0;
    } else if (velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }
    vector_at(object_of(this), kVelocity).y += speed;
    bool bike = thaw_bmx::live_now(this); // (not original: THAW 0x005DDDC0's bike branches, thaw_bmx_physics.cpp)
    if (bike)
        thaw_bmx::jump_boost(this);
    object = object_of(this);
    if (vector_at(object, kUp).y < kSteepUp) {
        // A ramp too steep (upside down): less up, and lifted off it along the up.
        maths::Vec4& v = vector_at(object, kVelocity);
        v.y = static_cast<float>(v.y - static_cast<double>(speed) * kSteepDrop);
        const maths::Vec4& up = vector_at(object, kUp);
        maths::Vec4& position = vector_at(object, kPosition);
        float z = up.z * kFeelerAhead, w = up.w * kFeelerAhead;
        position.x = static_cast<float>(static_cast<double>(up.x) * kFeelerAhead + position.x);
        position.y = static_cast<float>(static_cast<double>(up.y) * kFeelerAhead + position.y);
        position.z += z;
        position.w += w;
    }
    int state = state_of(this);
    if (state == kRail || (state == kAir && get_at<int>(this, 0x94) == kRail))
        turn_for_rail_jump();
    if (get_at<uint8_t>(this, 0x2EC))
        trigger_rail_script(kJumpEvents);
    if (bike)
        thaw_bmx::jump_pitch(this);
    set_physics_state(kAir);
    get_at<int>(this, kLeapDuration) = get_now();
    static_cast<objects::Entity*>(object_of(this))->announce(kSkaterJump, nullptr, 0.0f);
}

// 0x00509920
void SkaterPhysicsModule::begin_skitch()
{
    auto* object = get_at<objects::GameEntity*>(this, kSkitched);
    if (!object)
        return;
    get_at<walk::MovableContact*>(this, kRide)->start_riding(object);
    set_flag(this, 0x88, 1);
    get_at<uint8_t>(this, 0x24A) = 1;
    get_skitch();
    if (auto* skitched = get_at<objects::Entity*>(this, kSkitched))
        skitched->post_own_event(0x35224F25, nullptr);
}

// 0x00509990
void SkaterPhysicsModule::end_skitch()
{
    if (!bit_mark(this, 0x88))
        return;
    set_flag(this, 0x88, 0);
    auto& skitched = get_at<objects::SharedRef<objects::GameEntity>>(this, kSkitched);
    if (skitched.owner_object_) {
        static_cast<objects::Entity*>(skitched.owner_object_)->post_own_event(0x2739B86D, nullptr);
        skitched.get_release();
        skitched.owner_object_ = nullptr;
    }
    void* ride = get_at<void*>(this, kRide);
    if (objects::RideTracker* tracker = get_at<objects::RideTracker*>(ride, 0x18)) {
        get_at<void*>(ride, 0x6C) = tracker->entity_.owner_object_; // (what it rode last)
        reinterpret_cast<objects::SharedRef<objects::SharedBase>*>(&tracker->entity_)->get_release();
        memory::Hub::get_pull()->dealloc(tracker);
        get_at<objects::RideTracker*>(ride, 0x18) = nullptr;
        get_at<int>(ride, 0x70) = get_now();
    }
}

// 0x00509050
void SkaterPhysicsModule::step_wallplant()
{
    int planted = get_at<int>(this, 0x90);
    get_at<uint8_t>(this, 0x2E4) = 0;
    uint32_t elapsed = get_since(static_cast<uint32_t>(planted));
    if (static_cast<float>(elapsed) > script::fetch_float(kWallplantDuration)) {
        // Over: into the air, with a feeler along the facing (12 ahead to 36 behind) for a trigger.
        set_physics_state(kAir);
        maths::Vec4& start = vector_at(this, kProbeOrigin);
        maths::Vec4& end = vector_at(this, kProbeTip);
        start = vector_at(object_of(this), kFacing) * kFeelerAhead;
        end = vector_at(object_of(this), kFacing) * kFeelerBehind;
        start += vector_at(object_of(this), kPosition);
        end += vector_at(object_of(this), kPosition);
        if (get_feel(this, 0x10, 0))
            trigger_feeler(this, kWallplantEvents, kFeeler);
        return;
    }
    // Still on the wall: no rail; carried along by what the skater rides on (its turn too, when it turned).
    get_at<void*>(this, kRailNode) = nullptr;
    get_at<uint32_t>(this, kLastRailScript) = 0;
    objects::RideTracker* tracker = get_tracker(this);
    if (!tracker)
        return;
    if (!tracker->entity_.owner_object_ || !tracker->tick_frame(&vector_at(object_of(this), kPosition))) {
        stop_riding(this);
        return;
    }
    vector_at(object_of(this), kPosition) += get_tracker(this)->carried_;
    tracker = get_tracker(this);
    if (tracker->turned_) {
        auto multiply = d3dx::matrix_multiply;
        maths::Mat4& matrix = matrix_at(object_of(this), kMatrix);
        multiply(&matrix, &matrix, &tracker->turn_);
        multiply(&matrix_at(this, kDisplayMatrix), &matrix_at(this, kDisplayMatrix), &get_tracker(this)->turn_);
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
        trigger_rail_script(kOffLipEvents);
    if (state_of(this) != kLip && state == kLipName)
        get_at<void*>(this, kRailNode) = nullptr;
    if (state_of(this) == kWallRide) {
        turn_to_normal(maths::Vec4{0.0f, 1.0f, 0.0f, 1.0f});
        matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
    }
    switch (state) {
    case kAirName:
        set_physics_state(kAir);
        break;
    case kGroundName:
        get_at<uint8_t>(this, 0x1C5) = 0;
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
    trigger_rail_script(kGrindOffEvents);
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
