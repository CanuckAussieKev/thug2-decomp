// The skater's core physics component: the skater against walls ahead (in the air) and to the sides.

#include <cmath>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/collision_events.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/obj/trigger_events.h"

namespace Skater {

using namespace CoreFields;

namespace {

constexpr uint32_t kForwardHeight = 0xD4205C9B;    // Skater_First_Forward_Collision_Height
constexpr uint32_t kForwardLength = 0x20102726;    // Skater_First_Forward_Collision_Length
constexpr uint32_t kAirSnapUp = 0x786B3272;        // Physics_Air_Snap_Up
constexpr uint32_t kMinDistanceToWall = 0x23410C14; // Skater_Min_Distance_To_Wall
constexpr uint32_t kMaxMaxSpeed = 0xCC5F87AA;      // Skater_Max_Max_Speed_Stat
constexpr uint32_t kVehicleLandSound = 0xD478C96D; // vehicle_play_land_sound (a script)
constexpr uint32_t kSideHeight = 0xB45B39E2;       // Skater_side_collide_height
constexpr uint32_t kSideLength = 0x406B425F;       // Skater_side_collide_length
constexpr uint32_t kAirExtraSide = 0x1C58C8B4;     // Skater_air_extra_side_col

// A stack feeler: raw storage (see CopyFeeler).
struct alignas(16) FeelerStorage {
    uint8_t bytes[sizeof(Nx::CFeeler)];
    Nx::CFeeler* operator->() { return reinterpret_cast<Nx::CFeeler*>(bytes); }
    Nx::CFeeler* get() { return reinterpret_cast<Nx::CFeeler*>(bytes); }
};

// The smart pointer of a stack feeler let go inline (the original's code on some paths instead of the destructor).
void ReleaseFeelerObject(FeelerStorage& feeler)
{
    auto& object = reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>&>(feeler->m_object);
    if (object.mp_object)
        object.mp_object->RemoveSmartPointer(&object);
}

} // namespace

// 0x00508780
bool CorePhysicsComponent::CheckAirForwardCollision(const Mth::Vector* from)
{
    void* object = ObjectOf(this);
    const Mth::Vector entryVelocity = VectorAt(object, kVelocity); // (not original: for NotePath)
    // The move's direction (x, y, z normalised - z's difference kept unrounded, as the original does; w as it is).
    const Mth::Vector& position = VectorAt(object, kPosition);
    float moveW = position.w - from->w;
    double dz = static_cast<double>(position.z) - from->z;
    float dy = position.y - from->y;
    float dx = position.x - from->x;
    Mth::Vector direction = {dx, dy, static_cast<float>(dz), 0.0f};
    double length = std::sqrt((static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) + dz * dz);
    if (length > 0.0) {
        double scale = 1.0 / length;
        direction.x = static_cast<float>(dx * scale);
        direction.y = static_cast<float>(dy * scale);
        direction.z = static_cast<float>(dz * scale);
    }

    // The feeler: from `from` raised to the collision height, to the new position raised, plus the length along the
    // move.
    float height = PhysicsFloat(kForwardHeight, object);
    const Mth::Vector& up = VectorAt(object, kUp);
    Mth::Vector raise = {height * up.x, height * up.y, height * up.z, height * up.w};
    Mth::Vector& start = VectorAt(this, kFeelerStart);
    start.x = raise.x + from->x;
    start.y = raise.y + from->y;
    start.z = raise.z + from->z;
    start.w = raise.w + from->w;
    float reach = PhysicsFloat(kForwardLength, object);
    float aheadW = moveW * reach, aheadZ = direction.z * reach, aheadY = direction.y * reach,
          aheadX = reach * direction.x;
    const Mth::Vector& now = VectorAt(ObjectOf(this), kPosition);
    double raisedW = static_cast<double>(raise.w) + now.w; // (unrounded)
    float raisedZ = raise.z + now.z, raisedY = raise.y + now.y, raisedX = raise.x + now.x;
    Mth::Vector& end = VectorAt(this, kFeelerEnd);
    end.x = raisedX + aheadX;
    end.y = raisedY + aheadY;
    end.z = raisedZ + aheadZ;
    end.w = static_cast<float>(raisedW + aheadW);
    if (!Feel(this, 0x10, 0))
        return false;

    const Mth::Vector normal = VectorAt(this, kHitNormal);
    const Mth::Vector* hitNormal = &VectorAt(this, kHitNormal);
    if (At<uint8_t>(this, kHitSkatable)) {
        // Something to skate on: only a wall when it's unlike the ground (and not facing up).
        const Mth::Vector& ground = VectorAt(this, kGroundNormal);
        double dot = (static_cast<double>(normal.z) * ground.z + static_cast<double>(normal.y) * ground.y) +
                     static_cast<double>(normal.x) * ground.x;
        if (!(dot < static_cast<double>(0.8f)) || !(normal.y < 0.5f))
            return false;
    }

    FeelerStorage hit;
    CopyFeeler(hit.get(), &At<uint8_t>(this, kFeeler));
    bool steep = VectorAt(this, kHitNormal).y < 0.1f;
    // Try the skater at the feeler's end (lowered back): 0x004FFDB0 may take it there - then stopped, when going up
    // fast or the wall is steep (left there).
    Mth::Vector& p = VectorAt(ObjectOf(this), kPosition);
    Mth::Vector saved = p;
    p.x = end.x - raise.x;
    p.y = end.y - raise.y;
    p.z = end.z - raise.z;
    p.w = end.w - raise.w;
    if (StepUp(from) &&
        (VectorAt(ObjectOf(this), kVelocity).y > 10.0f || steep)) {
        ReleaseFeelerObject(hit);
        return true;
    }
    VectorAt(ObjectOf(this), kPosition) = saved;
    AssignFeeler(&At<uint8_t>(this, kFeeler), hit.get());

    // A ledge: snap up onto it (going up, upright, against a flat-topped wall) - the feeler raised by the snap height,
    // then lowered 2 at a time until it hits (the snap height going down with it, to 4).
    void* o = ObjectOf(this);
    if (VectorAt(o, kUp).y > 0.5f && VectorAt(o, kVelocity).y > 0.0f && normal.y < 0.01f) {
        float snap = PhysicsFloat(kAirSnapUp, object);
        hit->m_line.start.y = hit->m_line.start.y + snap;
        hit->m_line.end.y = hit->m_line.end.y + snap;
        if (!hit->GetCollision(true, false)) {
            if (snap > 4.0f) {
                double lower;
                do {
                    hit->m_line.start.y = hit->m_line.start.y - 2.0f;
                    hit->m_line.end.y = hit->m_line.end.y - 2.0f;
                    if (hit->GetCollision(true, false))
                        break;
                    lower = static_cast<double>(snap) - 2.0;
                    snap = static_cast<float>(lower);
                } while (lower > 4.0); // (the unrounded value compared)
            }
            Mth::Vector& position2 = VectorAt(ObjectOf(this), kPosition);
            position2.y = snap + position2.y;
            DestroyFeeler(hit.get());
            return true;
        }
    }

    if (Flag(this, 0x30)) {
        // On vert: back at the hit, kept the min distance off it, the velocity along it.
        const Mth::Vector& point = VectorAt(this, kHitPoint);
        Mth::Vector& q = VectorAt(ObjectOf(this), kPosition);
        q.x = point.x - raise.x;
        q.y = point.y - raise.y;
        q.z = point.z - raise.z;
        q.w = point.w - raise.w;
        float distance = PhysicsFloat(kMinDistanceToWall, object);
        Mth::Vector& r = VectorAt(ObjectOf(this), kPosition);
        r.x = static_cast<float>(static_cast<double>(distance) * normal.x + r.x);
        r.y = static_cast<float>(static_cast<double>(normal.y) * distance + r.y);
        r.z = static_cast<float>(static_cast<double>(normal.z) * distance + r.z);
        r.w = static_cast<float>(static_cast<double>(normal.w) * distance + r.w);
        Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
        if (Flag(this, 0x78))
            velocity.RotateToPlane(*hitNormal);
        else
            velocity.ProjectToPlane(normal);
        CollisionEvents::NotePath(this, CollisionEvents::Path::AirWallVert, entryVelocity); // (not original)
        ReleaseFeelerObject(hit);
        return false;
    }

    // A bonk - unless a wallplant (not with +0x302) or 0x00508140 takes over.
    bool done = false;
    if (!Flag(this, 0xE0) && !Flag(this, 0x70)) {
        if (!At<uint8_t>(this, 0x302) && WallPlant(false, false, false))
            done = true;
        else if (StartWallRide())
            done = true;
    }
    if (!done) {
        SetFlag(this, 0xB0, 1);
        TriggerFeeler(this, g_bonkEvents, kFeeler);
        done = TriggerTookOver(this);
    }
    if (!done) {
        if (VectorAt(this, kHitNormal).y < 0.05f) {
            // The bonk's sound: by speed and terrain - on a vehicle its own script.
            if (!At<uint8_t>(this, 0x2ED)) {
                uint8_t terrain = At<uint8_t>(this, kSurfaceFlags);
                void* skater = ObjectOf(this);
                float speed = static_cast<float>(LengthOf(VectorAt(skater, kVelocity)));
                float fraction = speed / static_cast<Obj::CSkater*>(skater)->GetScaledPhysics(kMaxMaxSpeed);
                static_cast<Obj::CSkaterSoundComponent*>(At<void*>(this, kSoundComponent))->PlayBonk(fraction, terrain);
            } else {
                Script::RunScript(kVehicleLandSound, nullptr, static_cast<Obj::CObject*>(ObjectOf(this)), false,
                                  nullptr);
            }
        }
        // The velocity along the wall (keeping its height when the wall isn't facing down), pushed off it by a
        // tenth of the speed.
        float keptY = 0.0f;
        if (normal.y > -0.1f) {
            Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
            keptY = v.y;
            v.y = 0.0f;
        }
        VectorAt(ObjectOf(this), kVelocity).ProjectToPlane(normal);
        Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
        float push = static_cast<float>(LengthOf(velocity) * 0.1f);
        velocity += normal * push;
        if (normal.y > -0.1f)
            VectorAt(ObjectOf(this), kVelocity).y = keptY;
        // (The original clears the facing's y around this copy and puts it back.)
        VectorAt(this, 0x14F0) = normal;
        // The facing turned a little away from the wall; the matrix made square again around it.
        void* s = ObjectOf(this);
        VectorAt(s, kFacing) += 0.05f * normal;
        VectorAt(ObjectOf(this), kFacing).Normalize();
        s = ObjectOf(this);
        VectorAt(s, kRight) = Mth::CrossProduct(VectorAt(s, kUp), VectorAt(s, kFacing));
        VectorAt(ObjectOf(this), kRight).Normalize();
        s = ObjectOf(this);
        VectorAt(s, kUp) = Mth::CrossProduct(VectorAt(s, kFacing), VectorAt(s, kRight));
        VectorAt(ObjectOf(this), kUp).Normalize();
        // A moving object that was hit pushes the skater along (when it's moving into the skater).
        auto& feeler = At<Nx::CFeeler>(this, kFeeler);
        if (feeler.HitObjectAlive()) {
            Mth::Vector carried = VectorAt(feeler.GetHitObject(), kVelocity);
            double into = (static_cast<double>(carried.z) * hitNormal->z + static_cast<double>(carried.y) * hitNormal->y) +
                          static_cast<double>(carried.x) * hitNormal->x;
            if (into > 0.0) {
                float frame = At<float>(this, kFrameLength);
                void* t = ObjectOf(this);
                VectorAt(t, kPosition) += (carried * frame) * 2.0f;
                VectorAt(ObjectOf(this), kVelocity) += carried;
            }
        }
        CollisionEvents::NotePath(this, CollisionEvents::Path::AirBonk, entryVelocity); // (not original)
    }
    DestroyFeeler(hit.get());
    return false;
}

// 0x0050A2C0
bool CorePhysicsComponent::CheckSideCollision(float side, float length)
{
    if (StateOf(this) == kWallRide)
        return false;
    void* object = ObjectOf(this);
    float height = Script::GetFloat(kSideHeight);
    const Mth::Vector& up = VectorAt(object, kUp);
    float raiseW = height * up.w, raiseZ = height * up.z, raiseY = height * up.y, raiseX = height * up.x;
    const Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
    Mth::Vector& start = VectorAt(this, kFeelerStart);
    start.x = raiseX + position.x;
    start.y = raiseY + position.y;
    start.z = raiseZ + position.z;
    start.w = raiseW + position.w;
    float reach = length;
    if (StateOf(this) == kAir)
        reach = Script::GetFloat(kAirExtraSide) + length;
    // Out along the right (z and w from the unrounded side products, as the original).
    const Mth::Vector& right = VectorAt(ObjectOf(this), kRight);
    float outY = (side * right.y) * reach;
    float outX = (side * right.x) * reach;
    float outW = static_cast<float>(static_cast<double>(side) * right.w * reach);
    float outZ = static_cast<float>(static_cast<double>(side) * right.z * reach);
    Mth::Vector& end = VectorAt(this, kFeelerEnd);
    end.x = outX + start.x;
    end.y = outY + start.y;
    end.z = outZ + start.z;
    end.w = outW + start.w;
    if (!Feel(this, 0x10, 0))
        return false;

    Mth::Vector normal = VectorAt(this, kHitNormal);
    float overlap = 0.0f; // (uninitialised in the original when not in the air)
    if (StateOf(this) == kAir) {
        if (!Flag(this, 0xE0) && !Flag(this, 0x70) && StartWallRide())
            return true;
        // Pushed out of it by a tenth of how far into it the skater is (the hit's distance under the reach, less
        // the extra air reach).
        const Mth::Vector& point = VectorAt(this, kHitPoint);
        Mth::Vector into = {point.x - start.x, point.y - start.y, point.z - start.z, point.w - start.w};
        double distance = std::sqrt((static_cast<double>(into.z) * into.z + static_cast<double>(into.y) * into.y) +
                                    static_cast<double>(into.x) * into.x);
        double depth = distance - (static_cast<double>(reach) - length);
        overlap = static_cast<float>(depth);
        if (!(depth > 0.0))
            return false;
        into.SetLength(overlap);
        VectorAt(ObjectOf(this), kPosition) -= into / 10.0f;
        const uint8_t* input = InputOf(this);
        if (StateOf(this) != kAir && !input[0x100] && !input[0xA0] && !input[0x60] && !input[0x80])
            BounceOffWall(&normal, &overlap, 0.2f); // (never: the state is the air's)
        return false;
    }
    // On the ground: a near-vertical wall puts the skater back where it was (+0x98) ...
    double facingUp = (static_cast<double>(normal.z) * up.z + static_cast<double>(normal.y) * up.y) +
                      static_cast<double>(normal.x) * up.x;
    if (!(facingUp < static_cast<double>(0.25f)) || !(facingUp > static_cast<double>(-0.25f)))
        return false;
    const Mth::Vector entryVelocity = VectorAt(ObjectOf(this), kVelocity); // (not original: for NotePath)
    VectorAt(ObjectOf(this), kPosition) = VectorAt(this, 0x98);
    BounceOffWall(&normal, &overlap, 0.2f);
    CollisionEvents::NotePath(this, CollisionEvents::Path::SideWall, entryVelocity); // (not original)
    // ... then, unless there's something between (a feeler from just off the hit out to the length) or a trigger's
    // script takes over, onto the wall: at that feeler's end, lowered by the side height.
    const Mth::Vector& point = VectorAt(this, kHitPoint);
    Mth::Vector outer = point + normal * length;
    Mth::Vector inner = point + normal;
    FeelerStorage feeler;
    feeler->Construct(&inner, &outer);
    if (feeler->GetCollision(true, false)) {
        DestroyFeeler(feeler.get());
        return false;
    }
    TriggerFeeler(this, g_bonkEvents, kFeeler);
    if (TriggerTookOver(this)) {
        DestroyFeeler(feeler.get());
        return false;
    }
    void* skater = ObjectOf(this);
    float lower = Script::GetFloat(kSideHeight);
    VectorAt(skater, kPosition) = feeler->m_line.end - VectorAt(skater, kUp) * lower;
    DestroyFeeler(feeler.get());
    return true;
}

// 0x0050C930
void CorePhysicsComponent::CheckSideCollisions()
{
    Mth::Vector saved = VectorAt(ObjectOf(this), kPosition);
    float length = Script::GetFloat(kSideLength);
    bool both;
    if (CheckSideCollision(-1.0f, length))
        both = CheckSideCollision(1.0f, length);
    else
        both = CheckSideCollision(1.0f, length) && CheckSideCollision(-1.0f, length);
    if (both)
        VectorAt(ObjectOf(this), kPosition) = saved;
}

} // namespace Skater
