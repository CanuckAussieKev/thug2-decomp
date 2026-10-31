// The skater's core physics component: jumping, skitching, the script member functions around them and the
// wallplant state's update.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/gfx/d3dx.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/obj/trigger_component.h"
#include "game/obj/walk_hang.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/skater/thaw_bmx.h"
#include "game/skate/skate_module.h"
#include "game/obj/trigger_events.h"

namespace Skater {

using namespace CoreFields;

namespace {

// The checksums used here.
constexpr uint32_t kTriggerScript = 0x2CA8A299;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kNoSound = 0xE13620A8;           // no_sound
constexpr uint32_t kBonelessHeight = 0xEC6B7FC7;    // BonelessHeight
constexpr uint32_t kSpeed = 0xF0D90109;             // speed
constexpr uint32_t kMaxTenseTime = 0xF733C097;      // Skater_max_tense_time
constexpr uint32_t kMaxMaxSpeed = 0xCC5F87AA;       // Skater_Max_Max_Speed_Stat
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

constexpr float kFeelerAhead = 12.0f;  // 0x0064546C (also how far a jump off a steep ramp lifts the skater)
constexpr float kFeelerBehind = -36.0f; // 0x0064BD68
constexpr float kSteepUp = -0.1f;      // 0x0064BD44: the up's y below this: a ramp too steep to jump straight off
constexpr float kSteepDrop = 1.5f;     // 0x00646744

float ScaledPhysics(void* object, uint32_t name) { return static_cast<Obj::CSkater*>(object)->GetScaledPhysics(name); }

// 0x00518EE0: the jump sound, by how fast the skater goes (a fraction of the max max speed) and a terrain.
void PlayJumpSound(void* component, uint8_t terrain)
{
    void* object = ObjectOf(component);
    const Mth::Vector& v = VectorAt(object, kVelocity);
    float speed = static_cast<float>(std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y +
                                               static_cast<double>(v.z) * v.z));
    float fraction = speed / ScaledPhysics(object, kMaxMaxSpeed);
    static_cast<Obj::CSkaterSoundComponent*>(At<void*>(component, kSoundComponent))->PlayJumpInState(fraction, terrain);
}

} // namespace

// 0x005092E0
void CorePhysicsComponent::TriggerRailScript(const int* events)
{
    auto* node = At<Obj::CRailNode*>(this, kRailNode);
    if (!node)
        return;
    if (!At<void*>(this, kRailManager))
        At<void*>(this, kRailManager) = At<void*>(reinterpret_cast<uint8_t*>(Mdl::g_skateModule), 0x24); // (the skate module's)
    Script::CStruct* script = SkateScript::GetNode(node->m_name, false);
    if (!script)
        return;
    // The node's TriggerScript, else the first one back along the rail (the walk stops at a loop: a second
    // pointer going half as fast).
    uint32_t triggerScript = 0;
    if (!script->GetChecksum(kTriggerScript, &triggerScript, 0)) {
        Obj::CRailNode* slow = node;
        bool step = false;
        for (Obj::CRailNode* p = node->m_prev; p && p != node && p != slow;) {
            script = SkateScript::GetNode(p->m_name, true);
            if (script->GetChecksum(kTriggerScript, &triggerScript, 0)) // (a node without a script: a crash)
                break;
            p = p->m_prev;
            if (step)
                slow = slow->m_prev;
            step = !step;
        }
    }
    if (!triggerScript)
        return;
    uint32_t name = 0; // (the original's is a stale stack word - a node pointer - if a node had no Name)
    script->GetChecksum(kName, &name, 1);
    SetFlag(this, 0xC8, name != At<uint32_t>(this, kLastRailScript));
    At<uint32_t>(this, kRailScriptNode) = name;
    At<uint32_t>(this, kLastRailScript) = name;
    const uint8_t* rideObject = nullptr;
    if (Obj::CRideTracker* tracker = Tracker(this)) {
        if (tracker->m_object.mp_object)
            rideObject = reinterpret_cast<const uint8_t*>(tracker->m_object.mp_object);
        else
            StopRiding(this);
    }
    static_cast<Obj::CTriggerComponent*>(At<void*>(this, kTriggerComponent))
        ->TriggerNode(events, At<uint32_t>(this, kRailScriptNode), nullptr, rideObject);
}

// 0x00509450
void CorePhysicsComponent::DoJump(Script::CStruct* params)
{
    bool sound = !params->ContainsFlag(kNoSound);
    switch (StateOf(this)) {
    case kGround:
        TriggerFeeler(this, g_jumpTriggerEvents, kOtherFeeler);
        if (TriggerTookOver(this))
            return;
        std::memcpy(&At<Mth::Vector>(this, 0x338), &VectorAt(ObjectOf(this), kPosition), 16); // (where it jumped)
        if (sound)
            PlayJumpSound(this, At<uint8_t>(this, 0x1AA));
        StartVertAir();
        SetFlag(this, 0x48, 1);
        break;
    case kAir:
        if (sound)
            PlayJumpSound(this, At<uint8_t>(this, 0x1AA));
        break;
    case kRail:
        if (sound)
            PlayJumpSound(this, Flag(this, 0xE1));
        break;
    default:
        break;
    }

    // The speed: between the min and the max by how long the jump was held.
    int maxTense = Script::GetInteger(kMaxTenseTime);
    int& tense = At<int>(this, kTenseTime);
    if (tense >= maxTense)
        tense = maxTense;
    bool fromVert = Flag(this, 0x30) || (StateOf(this) == kGround && Flag(this, 0x40));
    const uint32_t* names = kJumpSpeed[params->ContainsFlag(kBonelessHeight)][fromVert];
    void* object = ObjectOf(this);
    float most = ScaledPhysics(object, names[0]);
    float least = ScaledPhysics(ObjectOf(this), names[1]);
    double held = static_cast<double>(tense) / static_cast<double>(maxTense);
    float speed = static_cast<float>((static_cast<double>(most) - least) * held + least);
    params->GetFloat(kSpeed, &speed, 0);

    SetFlag(this, 0x20, 0);
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    if (Flag(this, 0x30) && velocity.y < 0.0f) {
        // Off vert, going down: pushed off the ramp (along +0x58) instead; no longer on vert.
        const Mth::Vector& away = VectorAt(this, 0x58);
        float z = speed * away.z, w = speed * away.w;
        velocity.x = static_cast<float>(static_cast<double>(speed) * away.x + velocity.x);
        velocity.y = static_cast<float>(static_cast<double>(speed) * away.y + velocity.y);
        velocity.z += z;
        velocity.w += w;
        speed = 0.0f;
        SetFlag(this, 0x30, 0);
        At<uint8_t>(this, 0x2D0) = 0;
    } else if (velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }
    VectorAt(ObjectOf(this), kVelocity).y += speed;
    bool bike = ThawBmx::Active(this); // (not original: THAW 0x005DDDC0's bike branches, thaw_bmx_physics.cpp)
    if (bike)
        ThawBmx::JumpBoost(this);
    object = ObjectOf(this);
    if (VectorAt(object, kUp).y < kSteepUp) {
        // A ramp too steep (upside down): less up, and lifted off it along the up.
        Mth::Vector& v = VectorAt(object, kVelocity);
        v.y = static_cast<float>(v.y - static_cast<double>(speed) * kSteepDrop);
        const Mth::Vector& up = VectorAt(object, kUp);
        Mth::Vector& position = VectorAt(object, kPosition);
        float z = up.z * kFeelerAhead, w = up.w * kFeelerAhead;
        position.x = static_cast<float>(static_cast<double>(up.x) * kFeelerAhead + position.x);
        position.y = static_cast<float>(static_cast<double>(up.y) * kFeelerAhead + position.y);
        position.z += z;
        position.w += w;
    }
    int state = StateOf(this);
    if (state == kRail || (state == kAir && At<int>(this, 0x94) == kRail))
        TurnForRailJump();
    if (At<uint8_t>(this, 0x2EC))
        TriggerRailScript(g_jumpEvents);
    if (bike)
        ThawBmx::JumpPitch(this);
    SetPhysicsState(kAir);
    At<int>(this, kJumpTime) = Now();
    static_cast<Obj::CObject*>(ObjectOf(this))->BroadcastEvent(kSkaterJump, nullptr, 0.0f);
}

// 0x00509920
void CorePhysicsComponent::BeginSkitch()
{
    auto* object = At<Obj::CCompositeObject*>(this, kSkitched);
    if (!object)
        return;
    At<Walk::MovableContact*>(this, kRide)->StartRiding(object);
    SetFlag(this, 0x88, 1);
    At<uint8_t>(this, 0x24A) = 1;
    Skitch();
    if (auto* skitched = At<Obj::CObject*>(this, kSkitched))
        skitched->SelfEvent(0x35224F25, nullptr);
}

// 0x00509990
void CorePhysicsComponent::EndSkitch()
{
    if (!Flag(this, 0x88))
        return;
    SetFlag(this, 0x88, 0);
    auto& skitched = At<Obj::CSmtPtr<Obj::CCompositeObject>>(this, kSkitched);
    if (skitched.mp_object) {
        static_cast<Obj::CObject*>(skitched.mp_object)->SelfEvent(0x2739B86D, nullptr);
        skitched.Release();
        skitched.mp_object = nullptr;
    }
    void* ride = At<void*>(this, kRide);
    if (Obj::CRideTracker* tracker = At<Obj::CRideTracker*>(ride, 0x18)) {
        At<void*>(ride, 0x6C) = tracker->m_object.mp_object; // (what it rode last)
        reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>*>(&tracker->m_object)->Release();
        Mem::Manager::Get()->Free(tracker);
        At<Obj::CRideTracker*>(ride, 0x18) = nullptr;
        At<int>(ride, 0x70) = Now();
    }
}

// 0x00509050
void CorePhysicsComponent::DoWallplantPhysics()
{
    int planted = At<int>(this, 0x90);
    At<uint8_t>(this, 0x2E4) = 0;
    uint32_t elapsed = Since(static_cast<uint32_t>(planted));
    if (static_cast<float>(elapsed) > Script::GetFloat(kWallplantDuration)) {
        // Over: into the air, with a feeler along the facing (12 ahead to 36 behind) for a trigger.
        SetPhysicsState(kAir);
        Mth::Vector& start = VectorAt(this, kFeelerStart);
        Mth::Vector& end = VectorAt(this, kFeelerEnd);
        start = VectorAt(ObjectOf(this), kFacing) * kFeelerAhead;
        end = VectorAt(ObjectOf(this), kFacing) * kFeelerBehind;
        start += VectorAt(ObjectOf(this), kPosition);
        end += VectorAt(ObjectOf(this), kPosition);
        if (Feel(this, 0x10, 0))
            TriggerFeeler(this, g_wallplantEvents, kFeeler);
        return;
    }
    // Still on the wall: no rail; carried along by what the skater rides on (its turn too, when it turned).
    At<void*>(this, kRailNode) = nullptr;
    At<uint32_t>(this, kLastRailScript) = 0;
    Obj::CRideTracker* tracker = Tracker(this);
    if (!tracker)
        return;
    if (!tracker->m_object.mp_object || !tracker->Update(&VectorAt(ObjectOf(this), kPosition))) {
        StopRiding(this);
        return;
    }
    VectorAt(ObjectOf(this), kPosition) += Tracker(this)->m_carried;
    tracker = Tracker(this);
    if (tracker->m_turned) {
        auto multiply = D3dx::MatrixMultiply;
        Mth::Matrix& matrix = MatrixAt(ObjectOf(this), kMatrix);
        multiply(&matrix, &matrix, &tracker->m_turn);
        multiply(&MatrixAt(this, kDisplayMatrix), &MatrixAt(this, kDisplayMatrix), &Tracker(this)->m_turn);
    }
}

// 0x00509A40
int CorePhysicsComponent::Jump(Script::CStruct* params, Script::CScript*)
{
    DoJump(params);
    return 1;
}

// 0x00509A60
int CorePhysicsComponent::StartSkitch(Script::CStruct*, Script::CScript*)
{
    BeginSkitch();
    return 1;
}

// 0x00509A70
int CorePhysicsComponent::StopSkitch(Script::CStruct*, Script::CScript*)
{
    EndSkitch();
    return 1;
}

// 0x00509A80
int CorePhysicsComponent::SetState(Script::CStruct* params, Script::CScript*)
{
    uint32_t state = 0;
    params->GetChecksum(0u, &state, 0);
    if (StateOf(this) == kLip && state != kLipName)
        TriggerRailScript(g_offLipEvents);
    if (StateOf(this) != kLip && state == kLipName)
        At<void*>(this, kRailNode) = nullptr;
    if (StateOf(this) == kWallRide) {
        TurnToNormal(Mth::Vector{0.0f, 1.0f, 0.0f, 1.0f});
        MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
    }
    switch (state) {
    case kAirName:
        SetPhysicsState(kAir);
        break;
    case kGroundName:
        At<uint8_t>(this, 0x1C5) = 0;
        SetPhysicsState(kGround);
        break;
    case kLipName:
        SetPhysicsState(kLip);
        break;
    default:
        break;
    }
    return 1;
}

// 0x00509B70
int CorePhysicsComponent::TriggerGrindOff(Script::CStruct*, Script::CScript*)
{
    TriggerRailScript(g_grindOffEvents);
    return 1;
}

// 0x00509B90
int CorePhysicsComponent::ForceBreakVert(Script::CStruct*, Script::CScript*)
{
    BreakVert(true);
    KeepUpright();
    return 1;
}

// 0x00509BB0
int CorePhysicsComponent::WalkingWallplant(Script::CStruct*, Script::CScript*)
{
    WallPlant(false, false, true);
    return 1;
}

} // namespace Skater
