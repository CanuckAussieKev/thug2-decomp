// The skater's core physics component, with PartyMod's PS2-style controls built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/skater/core_physics.h"

#include <chrono>
#include <cmath>
#include <cstring>

#include "game/core/crc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/control_pad.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/trigger_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/collision_events.h"
#include "game/skater/controls.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/skater/skater_state.h"
#include "game/skater/trick_component.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace Skater {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The component's fields (offsets).
constexpr uint32_t kObject = 0xC;            // the skater's object (below)
constexpr uint32_t kDisplayMatrix = 0x18;    // (a copy of the object's matrix)
constexpr uint32_t kGroundNormal = 0x68;     // (a vector the wall check turns the facing into)
constexpr uint32_t kFeeler = 0xA8;           // a collision feeler (its result is copied below)
constexpr uint32_t kSurfaceFlags = 0xFA;     // (a byte passed on when landing)
constexpr uint32_t kHitPoint = 0x120;        // the feeler's hit: point, then normal
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kOtherFeeler = 0x158;     // another feeler
constexpr uint32_t kFeelerStart = 0x208;     // the feeler's line
constexpr uint32_t kFeelerEnd = 0x218;
constexpr uint32_t kHitSkatable = 0x228;     // (set by the feeler check 0x005003A0)
constexpr uint32_t kHitFlag = 0x229;         // (likewise)
constexpr uint32_t kWallNormal = 0x278;      // riding a wall: its normal (+0x268: the point, +0x26C its height)
constexpr uint32_t kInput = 0x150C;          // the pad: a button every 0x20 bytes (pressed, then times)
constexpr uint32_t kTriggerComponent = 0x1510;
constexpr uint32_t kOtherComponent = 0x1514; // (+0x1C, +0x1D: bytes set on landing)
constexpr uint32_t kSkaterComponent = 0x1518;
constexpr uint32_t kOtherFlags = 0x151C;     // (+0x28, +0x50: bools)
constexpr uint32_t kTimedFlags = 0x1528;     // bools, each followed by the time it last changed
constexpr uint32_t kRide = 0x152C;           // +0x18: what the skater rides on (+0x5C: how far it moved)
constexpr uint32_t kTriggerState = 0x1530;   // +0x26: a trigger's script took over

// The object's fields.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kOldPosition = 0x5C;
constexpr uint32_t kMatrix = 0x6C;
constexpr uint32_t kUp = 0x7C;      // the matrix's rows (at +0x6C): up, then the facing
constexpr uint32_t kFacing = 0x8C;
constexpr uint32_t kVelocity = 0xAC;

// The input's buttons (offsets into the pad).
constexpr uint32_t kButton20 = 0x20;
constexpr uint32_t kButton40 = 0x40;
constexpr uint32_t kButton60 = 0x60;
constexpr uint32_t kButton80 = 0x80;
constexpr uint32_t kTrigger1 = 0xC0;
constexpr uint32_t kTrigger2 = 0x120;

Mth::Vector& VectorAt(void* object, uint32_t offset) { return At<Mth::Vector>(object, offset); }

// 0x00580400 / 0x00580440: a physics value (the skater's own tuning structure for `object`, else the global
// script value).
float PhysicsFloat(uint32_t name, void* object)
{
    return Obj::GetPhysicsFloat(name, 1, static_cast<Obj::CCompositeObject*>(object));
}
int PhysicsInteger(uint32_t name, void* object)
{
    return Obj::GetPhysicsInt(name, 1, static_cast<Obj::CCompositeObject*>(object));
}

// The game's clock (0x0044ECE0, plus the offset at 0x00696D60).
int Now()
{
    int time = Tmr::GameClockElapsed();
    return time + GameVariable<int>(0x00696D60);
}

// A timed flag: a bool and, after it, the time it last changed.
void SetTimedFlag(void* flags, uint32_t offset, uint8_t value)
{
    uint8_t& flag = At<uint8_t>(flags, offset);
    if (flag == value)
        return;
    flag = value;
    At<int>(flags, offset + 4) = Now();
}

// A button's times: since it was last pressed (0x0046B920) / released (0x0046B900).
int TimeSincePress(uint8_t* button) { return reinterpret_cast<Obj::CControlButton*>(button)->GetPressedTime(); }
int TimeSinceRelease(uint8_t* button) { return reinterpret_cast<Obj::CControlButton*>(button)->GetReleasedTime(); }

// The components the skater's physics reaches through its own fields.
Obj::CTriggerComponent* TriggerOf(void* component)
{
    return static_cast<Obj::CTriggerComponent*>(At<void*>(component, kTriggerComponent));
}
Nx::CFeeler* FeelerAt(void* component, uint32_t offset) { return &At<Nx::CFeeler>(component, offset); }
const int* Events(uint32_t address) { return reinterpret_cast<const int*>(address); }
void StopRiding(void* ride) { static_cast<Obj::CMovableContactComponent*>(ride)->StopRiding(); }

// The two triggers: both held - or, under PartyMod's PS2 controls (its JZ -> JNZ at each check), either.
bool TriggersHeld(const uint8_t* input)
{
    if (Ps2Controls())
        return input[kTrigger1] || input[kTrigger2];
    return input[kTrigger1] && input[kTrigger2];
}

// velocity += direction * scale, the x component's product unrounded (the original keeps it on the x87 stack).
void Push(Mth::Vector& velocity, const Mth::Vector& direction, float scale)
{
    float y = direction.y * scale;
    float z = direction.z * scale;
    float w = direction.w * scale;
    velocity.x = static_cast<float>(velocity.x + static_cast<double>(direction.x) * scale);
    velocity.y += y;
    velocity.z += z;
    velocity.w += w;
}

} // namespace

// 0x004FCBA0
int CorePhysicsComponent::LipPushOff(Script::CStruct*, Script::CScript*)
{
    void* object = At<void*>(this, kObject);
    const Mth::Vector& facing = VectorAt(object, kFacing);
    Mth::Vector forward = {facing.x, 0.0f, facing.z, facing.w};
    double length = std::sqrt(static_cast<double>(forward.z) * forward.z + static_cast<double>(forward.x) * forward.x);
    if (length > 0.0) {
        double scale = 1.0 / length; // (w isn't scaled; y stays 0)
        forward.x = static_cast<float>(forward.x * scale);
        forward.z = static_cast<float>(forward.z * scale);
    }
    Mth::Vector side = {forward.z, 0.0f, -forward.x, 0.0f};

    uint8_t* input = At<uint8_t*>(this, kInput);
    if (input[kButton80])
        return 0;
    // (The original: both triggers. PartyMod's JZ -> JNZ at 0x004FCC59 makes it either.)
    if (TriggersHeld(input)) {
        float scale = PhysicsFloat(0x1F96C224, object);
        Push(VectorAt(At<void*>(this, kObject), kVelocity), forward, scale);
        return 1;
    }
    if (input[kButton60]) {
        int wait = PhysicsInteger(0x3080E9E7, object);
        if (TimeSincePress(input + kButton60) > wait) {
            float scale = PhysicsFloat(0xDC818B7E, object);
            Push(VectorAt(At<void*>(this, kObject), kVelocity), forward, scale);
            return 1;
        }
    }
    if (input[kButton20]) {
        int wait = PhysicsInteger(0x9B25142A, object);
        if (TimeSincePress(input + kButton20) > wait) {
            float scale = PhysicsFloat(0x1AB3809F, object);
            Mth::Vector& velocity = VectorAt(At<void*>(this, kObject), kVelocity);
            // velocity -= side * scale (x's product unrounded, as above)
            float y = scale * 0.0f;
            float z = -forward.x * scale;
            velocity.x = static_cast<float>(velocity.x - static_cast<double>(forward.z) * scale);
            velocity.y -= y;
            velocity.z -= z;
            velocity.w -= y;
            return 1;
        }
    }
    if (input[kButton40]) {
        int wait = PhysicsInteger(0x9B25142A, object);
        if (TimeSincePress(input + kButton40) > wait) {
            float scale = PhysicsFloat(0x1AB3809F, object);
            VectorAt(At<void*>(this, kObject), kVelocity) += side * scale;
            return 1;
        }
    }
    return 0;
}

namespace {

// Not original - [features] debug_rail_speed=1: what the rail speed-up loops do (THAW's RailAccelerate2, which
// the THAW ports carry, and the level editor's ed_RailSpeedLoop: while the skater grinds and climbs, SetSpeed
// with the speed * (1 + rate) each frame). A loop's first boost (a new loop, or one after a pause of half a
// second) and then, once a second, the boosts in it: how many, the speed (game units a second - the length of the
// velocity, as GetSpeed reads it) at the first and at the last, the height. (Only boosts are seen: a loop on a rail
// that doesn't climb boosts nothing and logs nothing.)
struct RailBoostLog {
    Script::CScript* script = nullptr;
    std::chrono::steady_clock::time_point last, windowStart;
    int boosts = 0;
    float speedFrom = 0.0f, heightFrom = 0.0f;
};

void LogRailBoost(Script::CScript* script, float before, float after, float height)
{
    static const bool on = ConfigInt("features", "debug_rail_speed", 0) != 0;
    if (!on || !script)
        return;
    static const uint32_t kLoops[] = {Crc::GenerateCRCFromString("RailAccelerate2"),
                                      Crc::GenerateCRCFromString("ed_RailSpeedLoop")};
    if (script->m_scriptName != kLoops[0] && script->m_scriptName != kLoops[1])
        return;
    static RailBoostLog log;
    auto now = std::chrono::steady_clock::now();
    auto seconds = [](std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); };
    if (script != log.script || seconds(now - log.last) > 0.5) {
        float rate = 0.0f;
        script->m_params->GetFloat(Crc::GenerateCRCFromString("rate"), &rate, 0);
        Log("rail speed-up: %s boosting (rate %.3f): speed %.0f -> %.0f, height %.0f",
            script->m_scriptName == kLoops[0] ? "RailAccelerate2" : "ed_RailSpeedLoop", rate, before, after, height);
        log.script = script;
        log.windowStart = now;
        log.boosts = 0;
        log.speedFrom = before;
        log.heightFrom = height;
    } else {
        ++log.boosts;
        double window = seconds(now - log.windowStart);
        if (window >= 1.0) {
            // (the speed the skater had before each boost - what the rail physics and the speed limit left of the
            // last one)
            Log("rail speed-up: %.2f s, %d boosts: speed %.0f -> %.0f (%+.0f a second; this boost to %.0f), height "
                "%.0f -> %.0f",
                window, log.boosts, log.speedFrom, before, (before - log.speedFrom) / window, after, log.heightFrom,
                height);
            log.windowStart = now;
            log.boosts = 0;
            log.speedFrom = before;
            log.heightFrom = height;
        }
    }
    log.last = now;
}

} // namespace

// 0x004FCE90
int CorePhysicsComponent::SetSpeed(Script::CStruct* params, Script::CScript* script)
{
    float speed = 0.0f;
    params->GetFloat(0u, &speed, 0); // (unnamed)
    void* object = At<void*>(this, kObject);
    Mth::Vector& velocity = VectorAt(object, kVelocity);
    double lengthSqr = static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y +
                       static_cast<double>(velocity.z) * velocity.z;
    float before = static_cast<float>(std::sqrt(lengthSqr));
    if (lengthSqr < static_cast<double>(0.001f)) { // (not taken for a NaN)
        std::memcpy(&velocity, &VectorAt(object, kFacing), sizeof(velocity)); // (moved, not loaded as floats)
    } else {
        double scale = 1.0 / std::sqrt(lengthSqr);
        velocity.x = static_cast<float>(scale * velocity.x);
        velocity.y = static_cast<float>(scale * velocity.y);
        velocity.z = static_cast<float>(scale * velocity.z);
        velocity.w = static_cast<float>(scale * velocity.w);
    }
    velocity.x = speed * velocity.x;
    velocity.y = speed * velocity.y;
    velocity.z = speed * velocity.z;
    velocity.w = speed * velocity.w;
    LogRailBoost(script, before, speed, VectorAt(object, kPosition).y);
    return 1;
}

// 0x00509BD0
void CorePhysicsComponent::CheckForGround()
{
    void* object = At<void*>(this, kObject);
    uint8_t* input = At<uint8_t*>(this, kInput);
    // The feeler: from above the skater (up * a physics value) to 200 below, along its up axis.
    float above = PhysicsFloat(0xE4D79235, object);
    Mth::Vector& start = VectorAt(this, kFeelerStart);
    Mth::Vector& end = VectorAt(this, kFeelerEnd);
    start = VectorAt(object, kUp) * above;
    end = VectorAt(object, kUp) * -200.0f;
    start += VectorAt(object, kPosition);
    end += VectorAt(object, kPosition);

    if (CheckFeeler(0x10, 0)) {
        const Mth::Vector& position = VectorAt(object, kPosition);
        const Mth::Vector& point = VectorAt(this, kHitPoint);
        const Mth::Vector& up = VectorAt(object, kUp);
        double dx = static_cast<double>(position.x) - point.x;
        double dy = static_cast<double>(position.y) - point.y;
        double dz = static_cast<double>(position.z) - point.z;
        float distance = static_cast<float>(std::sqrt(dx * dx + dz * dz + dy * dy));
        float along = static_cast<float>(dz * up.z + dy * up.y + dx * up.x); // (how far above the hit, along up)

        if (!At<uint8_t>(this, kHitSkatable)) {
            // Not ground: if the skater is (nearly) at or below it, move it out by the hit's normal; then leave.
            if (along < 0.001f) {
                Mth::Vector& p = VectorAt(object, kPosition);
                const Mth::Vector& normal = VectorAt(this, kHitNormal);
                float x = point.x + normal.x, y = point.y + normal.y, z = point.z + normal.z, w = point.w + normal.w;
                p.z = z;
                p.w = w;
                p.x = x;
                p.y = y;
            }
            CollisionEvents::NotePath(this, CollisionEvents::Path::GroundUnskatable,
                                      VectorAt(object, kVelocity)); // (not original)
        } else {
            // Ground. The facing turned into the hit's plane and into the ground's plane: a hit at too steep
            // an angle to the current ground (cos of a physics angle - one value with button +0x20 held,
            // another without), or too far away for how fast the skater moves, isn't kept to.
            Mth::Vector normal = VectorAt(this, kHitNormal);
            const Mth::Vector& facing = VectorAt(object, kFacing);
            Mth::Vector inHitPlane = facing;
            Mth::Vector inGroundPlane = facing;
            float facingIntoHit = static_cast<float>(static_cast<double>(facing.x) * normal.x +
                                                     static_cast<double>(facing.z) * normal.z +
                                                     static_cast<double>(facing.y) * normal.y);
            inHitPlane.RotateToPlane(normal);
            inGroundPlane.RotateToPlane(VectorAt(this, kGroundNormal));
            float turn = static_cast<float>(static_cast<double>(inGroundPlane.x) * inHitPlane.x +
                                            static_cast<double>(inGroundPlane.z) * inHitPlane.z +
                                            static_cast<double>(inGroundPlane.y) * inHitPlane.y);
            float angle = PhysicsFloat(input[kButton20] ? 0x4138283E : 0xEF161C2A, object);
            double limit = std::cos(static_cast<double>(angle) * 0.0174532924f); // (degrees to radians)
            bool keep = true;
            if (facingIntoHit > 0.0f && turn > 0.0f && turn < limit)
                keep = false;
            if (keep && along > 0.0f) {
                // How far the skater could have dropped this frame at that angle (at least a physics value).
                float cosine = turn > 1.0f ? 1.0f : (turn < -1.0f ? -1.0f : turn);
                const Mth::Vector& p = VectorAt(object, kPosition);
                const Mth::Vector& old = VectorAt(object, kOldPosition);
                float mx = p.x - old.x, my = p.y - old.y, mz = p.z - old.z;
                double moved = std::sqrt(static_cast<double>(mx) * mx + static_cast<double>(mz) * mz +
                                         static_cast<double>(my) * my);
                float reach = static_cast<float>(std::tan(std::acos(static_cast<double>(cosine))) * moved);
                float least = PhysicsFloat(0x899BA3D0, object);
                if (reach < least)
                    reach = least;
                if (distance > reach)
                    keep = false;
            }
            if (keep) {
                void* flags = At<void*>(this, kTimedFlags);
                SetTimedFlag(flags, 0x40, At<uint8_t>(this, kHitFlag));
                GameFunction<void(__thiscall*)(void*, Mth::Vector)>(0x004FE860)(this, normal);
                VectorAt(object, kPosition) = VectorAt(this, kHitPoint);
                flags = At<void*>(this, kTimedFlags);
                if (At<int>(flags, 0x18) == 0 && distance > 2.0f && turn > 0.99f)
                    SetTimedFlag(flags, 0x98, 1);
                uint8_t surface = At<uint8_t>(this, kSurfaceFlags);
                static_cast<Obj::CSkaterStateComponent*>(At<void*>(this, kTimedFlags))->SetTerrain(surface);
                At<uint8_t>(At<uint8_t*>(this, kOtherComponent), 0x1C) = surface;
                GameFunction<void(__thiscall*)(void*)>(0x004FEA50)(this);
                StartRidingHitObject();
                if (At<int>(this, 0x1AC) != At<int>(this, 0xFC)) {
                    TriggerOf(this)->CheckFeeler(Events(0x0064BBA4), FeelerAt(this, kOtherFeeler));
                    if (At<uint8_t>(At<uint8_t*>(this, kTriggerState), 0x26))
                        return;
                    TriggerOf(this)->CheckFeeler(Events(0x0064BBB0), FeelerAt(this, kFeeler));
                    if (At<uint8_t>(At<uint8_t*>(this, kTriggerState), 0x26))
                        return;
                }
                void* skater = At<void*>(this, kSkaterComponent);
                if (!At<uint8_t>(skater, 0x13B0))
                    static_cast<Obj::CTrickComponent*>(skater)->SetTrickObjects(false);
                FeelerAt(this, kOtherFeeler)->Assign(*FeelerAt(this, kFeeler));
                At<uint8_t>(At<uint8_t*>(this, kOtherComponent), 0x1D) = At<uint8_t>(this, 0x1AA);
                return;
            }
            CollisionEvents::NotePath(this, CollisionEvents::Path::GroundRejected,
                                      VectorAt(object, kVelocity)); // (not original)
        }
    } else {
        CollisionEvents::NotePath(this, CollisionEvents::Path::NoGround, VectorAt(object, kVelocity)); // (not original)
    }

    // Off the ground.
    GameFunction<void(__thiscall*)(void*, int)>(0x004FDED0)(this, 1);
    CollisionEvents::Checkpoint(this, "set-air-4FDED0"); // (not original)
    static_cast<Obj::CObject*>(At<void*>(this, kObject))->BroadcastEvent(0xD96F01F1, nullptr, 0.0f);
    CollisionEvents::Checkpoint(this, "SkaterOffEdge-event"); // (not original)
    static_cast<Obj::CObject*>(At<void*>(this, kObject))->SelfEvent(0x3B1001B6, nullptr);
    CollisionEvents::Checkpoint(this, "GroundGone-event"); // (not original)
    GameFunction<void(__thiscall*)(void*)>(0x004FEBA0)(this);
    CollisionEvents::Checkpoint(this, "after-4FEBA0"); // (not original)
    void* flags = At<void*>(this, kTimedFlags);
    if (At<uint8_t>(flags, 0x30)) {
        SetTimedFlag(flags, 0x48, 1);
        // The original: unless both triggers are held. PartyMod only turns the first check's JZ into a JNZ (at
        // 0x00509E47), so under its PS2 controls this is skipped only while the second is held without the first.
        bool skip = Ps2Controls() ? !input[kTrigger1] && input[kTrigger2] : input[kTrigger1] && input[kTrigger2];
        if (!skip)
            BreakVert(false);
        int wait = Script::GetInteger(0x6BB5B751);
        if (TimeSinceRelease(input + kButton20) > wait && TimeSincePress(input + kButton20) > wait)
            SetTimedFlag(At<void*>(this, kTimedFlags), 0x48, 0);
    } else if (TriggersHeld(input)) {
        // (The original: both triggers; PartyMod's JZ -> JNZ at 0x0050A257 makes it either.)
        uint8_t drop[40];
        void* o = At<void*>(this, kObject);
        if (FindAcidDrop(true, &VectorAt(o, kPosition), &VectorAt(o, kOldPosition), &VectorAt(o, kVelocity),
                         reinterpret_cast<AcidDrop*>(drop)))
            StartAcidDrop(reinterpret_cast<const AcidDrop*>(drop));
    }
    TriggerOf(this)->CheckFeeler(Events(0x0064BB80), FeelerAt(this, kOtherFeeler));
}

namespace {

void CallThis(uint32_t address, void* object) { GameFunction<void(__thiscall*)(void*)>(address)(object); }
uint8_t& Flag(void* component, uint32_t offset) { return At<uint8_t>(At<void*>(component, kTimedFlags), offset); }
void* ObjectOf(void* component) { return At<void*>(component, kObject); }

} // namespace

// 0x0050CA00
void CorePhysicsComponent::DoAirPhysics()
{
    uint8_t* input = At<uint8_t*>(this, kInput);
    float gravity = GameFunction<float(__thiscall*)(void*)>(0x004FF470)(this);
    SetTimedFlag(At<void*>(this, kTimedFlags), 0x40, 0);
    void* flags = At<void*>(this, kTimedFlags);
    if (At<uint8_t>(flags, 0x30) || At<uint8_t>(flags, 0x78) || At<uint8_t>(flags, 0xA8) || At<uint8_t>(flags, 0x70))
        SetTimedFlag(flags, 0xB0, 1);
    At<uint8_t>(this, 0x2EC) = 0;
    LeanInAir();
    CallThis(0x004FFB90, this);
    Spin();
    RotateInAcidDrop();
    // (The original: flag +0x80 or both triggers. PartyMod's JZ -> JNZ at 0x0050CAC4 makes it either trigger.)
    if (Flag(this, 0x80) || TriggersHeld(input))
        RecoverInAir();
    {
        uint8_t* other = At<uint8_t*>(this, kOtherFlags);
        if (!Flag(this, 0x30) && !Flag(this, 0x78) && !other[0x28] && !other[0x50])
            KeepUpright();
    }
    if (At<float>(this, 0x320) != 0.0f)
        CallThis(0x004FE730, this);

    Mth::Vector velocity = VectorAt(ObjectOf(this), kVelocity);
    int& countdown = At<int>(this, 0x14CC);
    if (countdown > 0) {
        --countdown;
        if (WallPlant(true, false, false))
            countdown = 0;
        else if (countdown == 0)
            VectorAt(ObjectOf(this), kFacing).RotateToPlane(VectorAt(this, 0x14F0));
    }
    flags = At<void*>(this, kTimedFlags);
    if (At<uint8_t>(flags, 0x78)) {
        if (velocity.y < 0.0f && VectorAt(ObjectOf(this), kPosition).y < At<float>(this, 0x37C))
            VectorAt(flags, 0xE4).SetLength(0.1f);
        else
            velocity += VectorAt(flags, 0xE4);
    }

    // What the skater rides on (+0x152C's +0x18, if it's still under the skater) carries it along.
    Mth::Vector carried = {0.0f, 0.0f, 0.0f, 1.0f};
    {
        void* ride = At<void*>(this, kRide);
        uint32_t* rider = At<uint32_t*>(ride, 0x18);
        void* object = ObjectOf(this);
        if (rider) {
            if (*rider == 0)
                StopRiding(ride);
            else if (!reinterpret_cast<Obj::CRideTracker*>(rider)->Update(&VectorAt(object, kPosition)))
                StopRiding(ride);
            else if (!Flag(this, 0x78) || At<uint8_t>(this, 0x14C0))
                carried = VectorAt(At<void*>(At<void*>(this, kRide), 0x18), 0x5C);
        }
    }

    // Move: position += velocity * t + (0, gravity, 0, 1) * t * t / 2, then the carried offset; velocity +=
    // (0, gravity, 0, 1) * t. (Rounded as the original: x's velocity product and the z, w sums stay unrounded.)
    {
        float t = At<float>(this, 0x8C);
        double half = static_cast<double>(t) * t * 0.5f;
        float zeroHalf = static_cast<float>(0.0 * half);
        float halfGravity = static_cast<float>(half * gravity);
        float halfFloat = static_cast<float>(half);
        float ty = t * velocity.y, tz = t * velocity.z, tw = t * velocity.w;
        float sx = static_cast<float>(static_cast<double>(velocity.x) * t + zeroHalf);
        float sy = ty + halfGravity;
        double sz = 0.0 * half + tz;
        double sw = static_cast<double>(tw) + halfFloat;
        Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
        position.x = sx + position.x;
        position.y = sy + position.y;
        position.z = static_cast<float>(sz + position.z);
        position.w = static_cast<float>(sw + position.w);
        VectorAt(ObjectOf(this), kPosition) += carried;

        float tt = At<float>(this, 0x8C);
        float zero = 0.0f * tt;
        float fall = tt * gravity;
        Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
        v.x += zero;
        v.y += fall;
        v.z += zero;
        v.w += tt;
    }

    void* object = ObjectOf(this);
    flags = At<void*>(this, kTimedFlags);
    if (At<uint8_t>(flags, 0x48)) {
        if (At<uint8_t>(flags, 0x38) && At<uint8_t>(flags, 0x30)) {
            // On a wall: a feeler from the skater's up (+1) to 23 below; off it, break out of vert.
            const Mth::Vector& up = VectorAt(object, kUp);
            const Mth::Vector& position = VectorAt(object, kPosition);
            Mth::Vector& start = VectorAt(this, kFeelerStart);
            Mth::Vector& end = VectorAt(this, kFeelerEnd);
            float x = up.x + position.x, y = up.y + position.y, z = up.z + position.z, w = up.w + position.w;
            start.z = z, start.w = w, start.x = x, start.y = y;
            float ey = up.y * -23.0f, ez = up.z * -23.0f, ew = up.w * -23.0f;
            x = static_cast<float>(static_cast<double>(up.x) * -23.0f + position.x);
            y = ey + position.y, z = ez + position.z, w = ew + position.w;
            end.z = z, end.w = w, end.x = x, end.y = y;
            if (!CheckFeeler(0x10, 0)) {
                BreakVert(false);
                int wait = Script::GetInteger(0x6BB5B751);
                if (TimeSinceRelease(input + kButton20) > wait && TimeSincePress(input + kButton20) > wait)
                    SetTimedFlag(At<void*>(this, kTimedFlags), 0x48, 0);
            }
        }
        uint32_t stamp = At<uint32_t>(At<void*>(this, kTimedFlags), 0x4C);
        uint32_t now = static_cast<uint32_t>(Tmr::GameClockElapsed());
        int elapsed = static_cast<int>(now + (static_cast<uint32_t>(GameVariable<int>(0x00696D60)) - stamp));
        if (elapsed > Script::GetInteger(0x26495947))
            SetTimedFlag(At<void*>(this, kTimedFlags), 0x48, 0);
    } else if (At<uint8_t>(flags, 0x30)) {
        // (The original: both triggers. PartyMod's JZ -> JNZ at 0x0050CF89 makes it either.)
        if ((TriggersHeld(input) || At<uint8_t>(this, 0x2D0)) && !At<uint8_t>(flags, 0x78) &&
            VectorAt(object, kVelocity).y > 0.0f)
            BreakVert(false);
    }

    // Along a wall (+0x278: its normal; +0x26C: the height so far): a feeler across it, stepped down until it
    // hits.
    bool riding = false;
    {
        void* ride = At<void*>(this, kRide);
        uint32_t* rider = At<uint32_t*>(ride, 0x18);
        if (rider && *rider)
            riding = true;
        else if (rider)
            StopRiding(ride);
    }
    if (!riding && Flag(this, 0x38) && Flag(this, 0x30)) {
        Mth::Vector& start = VectorAt(this, kFeelerStart);
        Mth::Vector& end = VectorAt(this, kFeelerEnd);
        start.x = VectorAt(ObjectOf(this), kPosition).x;
        start.y = At<float>(this, 0x26C);
        start.z = VectorAt(ObjectOf(this), kPosition).z;
        end = start;
        const Mth::Vector& wall = VectorAt(this, kWallNormal);
        float wz = wall.z * 30.0f, ww = wall.w * 30.0f;
        start.x = static_cast<float>(static_cast<double>(wall.x) * 30.0f + start.x);
        start.y = static_cast<float>(static_cast<double>(wall.y) * 30.0f + start.y);
        start.z = wz + start.z;
        start.w = ww + start.w;
        end.x = static_cast<float>(end.x - static_cast<double>(wall.x) * 30.0f);
        end.y = static_cast<float>(end.y - static_cast<double>(wall.y) * 30.0f);
        end.z -= wz;
        end.w -= ww;
        float& step = At<float>(this, 0x298);
        bool found = false;
        if (step > 0.5f) {
            start.y = step + start.y;
            end.y = end.y + step;
            if (CheckFeeler(0, 8)) {
                found = true;
            } else {
                start.y -= step;
                end.y -= step;
                step *= 0.5f;
            }
        }
        if (!found && CheckFeeler(0, 8))
            found = true;
        for (int i = 0; !found && i < 10; ++i) {
            start.y -= 3.0f;
            end.y -= 3.0f;
            if (CheckFeeler(0, 8)) {
                At<float>(this, 0x26C) = VectorAt(this, kHitPoint).y;
                found = true;
            }
        }
        const Mth::Vector& point = VectorAt(this, kHitPoint);
        const Mth::Vector& normal = VectorAt(this, kHitNormal);
        if (found && std::sqrt(std::fabs(static_cast<double>(wall.z) * normal.z + static_cast<double>(wall.x) * normal.x)) >
                         0.02f) {
            // Still along the wall: stay on it (not below the height so far); its normal is the new wall normal,
            // and the skater is pushed off it by a script value.
            float height = point.y < At<float>(this, 0x26C) ? At<float>(this, 0x26C) : point.y;
            float hz = point.z, hw = point.w;
            At<float>(this, 0x268) = point.x;
            At<float>(this, 0x274) = hw;
            At<float>(this, 0x26C) = height;
            At<float>(this, 0x270) = hz;
            VectorAt(ObjectOf(this), kPosition).x = point.x;
            VectorAt(ObjectOf(this), kPosition).z = hz;
            Mth::Vector& newWall = VectorAt(this, kWallNormal);
            newWall = normal;
            float push = Script::GetFloat(0x78384871);
            Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
            float py = push * newWall.y, pz = push * newWall.z, pw = push * newWall.w;
            position.x = static_cast<float>(static_cast<double>(push) * newWall.x + position.x);
            position.y += py;
            position.z += pz;
            position.w += pw;
            Mth::Vector flat = {newWall.x, 0.0f, newWall.z, newWall.w};
            flat.Normalize();
            GameFunction<void(__thiscall*)(void*, Mth::Vector)>(0x004FE860)(this, flat);
            VectorAt(ObjectOf(this), kVelocity).RotateToPlane(flat);
        } else {
            SetTimedFlag(At<void*>(this, kTimedFlags), 0x38, 0);
        }
    }

    {
        uint8_t* other = At<uint8_t*>(this, kOtherFlags);
        if (!Flag(this, 0x30) || Flag(this, 0x78) || At<uint8_t>(this, 0x2DB) || other[0x28] || other[0x50])
            At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
        else
            CallThis(0x004FEA50, this);
    }

    // Landing: a feeler from the last position (carried along) to the new one.
    auto lastPosition = [&] { return carried + VectorAt(ObjectOf(this), kOldPosition); };
    Mth::Vector from = lastPosition();
    bool stopped = CheckAirForwardCollision(&from);
    int state = At<int>(At<void*>(this, kTimedFlags), 0x18);
    if (state == 2 || state == 5)
        return;
    VectorAt(this, kFeelerStart) = lastPosition();
    VectorAt(this, kFeelerEnd) = VectorAt(ObjectOf(this), kPosition);
    if (!stopped && CheckFeeler(0x10, 0)) {
        bool steep = VectorAt(this, kHitNormal).y < 0.1f;
        from = lastPosition();
        // (Not yet, while something above says so - fast or steep, and the ride's timer past 500.)
        bool notYet = GameFunction<bool(__thiscall*)(void*, Mth::Vector*)>(0x004FFDB0)(this, &from) &&
                      (VectorAt(ObjectOf(this), kVelocity).y > 10.0f || steep) &&
                      GameFunction<uint32_t(__thiscall*)(void*)>(0x004FC400)(At<void*>(this, kRide)) > 500;
        if (!notYet) {
            Mth::Vector normal = VectorAt(this, kHitNormal);
            VectorAt(ObjectOf(this), kPosition) = VectorAt(this, kHitPoint);
            VectorAt(ObjectOf(this), kPosition) += normal;
            if (At<uint8_t>(this, kHitSkatable)) {
                LandFromAir();
                return;
            }
            // Not something to land on: turn along it and push off it.
            VectorAt(ObjectOf(this), kVelocity).RotateToPlane(normal);
            VectorAt(ObjectOf(this), kFacing).RotateToPlane(normal);
            At<Mth::Matrix>(ObjectOf(this), kMatrix).OrthoNormalizeAbout(2);
            At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
            float push = PhysicsFloat(0x23410C14, object);
            VectorAt(ObjectOf(this), kPosition) += normal * push;
            SetTimedFlag(At<void*>(this, kTimedFlags), 0xB0, 1);
        }
    } else if (!Flag(this, 0x30)) {
        CheckSideCollisions();
    }

    if (KeepOffCeiling())
        SetTimedFlag(At<void*>(this, kTimedFlags), 0xB0, 1);
    CallThis(0x004FFB10, this);
    VertWallplant();
    if (Flag(this, 0xB0))
        return;
    // An acid drop: both triggers (the original; PartyMod's JZ -> JNZ at 0x0050DA64 makes it either).
    if (!TriggersHeld(input))
        return;
    bool recent = false;
    if (At<uint32_t>(this, 0x380) > At<uint32_t>(this, 0x4EC))
        recent = static_cast<uint32_t>(Tmr::GameClockTimeSince(At<int>(this, 0x380))) < 0xFA;
    uint8_t drop[40];
    void* o = ObjectOf(this);
    if (FindAcidDrop(recent, &VectorAt(o, kPosition), &VectorAt(o, kOldPosition), &VectorAt(o, kVelocity),
                     reinterpret_cast<AcidDrop*>(drop)))
        StartAcidDrop(reinterpret_cast<const AcidDrop*>(drop));
}

// (Part of 0x0050CA00: landing on something skatable.)
void CorePhysicsComponent::LandFromAir()
{
    GameFunction<void(__thiscall*)(void*, int)>(0x004FDED0)(this, 0);
    StartRidingHitObject();
    void* flags = At<void*>(this, kTimedFlags);
    if (At<uint8_t>(flags, 0x30) || At<uint8_t>(flags, 0x78)) {
        At<uint8_t>(this, 0x373) = 1;
        At<uint8_t>(this, 0x374) = 1;
    } else {
        At<uint8_t>(this, 0x374) = 0;
    }
    At<uint8_t>(this, 0x372) = At<uint8_t>(flags, 0x78);
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x004FC470)(this, At<uint8_t>(this, kSurfaceFlags));
    if (!At<uint8_t>(this, 0x2ED)) {
        uint32_t which = Flag(this, 0xE1);
        void* object = ObjectOf(this);
        float speed = VectorAt(object, kVelocity).Length();
        float scale = static_cast<Obj::CSkater*>(object)->GetScaledPhysics(0xCC5F87AA);
        static_cast<Obj::CSkaterSoundComponent*>(At<void*>(this, kOtherComponent))
            ->PlayLand(speed / scale, static_cast<int>(which));
    }
    void* skater = At<void*>(this, kSkaterComponent);
    if (!At<uint8_t>(skater, 0x13B0))
        static_cast<Obj::CTrickComponent*>(skater)->SetTrickObjects(false);
    SetLastGroundFeeler(&At<uint8_t>(this, kFeeler));
    TriggerOf(this)->CheckFeeler(Events(0x0064BB98), FeelerAt(this, kOtherFeeler));
    if (!At<uint8_t>(At<uint8_t*>(this, kTriggerState), 0x26)) {
        const Mth::Vector& normal = VectorAt(this, kHitNormal);
        Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
        uint8_t* input = At<uint8_t*>(this, kInput);
        if (!At<uint8_t>(this, 0x374) && !At<uint8_t>(this, 0x373) && velocity.x == 0.0f && velocity.z == 0.0f &&
            input[kButton40]) {
            // (Landing straight down with button +0x40 held: no bounce.)
            At<uint32_t>(&velocity, 4) = 0;
        } else {
            if (!Flag(this, 0x78)) {
                velocity.ProjectToPlane(normal);
            } else {
                // Landing from a wall ride: along +0x490's direction in the ground's plane, unless that turns
                // the skater upwards (0x004FF590) - then the old velocity in the plane; at least a script speed.
                Mth::Vector saved = velocity;
                velocity.SetDirection(VectorAt(this, 0x490));
                velocity.RotateToPlane(normal);
                Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
                Mth::Vector probe = v;
                GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x004FF590)(this, &probe);
                if (probe.y > 0.0f) {
                    v = saved;
                    VectorAt(ObjectOf(this), kVelocity).ProjectToPlane(normal);
                }
                float least = Script::GetFloat(0x59484878);
                if (static_cast<double>(least) * least > VectorAt(ObjectOf(this), kVelocity).LengthSqr())
                    VectorAt(ObjectOf(this), kVelocity).SetLength(Script::GetFloat(0x59484878));
            }
            GameFunction<void(__thiscall*)(void*, float)>(0x004FC370)(&VectorAt(ObjectOf(this), kVelocity), 10.0f);
        }
        SetTimedFlag(At<void*>(this, kTimedFlags), 0x30, 0);
        At<uint8_t>(this, 0x2D0) = 0;
        Mth::Vector& ground = VectorAt(this, 0x58);
        ground = normal;
        VectorAt(this, kGroundNormal) = ground;
        VectorAt(this, 0x78) = ground;
        FindNearestWallSide();
        VectorAt(ObjectOf(this), kUp) = ground;
        At<Mth::Matrix>(ObjectOf(this), kMatrix).OrthoNormalizeAbout(1);
        At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    }
    static_cast<Obj::CObject*>(ObjectOf(this))->SelfEvent(0x532B16EF, nullptr);
}

} // namespace Skater

REPLACE(0x004FCBA0, &Skater::CorePhysicsComponent::LipPushOff);
REPLACE(0x004FCE90, &Skater::CorePhysicsComponent::SetSpeed);
REPLACE(0x00509BD0, &Skater::CorePhysicsComponent::CheckForGround);
REPLACE(0x0050CA00, &Skater::CorePhysicsComponent::DoAirPhysics);
