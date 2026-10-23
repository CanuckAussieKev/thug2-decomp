// The skater's wall plants and the start of a wall ride.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/trigger_component.h"
#include "game/obj/walk_hang.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/tagging.h"
#include "game/skater/trick_component.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kCanWallplant = 0x5310E7B5;         // canwallplant (a tag: a vehicle that may)
constexpr uint32_t kDisallowRewallplant = 0x82135DD7;  // Physics_Disallow_Rewallplant_Duration (ms)
constexpr uint32_t kWallplantDelay = 0x545840C4;       // Physics_Wallplant_Delay (seconds)
constexpr uint32_t kMinWallplantHeight = 0xD5349CC6;   // Physics_Min_Wallplant_Height
constexpr uint32_t kWallplantTrick = 0x5D1F84A7;       // Wallplant_Trick (the trick names that plant)
constexpr uint32_t kLateWallplantFrames = 0x3B389187;  // Physics_late_wallplant_frames
constexpr uint32_t kForwardHeight = 0xD4205C9B;        // Skater_First_Forward_Collision_Height
constexpr uint32_t kForwardLength = 0x20102726;        // Skater_First_Forward_Collision_Length
constexpr uint32_t kMinApproachAngle = 0x8F79CC1C;     // Physics_Wallplant_Min_Approach_Angle (degrees)
constexpr uint32_t kSpeedLoss = 0x09B2D4A3;            // Physics_Wallplant_Speed_Loss
constexpr uint32_t kMinExitSpeed = 0x7CEE396C;         // Physics_Wallplant_Min_Exit_Speed
constexpr uint32_t kWalkingWallplantSpeed = 0x5F768D84; // Physics_Walking_Wallplant_Speed
constexpr uint32_t kVerticalExitSpeed = 0x074957FA;    // Physics_Wallplant_Vertical_Exit_Speed
constexpr uint32_t kDistanceFromWall = 0x024BE8F0;     // Physics_Wallplant_Distance_From_Wall
constexpr uint32_t kWallplant = 0xCF74F6B7;            // Wallplant (an event)
constexpr uint32_t kTriangle = 0x20689278;             // triangle (a button)
constexpr uint32_t kWallRideWindow = 0x5D241B00;       // Wall_Ride_Triangle_Window (seconds)
constexpr uint32_t kWallRideDelay = 0xF0C74EC2;        // Wall_Ride_Delay (seconds)
constexpr uint32_t kWallRideMinSpeed = 0xF0636A67;     // Wall_Ride_Min_Speed
constexpr uint32_t kUpsideDownAngle = 0xAE23E850;      // Wall_Ride_Upside_Down_Angle (degrees)
constexpr uint32_t kMaxIncidentAngle = 0x6AC9F64D;     // Wall_Ride_Max_Incident_Angle (degrees)
constexpr uint32_t kMaxTilt = 0xBFCBB7E8;              // Wall_Ride_Max_Tilt (degrees)
constexpr uint32_t kWallRideRight = 0x51372712;        // WallRideRight (an event)
constexpr uint32_t kWallRideLeft = 0x5DE19C83;         // WallRideLeft (an event)

constexpr uint32_t kDriving = 0x2ED;
constexpr uint32_t kModelRotating = 0x2DB;
constexpr uint32_t kHitObjectChecksum = 0x100;
constexpr uint32_t kWallplantTime = 0x4E0;
constexpr uint32_t kWallRideTime = 0x39C;
constexpr uint32_t kWallplantEndTime = 0x3A0;
constexpr uint32_t kWallDirection = 0x38C;   // the wall ridden (its normal)
constexpr uint32_t kLateFrames = 0x14CC;     // frames left for a late wall plant
constexpr uint32_t kSavedVelocity = 0x14D0;  // (when the plant was missed)
constexpr uint32_t kSavedPosition = 0x14E0;
constexpr uint32_t kSavedWallNormal = 0x14F0;
constexpr uint32_t kTakeOffHeight = 0x11C;   // (in the flags block)

constexpr float kMinUp = 0.1f;               // 0x00645D74
constexpr float kToMilliseconds = 1000.0f;   // 0x006453C0
constexpr float kUnsignedOffset = 4294967296.0f; // 0x006453B4
constexpr float kMaxTilt01 = 0.1f;           // 0x00645D74
constexpr float kBackOff = 24.0f;            // 0x00645EE8
constexpr float kMinSpeed = 0.01f;           // 0x0064544C
constexpr float kDegreesToRadians = 0.0174532924f; // 0x00645464
constexpr float kRiseScale = 0.15f;          // 0x0064BD4C
constexpr float kProbe = 6.0f;               // (0x40C00000)
constexpr float kWalkReach = 36.0f;          // (0x42100000)
constexpr float kMinFlatSpeed = 0.0001f;     // 0x006453D4
constexpr float kFromWall = 0.1f;            // (0x3DCCCCCD)
constexpr uint32_t kLateWallplantText = 0x0064BD58; // "late wallplant\n"

Obj::CCompositeObject* SkaterObject(void* component) { return static_cast<Obj::CCompositeObject*>(ObjectOf(component)); }

void CheckTriggers(void* component, uint32_t kind, uint32_t feeler)
{
    static_cast<Obj::CTriggerComponent*>(At<void*>(component, kTriggerComponent))
        ->CheckFeeler(reinterpret_cast<const int*>(kind), &At<Nx::CFeeler>(component, feeler));
}

bool TriggerTookOver(void* component) { return At<uint8_t>(At<void*>(component, kStateComponent), 0x26) != 0; }

// The trick button `button` pressed now, or within Wall_Ride_Triangle_Window (0x0051ECF0 / 0x0051F6B0 on the
// skater component).
bool ButtonPressed(void* component, uint32_t button)
{
    auto* skater = static_cast<Obj::CTrickComponent*>(At<void*>(component, kSkaterComponent));
    if (skater->IsButtonDown(button))
        return true;
    uint32_t window = Ftol(static_cast<double>(Script::GetFloat(kWallRideWindow)) * kToMilliseconds);
    return static_cast<Obj::CTrickComponent*>(At<void*>(component, kSkaterComponent))->FindPress(button, window, 0xFFFFFFFFu);
}

// Moving up the wall: whether there's wall 6 either side of the hit point raised by 0.15 of the upward speed (along
// the wall).
bool WallAboveHit(void* component)
{
    Mth::Vector point = VectorAt(component, kHitPoint);
    Mth::Vector normal = VectorAt(component, kHitNormal);
    Mth::Vector rise = {0.0f, VectorAt(ObjectOf(component), kVelocity).y * kRiseScale, 0.0f, 1.0f};
    rise.RotateToPlane(normal);
    point += rise;
    Mth::Vector end = point - normal * kProbe;
    Mth::Vector start = point + normal * kProbe;
    Nx::CFeeler feeler;
    feeler.Construct(&start, &end);
    return feeler.GetCollision(true, false) != 0;
}

} // namespace

// 0x00507500
bool CorePhysicsComponent::WallPlant(bool late, bool checkOnly, bool walking)
{
    if (At<uint8_t>(this, kDriving)) {
        int allowed = 0;
        if (!SkaterObject(this)->GetIntegerTag(kCanWallplant, &allowed))
            return false;
    }
    if (At<uint8_t>(this, kModelRotating) || At<uint8_t>(At<void*>(this, kStateComponent), 0x24))
        return false;

    if (!walking) {
        void* object = ObjectOf(this);
        if (VectorAt(object, kUp).y < kMinUp)
            return false;
        int elapsed = static_cast<int>(Since(At<uint32_t>(this, kWallplantTime)));
        double sincePlant = elapsed;
        if (elapsed < 0)
            sincePlant += kUnsignedOffset; // (unsigned)
        float sincePlantFloat = static_cast<float>(sincePlant);
        if (Script::GetFloat(kDisallowRewallplant) > sincePlantFloat)
            return false;
        float sinceRide =
            static_cast<float>(Tmr::GameClockTimeSince(At<int>(this, kWallRideTime)));
        if (static_cast<double>(Script::GetFloat(kWallplantDelay)) * kToMilliseconds > sinceRide)
            return false;
        if (Script::GetFloat(kMinWallplantHeight) > At<float>(FlagsOf(this), kTakeOffHeight))
            return false;
        // Not a steep enough wall: only for a late plant (unless just checking, or it's vert).
        if (!checkOnly && !(At<uint16_t>(this, kHitFlags) & 8) && std::fabs(VectorAt(this, kHitNormal).y) > kMaxTilt01 &&
            !late)
            return false;
        // A wall plant trick pressed?
        Script::CArray* tricks = Script::GetArray(kWallplantTrick);
        bool pressed = false;
        for (uint32_t i = 0; i < tricks->m_size; ++i) {
            uint32_t trick = tricks->GetElement(i);
            if (static_cast<Obj::CTrickComponent*>(At<void*>(this, kSkaterComponent))
                    ->CheckTrigger(reinterpret_cast<Script::CStruct*>(trick), 0, 0, true)) {
                pressed = true;
                break;
            }
        }
        if (!pressed) {
            if (late)
                return false;
            // Not yet: a few frames to press it late in (kept: the velocity and position; the skater backed off the
            // wall 24 the way it came).
            At<int>(this, kLateFrames) = Script::GetInteger(kLateWallplantFrames);
            VectorAt(this, kSavedVelocity) = VectorAt(ObjectOf(this), kVelocity);
            VectorAt(this, kSavedPosition) = VectorAt(ObjectOf(this), kPosition);
            void* o = ObjectOf(this);
            const Mth::Vector& v = VectorAt(o, kVelocity);
            Mth::Vector back = {-v.x, 0.0f, -v.z, v.w};
            back.Normalize();
            Mth::Vector& p = VectorAt(o, kPosition);
            float y = back.y * kBackOff, z = back.z * kBackOff, w = back.w * kBackOff;
            p.z = z + p.z;
            p.w = w + p.w;
            p.x = static_cast<float>(static_cast<double>(back.x) * kBackOff + p.x);
            p.y = y + p.y;
            return false;
        }
        if (late) {
            // A late plant: back to where the skater was, and the wall looked for again along its move.
            Dbg::Printf(reinterpret_cast<const char*>(kLateWallplantText));
            VectorAt(ObjectOf(this), kVelocity) = VectorAt(this, kSavedVelocity);
            VectorAt(ObjectOf(this), kPosition) = VectorAt(this, kSavedPosition);
            void* o = ObjectOf(this);
            const Mth::Vector& position = VectorAt(o, kPosition);
            const Mth::Vector& old = VectorAt(o, kOldPosition);
            Mth::Vector move = {position.x - old.x, position.y - old.y, position.z - old.z, position.w - old.w};
            move.Normalize();
            double height = PhysicsFloat(kForwardHeight, o);
            const Mth::Vector& up = VectorAt(o, kUp);
            float hw = static_cast<float>(height * up.w), hz = static_cast<float>(height * up.z),
                  hy = static_cast<float>(height * up.y), hx = static_cast<float>(height * up.x);
            const Mth::Vector& last = VectorAt(ObjectOf(this), kOldPosition);
            Mth::Vector& start = VectorAt(this, kFeelerStart);
            start = {hx + last.x, hy + last.y, hz + last.z, hw + last.w};
            double length = PhysicsFloat(kForwardLength, ObjectOf(this));
            float lw = static_cast<float>(move.w * length), lz = static_cast<float>(move.z * length),
                  ly = static_cast<float>(move.y * length), lx = static_cast<float>(length * move.x);
            const Mth::Vector& now = VectorAt(ObjectOf(this), kPosition);
            double w = static_cast<double>(hw) + now.w; // (left unrounded)
            float z = hz + now.z, y = hy + now.y, x = hx + now.x;
            Mth::Vector& end = VectorAt(this, kFeelerEnd);
            end = {x + lx, y + ly, z + lz, static_cast<float>(w + lw)};
            if (!CheckFeeler(0x10, 0))
                return false;
        }
    } else {
        // Walking: a feeler 36 the other way from the facing, at the forward collision height.
        void* object = ObjectOf(this);
        const Mth::Vector& facing = VectorAt(object, kForward);
        Mth::Vector direction = {-facing.x, facing.y, -facing.z, facing.w};
        direction.Normalize();
        float height = PhysicsFloat(kForwardHeight, object);
        Mth::Vector lift = VectorAt(object, kUp) * height;
        VectorAt(this, kFeelerStart) = VectorAt(ObjectOf(this), kPosition) + lift;
        Mth::Vector reach = direction * kWalkReach;
        Mth::Vector base = VectorAt(ObjectOf(this), kPosition) + lift;
        VectorAt(this, kFeelerEnd) = base + reach;
        if (!CheckFeeler(0x10, 0))
            return false;
    }

    if (checkOnly)
        return true;
    // The plant: off the wall (its normal flattened; a late plant's saved one) at the approach angle allowed.
    float speed = static_cast<float>(LengthUnrounded(VectorAt(ObjectOf(this), kVelocity)));
    if (speed < kMinSpeed)
        return false;
    Mth::Vector heading = VectorAt(ObjectOf(this), kVelocity) / speed;
    heading.y = 0.0f;
    heading.Normalize();
    Mth::Vector wall = VectorAt(this, kHitNormal);
    if (late) {
        wall.x = VectorAt(this, kSavedWallNormal).x;
        wall.z = VectorAt(this, kSavedWallNormal).z;
        wall.w = VectorAt(this, kSavedWallNormal).w;
    }
    wall.y = 0.0f;
    wall.Normalize();
    if (!walking) {
        double limit = X87Sin(static_cast<double>(Script::GetFloat(kMinApproachAngle)) * kDegreesToRadians);
        double approach = static_cast<double>(heading.x) * wall.x + static_cast<double>(heading.z) * wall.z +
                          static_cast<double>(wall.y) * heading.y;
        if (-limit < approach)
            return false;
    }
    if (VectorAt(ObjectOf(this), kVelocity).y > 0.0f && InputOf(this)[0x1A0] && !WallAboveHit(this))
        return false;
    static_cast<Obj::CTaggingControlComponent*>(At<void*>(this, 0x1538))->SlapSticker(false, walking, late);
    VectorAt(ObjectOf(this), kVelocity).y = 0.0f;
    CheckTriggers(this, 0x0064BBEC, kFeeler);
    if (TriggerTookOver(this))
        return true;

    // Bounced off the wall (the speed less Physics_Wallplant_Speed_Loss, at least the minimum exit speed), up at
    // the vertical exit speed.
    void* object = ObjectOf(this);
    Mth::Vector& velocity = VectorAt(object, kVelocity);
    float into = static_cast<float>(static_cast<double>(wall.z) * velocity.z + static_cast<double>(wall.y) * velocity.y +
                                    static_cast<double>(wall.x) * velocity.x);
    Mth::Vector bounce = into * wall;
    if (!walking) {
        velocity -= 2.0f * bounce;
        Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
        float flat = static_cast<float>(std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.z) * v.z));
        if (flat > kMinFlatSpeed) {
            v.y = 0.0f;
            float reduced = static_cast<float>(flat - static_cast<double>(Script::GetFloat(kSpeedLoss)));
            float least = Script::GetFloat(kMinExitSpeed);
            SetLength(VectorAt(ObjectOf(this), kVelocity), Obj::LargerOf(least, reduced));
        } else {
            VectorAt(ObjectOf(this), kVelocity) = -Script::GetFloat(kMinExitSpeed) * heading;
        }
    } else {
        VectorAt(ObjectOf(this), kVelocity) = wall * Script::GetFloat(kWalkingWallplantSpeed);
    }
    VectorAt(ObjectOf(this), kVelocity).y = Script::GetFloat(kVerticalExitSpeed);

    // Riding what was hit (if it's an object still there).
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(&At<uint8_t>(this, kFeeler));
    if (feeler->HitObjectAlive()) {
        bool riding = At<Obj::CMovableContactComponent*>(this, kRide)->IsRiding();
        if (!riding || *At<void**>(At<void*>(this, kRide), 0x18) != feeler->GetHitObject()) {
            At<Obj::CMovableContactComponent*>(this, kRide)->StopRiding();
            At<Walk::MovableContact*>(this, kRide)->StartRiding(feeler->GetHitObject());
        }
    }
    if (!walking) {
        // Placed Physics_Wallplant_Distance_From_Wall off it (or 0.1 off anything nearer).
        Mth::Vector& point = VectorAt(this, kHitPoint);
        Mth::Vector& normal = VectorAt(this, kHitNormal);
        feeler->m_line.start = point;
        feeler->m_line.end = point + Script::GetFloat(kDistanceFromWall) * normal;
        if (feeler->GetCollision(true, false))
            VectorAt(ObjectOf(this), kPosition) = point + kFromWall * normal;
        else
            VectorAt(ObjectOf(this), kPosition) = feeler->m_line.end;
    }
    // Upright, facing along the new velocity.
    object = ObjectOf(this);
    Mth::Vector& forward = VectorAt(object, kForward);
    forward = VectorAt(object, kVelocity);
    VectorAt(ObjectOf(this), kForward).y = 0.0f;
    VectorAt(ObjectOf(this), kForward).Normalize();
    VectorAt(ObjectOf(this), kUp) = {0.0f, 1.0f, 0.0f, 1.0f};
    object = ObjectOf(this);
    VectorAt(object, kMatrix) = Mth::CrossProduct(VectorAt(object, kUp), VectorAt(object, kForward));
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    At<int>(this, kWallplantTime) = Now();
    SkaterObject(this)->SelfEvent(kWallplant, nullptr);
    static_cast<Obj::CTrickComponent*>(At<void*>(this, kSkaterComponent))
        ->PassToTrickObjects(At<uint32_t>(this, kHitObjectChecksum));
    SetTimedFlag(this, 0xC0, 0);
    SetTimedFlag(this, 0xB0, 0);
    SetTimedFlag(this, 0xA0, 1);
    Physics::SetState(this, 5);
    At<int>(this, kLateFrames) = 0;
    At<int>(this, kWallplantEndTime) = Now();
    return true;
}

// 0x00508140
bool CorePhysicsComponent::StartWallRide()
{
    if (At<uint8_t>(this, kDriving) || !At<uint8_t>(this, kHitWall))
        return false;
    if (!ButtonPressed(this, kTriangle))
        return false;
    // Not again within Wall_Ride_Delay of the last ride or plant.
    float sinceRide = static_cast<float>(static_cast<int>(Since(At<uint32_t>(this, kWallRideTime))));
    if (!(static_cast<double>(Script::GetFloat(kWallRideDelay)) * kToMilliseconds < sinceRide))
        return false;
    float sincePlant = static_cast<float>(static_cast<int>(Since(At<uint32_t>(this, kWallplantEndTime))));
    if (!(static_cast<double>(Script::GetFloat(kWallRideDelay)) * kToMilliseconds < sincePlant))
        return false;
    if (VectorAt(ObjectOf(this), kVelocity).y > 0.0f && !WallAboveHit(this))
        return false;
    // Fast enough along the wall, not upside down, not head-on, not tilted too far.
    Mth::Vector& wall = VectorAt(this, kWallDirection);
    wall = VectorAt(this, kHitNormal);
    Mth::Vector flatWall = {wall.x, 0.0f, wall.z, wall.w};
    flatWall.Normalize();
    void* object = ObjectOf(this);
    const Mth::Vector& v = VectorAt(object, kVelocity);
    Mth::Vector heading = {v.x, 0.0f, v.z, v.w};
    heading.Normalize();
    Mth::Vector along = {v.x, 0.0f, v.z, v.w};
    along.ProjectToPlane(flatWall);
    float speed = static_cast<float>(LengthUnrounded(along));
    if (Script::GetFloat(kWallRideMinSpeed) > speed)
        return false;
    float facing = static_cast<float>(static_cast<double>(heading.x) * flatWall.x +
                                      static_cast<double>(heading.z) * flatWall.z +
                                      static_cast<double>(heading.y) * flatWall.y);
    if (!(-X87Sin(static_cast<double>(Script::GetFloat(kUpsideDownAngle)) * kDegreesToRadians) < wall.y))
        return false;
    float headOn = std::fabs(facing);
    if (!(X87Sin(static_cast<double>(Script::GetFloat(kMaxIncidentAngle)) * kDegreesToRadians) > headOn))
        return false;
    object = ObjectOf(this);
    if (!(X87Cos(static_cast<double>(Script::GetFloat(kMaxTilt)) * kDegreesToRadians) < VectorAt(object, kUp).y))
        return false;
    void* skater = At<void*>(this, kSkaterComponent);
    if (!At<uint8_t>(skater, 0x13B0))
        static_cast<Obj::CTrickComponent*>(skater)->SetTrickObjects(false);
    SetLastGroundFeeler(&At<uint8_t>(this, kFeeler));
    CheckTriggers(this, 0x0064BB98, kLastGroundFeeler);
    if (TriggerTookOver(this))
        return false;
    // Nothing in the way along the move (turned along the wall, three times as far).
    Mth::Vector hit = VectorAt(this, kHitPoint);
    object = ObjectOf(this);
    Mth::Vector move = VectorAt(object, kPosition) - VectorAt(object, kOldPosition);
    move.RotateToPlane(wall);
    VectorAt(this, kFeelerStart) = VectorAt(ObjectOf(this), kPosition);
    VectorAt(this, kFeelerEnd) = VectorAt(ObjectOf(this), kPosition) + move * 3.0f;
    if (CheckFeeler(0x10, 0))
        return false;
    // On the wall: at the hit, one off it; moving and facing along it, up its normal.
    At<int>(this, kWallRideTime) = Now();
    VectorAt(ObjectOf(this), kPosition) = hit;
    VectorAt(ObjectOf(this), kPosition) += wall;
    VectorAt(ObjectOf(this), kVelocity).RotateToPlane(wall);
    VectorAt(ObjectOf(this), kForward) = VectorAt(ObjectOf(this), kVelocity);
    VectorAt(ObjectOf(this), kForward).Normalize();
    VectorAt(ObjectOf(this), kUp) = wall;
    object = ObjectOf(this);
    VectorAt(object, kMatrix) = Mth::CrossProduct(VectorAt(object, kUp), VectorAt(object, kForward));
    VectorAt(ObjectOf(this), kMatrix).Normalize();
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    const Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    double side = static_cast<double>(wall.x) * velocity.z + -static_cast<double>(wall.z) * velocity.x +
                  static_cast<double>(velocity.y) * 0.0f;
    SkaterObject(this)->SelfEvent(side < 0.0 ? kWallRideLeft : kWallRideRight, nullptr);
    Physics::SetState(this, 2);
    StartRidingHitObject();
    At<int>(this, kLateFrames) = 0;
    return true;
}

} // namespace Skater

REPLACE(0x00507500, &Skater::CorePhysicsComponent::WallPlant);
REPLACE(0x00508140, &Skater::CorePhysicsComponent::StartWallRide);
