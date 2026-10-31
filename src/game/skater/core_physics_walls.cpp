// The skater against walls and ceilings: the vert wall plant, leaning to push off a wall, keeping the head out of
// ceilings, the feeler ahead and bouncing off what it hits.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/trigger_component.h"
#include "game/script/cscript.h"
#include "game/skater/collision_events.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/skater/tagging.h"
#include "game/skater/trick_component.h"
#include "game/obj/trigger_events.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kTrickButton = 0x7323E97C;           // x (the trick button's name)
constexpr uint32_t kWallRideWindow = 0x5D241B00;        // Wall_Ride_Triangle_Window (seconds)
constexpr uint32_t kGroundSnapUp = 0xE4D79235;          // Physics_Ground_Snap_Up
constexpr uint32_t kWallplantDistance = 0xDC2C15AD;     // Physics_vert_wallplant_distance
constexpr uint32_t kWallplantVelocityLimit = 0x273C9920; // Physics_vert_wallplant_vertical_velocity_limit
constexpr uint32_t kWallplantBoost = 0x751DBCB4;        // Physics_vert_wallplant_boost
constexpr uint32_t kVertWallPlant = 0x3722E9BF;         // Vert_WallPlant (an event)
constexpr uint32_t kMinLeanPushSpeed = 0xBC33D268;      // Skate_min_wall_lean_push_speed
constexpr uint32_t kLeanPushTime = 0x435F0653;          // Skate_wall_lean_push_time
constexpr uint32_t kLeanPushLength = 0x4B8BAB12;        // Skate_wall_lean_push_length
constexpr uint32_t kLeanPushHeight = 0xBFBBD0AF;        // Skate_wall_lean_push_height
constexpr uint32_t kHeadHeight = 0x542CF0C7;            // Skater_default_head_height
constexpr uint32_t kIgnoreCeilings = 0x2757ED2C;        // Physics_Ignore_Ceilings_After_Wallplant_Duration (ms)
constexpr uint32_t kForwardHeight = 0xD4205C9B;         // Skater_First_Forward_Collision_Height
constexpr uint32_t kForwardLength = 0x20102726;         // Skater_First_Forward_Collision_Length
constexpr uint32_t kDontSlowAngle = 0x1483FD01;         // Wall_Bounce_Dont_Slow_Angle (degrees)
constexpr uint32_t kDontFlailSpeed = 0xBE0A58A0;        // Wall_Bounce_Dont_Flail_Speed
constexpr uint32_t kFlailLeft = 0xB4101D70;             // FlailLeft (an event)
constexpr uint32_t kFlailRight = 0x756A7535;            // FlailRight (an event)
constexpr uint32_t kMaxMaxSpeedStat = 0xCC5F87AA;       // Skater_Max_Max_Speed_Stat
constexpr uint32_t kVehicleLandSound = 0xD478C96D;      // vehicle_play_land_sound (a script)

constexpr uint32_t kDriving = 0x2ED;
constexpr uint32_t kSurface = 0xFA;           // (the feeler's hit: a byte of its face)
constexpr uint32_t kHitObjectChecksum = 0x100;
constexpr uint32_t kWallplanted = 0x4E8;      // (a vert wall plant done: not again until reset)
constexpr uint32_t kWallplantTime = 0x4E0;
constexpr uint32_t kWallDirection = 0x38C;    // (the wall last ridden or planted on, it seems)
constexpr uint32_t kTakeOffHeight = 0x11C;    // (in the flags block)
constexpr uint32_t kTrickRotation = 0x13A4;   // (the skater component's rotation total)

constexpr float kToMilliseconds = 1000.0f;    // 0x006453C0
constexpr float kFeelerDepth = -200.0f;       // (0xC3480000)
constexpr float kMinWallplantHeight = 60.0f;  // 0x00645EA0
constexpr float kMaxWallplantTilt = 0.1f;     // 0x00645D74
constexpr float kMilliseconds = 0.001f;       // 0x006461D8
constexpr float kUnsignedOffset = 4294967296.0f; // 0x006453B4
constexpr float kCeilingSteepest = -0.1f;     // 0x0064BD44
constexpr float kCeilingNudge = 0.001f;       // (0x3A83126F)
constexpr float kNoHeadHeight = 6.0f;         // 0x00646284
constexpr float kWallOffset = 18.0f;          // 0x0064AAAC
constexpr float kWallFlat = 0.01f;            // 0x0064544C
constexpr float kWallPushOut = 0.1f;          // 0x00645D74
constexpr float kBounceBack = 6.0f;           // 0x00646284
constexpr float kDegreesToRadians = 0.0174532924f; // 0x00645464
constexpr float kHalfPi = 1.57079637f;        // 0x00645484
constexpr float kPi = 3.14159274f;            // 0x0064BB7C

GameObjects::CGameEntity* SkaterObject(void* component) { return static_cast<GameObjects::CGameEntity*>(ObjectOf(component)); }

// 0x00434BF0 (GameObjects::CActivationZoneModule::CheckFeeler): the triggers of what the feeler hit, for `kind`.
void CheckTriggers(void* component, const int* kind)
{
    static_cast<GameObjects::CActivationZoneModule*>(At<void*>(component, kTriggerComponent))
        ->CheckFeeler(kind, &At<Engine3D::CProbeRay>(component, kFeeler));
}

// Whether a trigger's script took over (+0x1530's +0x26).
bool TriggerTookOver(void* component) { return At<uint8_t>(At<void*>(component, kStateComponent), 0x26) != 0; }

// The velocity turned by `angle` about the y axis, as the original's inline x87 code.
void TurnVelocity(Maths::Vec4& velocity, double angle)
{
    double s = X87Sin(angle), c = X87Cos(angle);
    double x = velocity.x, z = velocity.z;
    velocity.x = static_cast<float>(c * x + s * z);
    velocity.z = static_cast<float>(c * z + -(x * s));
}

} // namespace

// 0x00503710
bool CorePhysicsComponent::VertWallplant()
{
    // (not original: THAW 0x005D6960 - never a vert wallplant on the bike)
    if (At<uint8_t>(this, kDriving) || ThawBmx::LiveNow(this))
        return false;
    // The trick button pressed (or pressed within Wall_Ride_Triangle_Window).
    void* skater = At<void*>(this, kSkaterComponent);
    if (!static_cast<GameObjects::CMoveExecutor*>(skater)->IsButtonDown(kTrickButton)) {
        uint32_t window = Ftol(static_cast<double>(Scripts::FetchFloat(kWallRideWindow)) * kToMilliseconds);
        if (!static_cast<GameObjects::CMoveExecutor*>(At<void*>(this, kSkaterComponent))
                 ->FindPress(kTrickButton, window, 0xFFFFFFFFu))
            return false;
    }
    uint8_t* flags = FlagsOf(this);
    if (!flags[0x30] || !flags[0xD8])
        return false;
    if (!(At<float>(At<void*>(this, kSkaterComponent), kTrickRotation) == 0.0f) || At<uint8_t>(this, kWallplanted))
        return false;
    // The wall within reach below the skater's up axis.
    void* object = ObjectOf(this);
    float snap = PhysicsFloat(kGroundSnapUp, object);
    VectorAt(this, kFeelerStart) = VectorAt(object, kUp) * snap;
    VectorAt(this, kFeelerEnd) = VectorAt(ObjectOf(this), kUp) * kFeelerDepth;
    VectorAt(this, kFeelerStart) += VectorAt(ObjectOf(this), kPosition);
    VectorAt(this, kFeelerEnd) += VectorAt(ObjectOf(this), kPosition);
    if (!CheckFeeler(0x10, 0))
        return false;
    float distance = static_cast<float>(LengthUnrounded(VectorAt(ObjectOf(this), kPosition) - VectorAt(this, kHitPoint)));
    if (static_cast<double>(Scripts::FetchIntValue(kWallplantDistance)) < distance)
        return false;
    object = ObjectOf(this);
    if (static_cast<double>(Scripts::FetchIntValue(kWallplantVelocityLimit)) > VectorAt(object, kVelocity).y)
        return false;
    if (At<float>(FlagsOf(this), kTakeOffHeight) < kMinWallplantHeight)
        return false;
    if (!(At<uint16_t>(this, kHitFlags) & 8) && std::fabs(VectorAt(this, kHitNormal).y) > kMaxWallplantTilt)
        return false;
    if (!static_cast<GameObjects::CTaggingControlComponent*>(At<void*>(this, 0x1538))->SlapSticker(true, false, false))
        return false;
    CheckTriggers(this, g_jumpTriggerEvents);
    if (TriggerTookOver(this))
        return true;
    object = ObjectOf(this);
    if (VectorAt(object, kVelocity).y >= 0.0f)
        VectorAt(object, kVelocity).y = static_cast<float>(Scripts::FetchIntValue(kWallplantBoost) +
                                                          static_cast<double>(VectorAt(object, kVelocity).y));
    SkaterObject(this)->PostOwnEvent(kVertWallPlant, nullptr);
    static_cast<GameObjects::CMoveExecutor*>(At<void*>(this, kSkaterComponent))
        ->PassToTrickObjects(At<uint32_t>(this, kHitObjectChecksum));
    SetTimedFlag(this, 0xC0, 0);
    SetTimedFlag(this, 0xB0, 0);
    At<uint8_t>(this, kWallplanted) = 1;
    return true;
}

// 0x00503B30
void CorePhysicsComponent::LeanPushOffWall()
{
    float speed = static_cast<float>(LengthUnrounded(VectorAt(ObjectOf(this), kVelocity)));
    if (Scripts::FetchFloat(kMinLeanPushSpeed) < speed)
        return;
    // Held against a wall to the left or right: a feeler that far out, Skate_wall_lean_push_length after
    // Skate_wall_lean_push_time.
    uint8_t* pad = InputOf(this) + 0x20;
    int held;
    if (pad[0x40])
        held = TimeSincePress(pad + 0x40);
    else if (pad[0x60])
        held = TimeSincePress(pad + 0x60);
    else
        return;
    double heldTime = held;
    if (held < 0)
        heldTime += kUnsignedOffset; // (unsigned)
    float seconds = static_cast<float>(heldTime * kMilliseconds);
    float pushTime = Scripts::FetchFloat(kLeanPushTime);
    if (seconds > pushTime)
        seconds = pushTime;
    if (pad[0x60])
        seconds = -seconds;
    double length = Scripts::FetchFloat(kLeanPushLength);
    float reach = static_cast<float>(length * (static_cast<double>(seconds) / pushTime));
    double height = Scripts::FetchFloat(kLeanPushHeight);
    void* object = ObjectOf(this);
    const Maths::Vec4& up = VectorAt(object, kUp);
    const Maths::Vec4& position = VectorAt(ObjectOf(this), kPosition);
    Maths::Vec4& start = VectorAt(this, kFeelerStart);
    Maths::Vec4& end = VectorAt(this, kFeelerEnd);
    float hx = static_cast<float>(height * up.x), hy = static_cast<float>(height * up.y),
          hz = static_cast<float>(height * up.z);
    double hw = height * up.w;
    start.z = hz + position.z;
    start.w = static_cast<float>(hw + position.w);
    start.x = hx + position.x;
    start.y = hy + position.y;
    const Maths::Vec4& side = VectorAt(ObjectOf(this), kMatrix);
    float ry = static_cast<float>(static_cast<double>(reach) * side.y),
          rz = static_cast<float>(static_cast<double>(reach) * side.z),
          rw = static_cast<float>(static_cast<double>(reach) * side.w);
    end.z = rz + start.z;
    end.w = rw + start.w;
    end.x = static_cast<float>(static_cast<double>(reach) * side.x + start.x);
    end.y = ry + start.y;
    if (!CheckFeeler(0x10, 0))
        return;
    // Moved along the wall's normal to where it is (one up from the skater's position), unless that's blocked.
    const Maths::Vec4& point = VectorAt(this, kHitPoint);
    Maths::Vec4 normal = VectorAt(this, kHitNormal);
    Maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.ProjectOnto(normal);
    object = ObjectOf(this);
    const Maths::Vec4& up2 = VectorAt(object, kUp);
    const Maths::Vec4& position2 = VectorAt(object, kPosition);
    start = {up2.x + position2.x, up2.y + position2.y, up2.z + position2.z, up2.w + position2.w};
    end = {along.x + start.x, along.y + start.y, along.z + start.z, along.w + start.w};
    if (CheckFeeler(0x10, 0))
        return;
    object = ObjectOf(this);
    Maths::Vec4& p = VectorAt(object, kPosition);
    const Maths::Vec4& u = VectorAt(object, kUp);
    p = {end.x - u.x, end.y - u.y, end.z - u.z, end.w - u.w};
}

// 0x00504EE0
bool CorePhysicsComponent::KeepOffCeiling()
{
    void* object = ObjectOf(this);
    if (VectorAt(object, kUp).y < 0.0f)
        return false;
    // A feeler up the skater's height (just 6 for a while after a wall plant).
    float headHeight = PhysicsFloat(kHeadHeight, object);
    uint32_t elapsed = Since(At<uint32_t>(this, kWallplantTime));
    double reach = elapsed > static_cast<uint32_t>(Scripts::FetchIntValue(kIgnoreCeilings)) ? headHeight : kNoHeadHeight;
    object = ObjectOf(this);
    const Maths::Vec4& up = VectorAt(object, kUp);
    double x = reach * up.x, y = reach * up.y;
    float z = static_cast<float>(reach * up.z), w = static_cast<float>(reach * up.w);
    Maths::Vec4& start = VectorAt(this, kFeelerStart);
    Maths::Vec4& end = VectorAt(this, kFeelerEnd);
    start = VectorAt(object, kPosition);
    end = VectorAt(ObjectOf(this), kPosition);
    end.x = static_cast<float>(x + end.x);
    end.y = static_cast<float>(y + end.y);
    end.z = z + end.z;
    end.w = w + end.w;
    if (!CheckFeeler(0x10, 0))
        return false;
    Maths::Vec4 normal = VectorAt(this, kHitNormal);
    if (normal.y > kCeilingSteepest)
        return false;
    // A ceiling: the skater slid along it to below it, its velocity into its plane.
    const Maths::Vec4& point = VectorAt(this, kHitPoint);
    Maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.ProjectOnto(normal);
    start = VectorAt(ObjectOf(this), kPosition);
    const Maths::Vec4& position = VectorAt(ObjectOf(this), kPosition);
    end = {along.x + position.x, along.y + position.y, along.z + position.z, along.w + position.w};
    if (CheckFeeler(0x10, 0)) // (nudged along the new hit's normal)
        VectorAt(ObjectOf(this), kPosition) = VectorAt(this, kHitPoint) + kCeilingNudge * VectorAt(this, kHitNormal);
    else
        VectorAt(ObjectOf(this), kPosition) = end;
    const Maths::Vec4 entryVelocity = VectorAt(ObjectOf(this), kVelocity); // (not original: for NotePath)
    VectorAt(ObjectOf(this), kVelocity).FlattenOntoPlane(normal);
    CollisionEvents::NotePath(this, CollisionEvents::RouteWay::Ceiling, entryVelocity); // (not original)
    return true;
}

// 0x005051B0
bool CorePhysicsComponent::KeepOffCeilingAtWall()
{
    // A feeler up the skater's height from 18 out along +0x38C.
    void* object = ObjectOf(this);
    const Maths::Vec4& wall = VectorAt(this, kWallDirection);
    double x = static_cast<double>(wall.x) * kWallOffset;
    float y = wall.y * kWallOffset, z = wall.z * kWallOffset, w = wall.w * kWallOffset;
    const Maths::Vec4& position = VectorAt(object, kPosition);
    Maths::Vec4& start = VectorAt(this, kFeelerStart);
    Maths::Vec4& end = VectorAt(this, kFeelerEnd);
    start.z = z + position.z;
    start.w = w + position.w;
    start.x = static_cast<float>(x + position.x);
    start.y = y + position.y;
    end = start;
    end.y = static_cast<float>(static_cast<double>(PhysicsFloat(kHeadHeight, object)) + end.y);
    if (!CheckFeeler(0x10, 0))
        return false;
    Maths::Vec4 normal = VectorAt(this, kHitNormal);
    if (normal.y > kCeilingSteepest)
        return false;
    const Maths::Vec4& point = VectorAt(this, kHitPoint);
    Maths::Vec4 along = {point.x - end.x, point.y - end.y, point.z - end.z, point.w - end.w};
    along.ProjectOnto(normal);
    start = VectorAt(ObjectOf(this), kPosition);
    const Maths::Vec4& p = VectorAt(ObjectOf(this), kPosition);
    end = {along.x + p.x, along.y + p.y, along.z + p.z, along.w + p.w};
    Maths::Vec4& position2 = VectorAt(ObjectOf(this), kPosition);
    if (CheckFeeler(0x10, 0)) {
        const Maths::Vec4& hit = VectorAt(this, kHitPoint);
        const Maths::Vec4& n = VectorAt(this, kHitNormal);
        position2 = {n.x + hit.x, n.y + hit.y, n.z + hit.z, n.w + hit.w};
    } else {
        position2 = end;
    }
    const Maths::Vec4 entryVelocity = VectorAt(ObjectOf(this), kVelocity); // (not original: for NotePath)
    VectorAt(ObjectOf(this), kVelocity).FlattenOntoPlane(normal);
    CollisionEvents::NotePath(this, CollisionEvents::RouteWay::Ceiling, entryVelocity); // (not original)
    return true;
}

// 0x00506E50
void CorePhysicsComponent::CheckWallAhead()
{
    void* object = ObjectOf(this);
    const Maths::Vec4& position = VectorAt(object, kPosition);
    const Maths::Vec4& old = VectorAt(object, kOldPosition);
    if (position.x == old.x && position.y == old.y && position.z == old.z && position.w == old.w)
        return;
    // A feeler Skater_First_Forward_Collision_Length along the move, from Skater_First_Forward_Collision_Height up
    // the skater at its last position to that height at its new one.
    Maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, position.w - old.w};
    move.MakeUnit();
    object = ObjectOf(this);
    double height = PhysicsFloat(kForwardHeight, object);
    const Maths::Vec4& up = VectorAt(object, kUp);
    Maths::Vec4 lift = {static_cast<float>(height * up.x), static_cast<float>(height * up.y),
                        static_cast<float>(height * up.z), static_cast<float>(height * up.w)};
    const Maths::Vec4& last = VectorAt(ObjectOf(this), kOldPosition);
    Maths::Vec4& start = VectorAt(this, kFeelerStart);
    start.z = lift.z + last.z;
    start.w = lift.w + last.w;
    start.x = lift.x + last.x;
    start.y = lift.y + last.y;
    double length = PhysicsFloat(kForwardLength, object);
    const Maths::Vec4& now = VectorAt(ObjectOf(this), kPosition);
    float ax = static_cast<float>(move.x * length), ay = static_cast<float>(move.y * length),
          az = static_cast<float>(move.z * length);
    double aw = length * move.w;
    double x = static_cast<double>(lift.x) + now.x; // (left unrounded)
    float y = lift.y + now.y, z = lift.z + now.z, w = lift.w + now.w;
    Maths::Vec4& end = VectorAt(this, kFeelerEnd);
    end.z = z + az;
    end.w = static_cast<float>(w + aw);
    end.x = static_cast<float>(x + ax);
    end.y = y + ay;
    if (!CheckFeeler(0x10, 0))
        return;
    Maths::Vec4 normal = VectorAt(this, kHitNormal);
    if (At<uint8_t>(this, kHitSkatable)) {
        // Skatable and not flat against the ground: pushed out of it a little.
        const Maths::Vec4& ground = VectorAt(this, kGroundNormal);
        double into = static_cast<double>(normal.z) * ground.z + static_cast<double>(normal.y) * ground.y +
                      static_cast<double>(normal.x) * ground.x;
        if (!(std::fabs(into) < kWallFlat)) {
            Maths::Vec4 out = {normal.x * kWallPushOut, normal.y * kWallPushOut, normal.z * kWallPushOut,
                               normal.w * kWallPushOut};
            VectorAt(ObjectOf(this), kPosition) = VectorAt(this, kHitPoint);
            VectorAt(ObjectOf(this), kPosition) += out;
            CollisionEvents::NotePath(this, CollisionEvents::RouteWay::WallAheadClimb,
                                      VectorAt(ObjectOf(this), kVelocity)); // (not original)
            return;
        }
    }
    CheckTriggers(this, g_bonkEvents);
    if (!TriggerTookOver(this))
        BounceOffWallAhead(&normal);
}

// 0x005059D0
void CorePhysicsComponent::BounceOffWallAhead(const Maths::Vec4* normal)
{
    const Maths::Vec4 entryVelocity = VectorAt(ObjectOf(this), kVelocity); // (not original: for NotePath)
    if (WallPushOff()) {
        CollisionEvents::NotePath(this, CollisionEvents::RouteWay::WallAheadPlant, entryVelocity); // (not original)
        return;
    }
    void* object = ObjectOf(this);
    const Maths::Vec4& position = VectorAt(object, kPosition);
    const Maths::Vec4& old = VectorAt(object, kOldPosition);
    // The move (rounded), and its direction.
    Maths::Vec4 move = {position.x - old.x, position.y - old.y, position.z - old.z, 0.0f};
    Maths::Vec4 direction = {move.x, move.y, move.z, position.w - old.w};
    direction.MakeUnit();
    object = ObjectOf(this);
    double height = PhysicsFloat(kForwardHeight, object);
    const Maths::Vec4& up = VectorAt(object, kUp);
    Maths::Vec4 lift = {static_cast<float>(height * up.x), static_cast<float>(height * up.y),
                        static_cast<float>(height * up.z), static_cast<float>(height * up.w)};
    float turn;
    float angle = BounceOffWall(normal, &turn, 1.0f);

    // Slowed the more head-on the wall is (unless within Wall_Bounce_Dont_Slow_Angle); fast enough, a flail.
    double dontSlow = static_cast<double>(Scripts::FetchFloat(kDontSlowAngle)) * kDegreesToRadians;
    double off = std::fabs(angle);
    if (off > dontSlow) {
        Maths::Vec4& velocity = VectorAt(ObjectOf(this), kVelocity);
        float speed = static_cast<float>(LengthUnrounded(velocity));
        double keep = 1.0f - (off - dontSlow) / (kHalfPi - dontSlow);
        velocity.x = static_cast<float>(keep * velocity.x);
        velocity.y = static_cast<float>(keep * velocity.y);
        velocity.z = static_cast<float>(keep * velocity.z);
        velocity.w = static_cast<float>(keep * velocity.w);
        if (Scripts::FetchFloat(kDontFlailSpeed) < speed) {
            uint32_t leftTurn = angle < 0.0f ? 1 : 0;
            bool same = (FlagsOf(this)[0x28] ^ leftTurn) == 0; // (flag +0x28 against the turn's side)
            SkaterObject(this)->PostOwnEvent(same ? kFlailRight : kFlailLeft, nullptr);
            if (!At<uint8_t>(this, kDriving)) {
                // (The original passes the surface byte in a word whose upper bytes are the angle's.)
                uint32_t angleBits;
                std::memcpy(&angleBits, &angle, 4);
                uint32_t surface = (angleBits & 0xFFFFFF00u) | At<uint8_t>(this, kSurface);
                float impact = static_cast<float>(speed / StatValue(ObjectOf(this), kMaxMaxSpeedStat));
                static_cast<GameObjects::CRiderSfxPlayer*>(At<void*>(this, kOtherComponent))
                    ->PlayBonk(impact, static_cast<int>(surface));
            } else {
                Scripts::LaunchScript(kVehicleLandSound, nullptr, SkaterObject(this), false, nullptr);
            }
        }
    }

    // Back out of the wall: the hit point less the lift, 6 out along the normal.
    object = ObjectOf(this);
    Maths::Vec4& p = VectorAt(object, kPosition);
    const Maths::Vec4& hit = VectorAt(this, kHitPoint);
    p.z = hit.z - lift.z;
    p.w = hit.w - lift.w;
    p.x = hit.x - lift.x;
    p.y = hit.y - lift.y;
    double nx = static_cast<double>(normal->x) * kBounceBack, ny = static_cast<double>(normal->y) * kBounceBack;
    float nz = normal->z * kBounceBack, nw = normal->w * kBounceBack;
    p.x = static_cast<float>(nx + p.x);
    p.y = static_cast<float>(ny + p.y);
    p.z = nz + p.z;
    p.w = nw + p.w;
    CollisionEvents::NotePath(this, CollisionEvents::RouteWay::WallAhead, entryVelocity); // (not original)

    // Then a feeler from there (twice the lift up) along the velocity, as far as the skater moved plus
    // Skater_First_Forward_Collision_Length.
    object = ObjectOf(this);
    float moved = static_cast<float>(std::sqrt(static_cast<double>(move.x) * move.x +
                                               static_cast<double>(move.z) * move.z +
                                               static_cast<double>(move.y) * move.y));
    Maths::Vec4 heading = VectorAt(object, kVelocity);
    heading.MakeUnit();
    Maths::Vec4 travel = {heading.x * moved, heading.y * moved, heading.z * moved, heading.w * moved};
    Maths::Vec4& start = VectorAt(this, kFeelerStart);
    const Maths::Vec4& here = VectorAt(object, kPosition);
    start.z = lift.z + here.z;
    start.w = lift.w + here.w;
    start.x = lift.x + here.x;
    start.y = lift.y + here.y;
    start.x = lift.x + start.x;
    start.y = lift.y + start.y;
    start.z = lift.z + start.z;
    start.w = lift.w + start.w;
    double length = PhysicsFloat(kForwardLength, object);
    const Maths::Vec4& now = VectorAt(ObjectOf(this), kPosition);
    float ax = static_cast<float>(heading.x * length), ay = static_cast<float>(heading.y * length),
          az = static_cast<float>(heading.z * length);
    double aw = length * heading.w;
    double bx = static_cast<double>(lift.x) + now.x, by = static_cast<double>(lift.y) + now.y;
    float bz = lift.z + now.z, bw = lift.w + now.w;
    Maths::Vec4 base = {static_cast<float>(bx + travel.x), static_cast<float>(by + travel.y), bz + travel.z,
                        bw + travel.w};
    Maths::Vec4& end = VectorAt(this, kFeelerEnd);
    end.x = static_cast<float>(static_cast<double>(base.x) + ax);
    end.y = static_cast<float>(static_cast<double>(base.y) + ay);
    end.z = base.z + az;
    end.w = static_cast<float>(base.w + aw);
    if (!CheckFeeler(0x10, 0) || !At<uint8_t>(ObjectOf(this), 0xBC))
        return;
    // Into a corner: turned back the other way, half as fast; fast enough, a flail.
    double back = static_cast<double>(kPi) - turn;
    float backAngle = static_cast<float>(back);
    TurnVelocity(VectorAt(ObjectOf(this), kVelocity), back);
    At<Maths::Mat4>(ObjectOf(this), kMatrix).YawLocally(backAngle);
    At<Maths::Mat4>(this, kDisplayMatrix) = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    VectorAt(ObjectOf(this), kVelocity) *= 0.5f;
    CollisionEvents::NotePath(this, CollisionEvents::RouteWay::WallAheadCorner, entryVelocity); // (not original)
    if (Scripts::FetchFloat(kDontFlailSpeed) < moved)
        SkaterObject(this)->PostOwnEvent(kFlailLeft, nullptr);
}

} // namespace Skater
