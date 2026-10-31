// The skater's core physics component: the wall ride state's update.

#include <cmath>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/gfx/d3dx.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/script/struct.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/skater/trick_component.h"
#include "game/obj/trigger_events.h"

namespace Skater {

using namespace CoreFields;

namespace {

constexpr uint32_t kWallRideGravity = 0x0C617CAF;    // Wall_Ride_Gravity
constexpr uint32_t kWallRideTurnSpeed = 0x64A48A64;  // Wall_Ride_Turn_Speed
constexpr uint32_t kForwardLength = 0x20102726;      // Skater_First_Forward_Collision_Length
constexpr uint32_t kGroundSnapUp = 0xE4D79235;       // Physics_Ground_Snap_Up
constexpr uint32_t kDownCheckLength = 0x438BA168;    // Wall_Ride_Down_Collision_Check_Length
constexpr uint32_t kJumpOutSpeed = 0x349D02D4;       // Wall_Ride_Jump_Out_Speed
constexpr uint32_t kJumpUpSpeed = 0x5A9DE35A;        // Wall_Ride_Jump_Up_Speed
constexpr uint32_t kMatrixQueries = 0xBF518E27;      // SkaterMatrixQueries (a component)
constexpr uint32_t kLanded = 0x532B16EF;             // Landed (event)
constexpr uint32_t kOffEdge = 0xD96F01F1;            // SkaterOffEdge (event)
constexpr uint32_t kGroundGone = 0x3B1001B6;         // GroundGone (event)

constexpr uint32_t kWallNormal = 0x38C;              // the wall's normal

constexpr float kHeadingUp = 0.68f;   // 0x0064BDB4: heading up the wall this much: no turning
constexpr float kTopFacesUp = 0.9f;   // 0x00649FE4: a hit ahead facing up this much: the wall's top
constexpr float kPushOff = 18.0f;     // 0x0064AAAC
constexpr float kPushAway = 100.0f;   // 0x006463D4

const Mth::Vector kUpAxis = {0.0f, 1.0f, 0.0f, 1.0f};

void ChangeState(void* component, int state) { static_cast<CorePhysicsComponent*>(component)->SetPhysicsState(state); }
void SetUp(void* component, const Mth::Vector& up)
{
    static_cast<CorePhysicsComponent*>(component)->TurnToNormal(up);
}
// velocity or position += normal * scale, x and y from the unrounded products (as the original).
void AddScaled(Mth::Vector& to, const Mth::Vector& normal, float scale)
{
    float z = normal.z * scale, w = normal.w * scale;
    to.x = static_cast<float>(static_cast<double>(normal.x) * scale + to.x);
    to.y = static_cast<float>(static_cast<double>(normal.y) * scale + to.y);
    to.z = z + to.z;
    to.w = w + to.w;
}

} // namespace

// (Part of 0x0050F830) Off the wall's edge: the up straight, pushed out 18 from the wall, into the air.
void CorePhysicsComponent::OffWallEdge(bool resetDisplay)
{
    SetUp(this, kUpAxis);
    if (resetDisplay)
        MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
    AddScaled(VectorAt(ObjectOf(this), kPosition), VectorAt(this, kWallNormal), kPushOff);
    ChangeState(this, kAir);
    static_cast<Obj::CObject*>(ObjectOf(this))->BroadcastEvent(kOffEdge, nullptr, 0.0f);
    static_cast<Obj::CObject*>(ObjectOf(this))->SelfEvent(kGroundGone, nullptr);
}

// 0x0050F830
void CorePhysicsComponent::DoWallRidePhysics()
{
    SetFlag(this, 0x78, 0);
    SetFlag(this, 0xA8, 0);
    SetFlag(this, 0x80, 0);
    SetFlag(this, 0xB0, 0);
    At<uint8_t>(this, 0x2E4) = 0;
    At<void*>(this, kRailNode) = nullptr;
    At<uint32_t>(this, kLastRailScript) = 0;
    At<int>(this, 0x39C) = Now();

    // What the skater rides on carries it (its turn too).
    Mth::Vector carried = {0.0f, 0.0f, 0.0f, 1.0f};
    if (Obj::CRideTracker* tracker = Tracker(this)) {
        if (!tracker->m_object.mp_object || !tracker->Update(&VectorAt(ObjectOf(this), kPosition))) {
            StopRiding(this);
        } else {
            carried = Tracker(this)->m_carried;
            if (Tracker(this)->m_turned) {
                auto multiply =
                    D3dx::MatrixMultiply;
                Mth::Matrix& m = MatrixAt(ObjectOf(this), kMatrix);
                multiply(&m, &m, &Tracker(this)->m_turn);
                multiply(&MatrixAt(this, kDisplayMatrix), &MatrixAt(this, kDisplayMatrix), &Tracker(this)->m_turn);
            }
        }
    }

    // The move: velocity * t + (0, gravity, 0, 1) * t * t / 2 (rounded as the original), plus the carry; then the
    // velocity += (0, gravity, 0, 1) * t.
    void* object = ObjectOf(this);
    float gravity = PhysicsFloat(kWallRideGravity, object);
    {
        float t = At<float>(this, kFrameLength);
        double half = static_cast<double>(t) * t * 0.5f;
        double zeroHalf = 0.0f * half;
        float zeroHalfRounded = static_cast<float>(zeroHalf);
        float gravityHalf = static_cast<float>(half * gravity);
        float halfRounded = static_cast<float>(half);
        Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
        double tx = static_cast<double>(t) * v.x;
        float ty = t * v.y, tz = t * v.z, tw = t * v.w;
        float mx = static_cast<float>(tx + zeroHalfRounded);
        float my = ty + gravityHalf;
        double mz = zeroHalf + tz;
        double mw = static_cast<double>(tw) + halfRounded;
        Mth::Vector& p = VectorAt(ObjectOf(this), kPosition);
        p.x = mx + p.x;
        p.y = my + p.y;
        p.z = static_cast<float>(mz + p.z);
        p.w = static_cast<float>(mw + p.w);
        p.x = carried.x + p.x;
        p.y = carried.y + p.y;
        p.z = carried.z + p.z;
        p.w = carried.w + p.w;
        float tt = At<float>(this, kFrameLength);
        double zeroT = 0.0f * static_cast<double>(tt);
        float fall = tt * gravity;
        Mth::Vector& u = VectorAt(ObjectOf(this), kVelocity);
        u.x = static_cast<float>(zeroT) + u.x;
        u.y = fall + u.y;
        u.z = static_cast<float>(zeroT + u.z);
        u.w = tt + u.w;
    }

    // Up the wall: the wall's level tangent (normalised) crossed with its normal.
    const Mth::Vector& wall = VectorAt(this, kWallNormal);
    double tangentZ = wall.z, tangentX = -static_cast<double>(wall.x);
    float tangentY = 0.0f;
    double tangentLength = std::sqrt(tangentZ * tangentZ + tangentX * tangentX);
    if (tangentLength > 0.0) {
        double inverse = 1.0 / tangentLength;
        tangentZ *= inverse;
        tangentY = static_cast<float>(0.0 * inverse);
        tangentX *= inverse;
    }
    Mth::Vector upWall = carried;
    upWall.x = static_cast<float>(tangentY * wall.z - tangentX * wall.y);
    upWall.y = static_cast<float>(tangentX * wall.x - tangentZ * wall.z);
    upWall.z = static_cast<float>(tangentZ * wall.y - tangentY * wall.x);

    // Turned towards up the wall when not heading up it enough (a turn's cosine at most).
    float turn = Script::GetFloat(kWallRideTurnSpeed);
    const Mth::Vector& facing = VectorAt(ObjectOf(this), kFacing);
    double heading = (static_cast<double>(upWall.z) * facing.z + static_cast<double>(upWall.y) * facing.y) +
                     static_cast<double>(upWall.x) * facing.x;
    bool turning = !(heading > kHeadingUp) && !(heading > 0.0 && heading > std::cos(static_cast<double>(turn)));
    if (turning) {
        const Mth::Vector& right = VectorAt(ObjectOf(this), kRight);
        double side = (static_cast<double>(upWall.z) * right.z + static_cast<double>(upWall.y) * right.y) +
                      static_cast<double>(upWall.x) * right.x;
        if (side <= 0.0)
            turn = -turn;
        void* o = ObjectOf(this);
        VectorAt(o, kVelocity).w = 1.0f;
        VectorAt(ObjectOf(this), kVelocity).RotateAboutAxis(VectorAt(this, kWallNormal), turn); // (about the wall)
        o = ObjectOf(this);
        VectorAt(o, kFacing) = VectorAt(o, kVelocity);
        VectorAt(ObjectOf(this), kFacing).Normalize();
        o = ObjectOf(this);
        VectorAt(o, kUp) = VectorAt(this, kWallNormal);
        const Mth::Vector& f = VectorAt(o, kFacing);
        const Mth::Vector& u = VectorAt(o, kUp);
        Mth::Vector& r = VectorAt(o, kRight);
        float rx = static_cast<float>(static_cast<double>(f.z) * u.y - static_cast<double>(f.y) * u.z);
        float ry = static_cast<float>(static_cast<double>(f.x) * u.z - static_cast<double>(f.z) * u.x);
        float rz = static_cast<float>(static_cast<double>(f.y) * u.x - static_cast<double>(f.x) * u.y);
        r.w = 0.0f;
        r.z = rz;
        r.x = rx;
        r.y = ry;
        VectorAt(ObjectOf(this), kRight).Normalize();
        MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
    }

    // A feeler ahead along the move (Skater_First_Forward_Collision_Length past the new position).
    {
        void* o = ObjectOf(this);
        const Mth::Vector& p = VectorAt(o, kPosition);
        const Mth::Vector& old = VectorAt(o, kOldPosition);
        Mth::Vector move = {p.x - old.x, p.y - old.y, p.z - old.z, p.w - old.w};
        double moved = std::sqrt((static_cast<double>(move.z) * move.z + static_cast<double>(move.y) * move.y) +
                                 static_cast<double>(move.x) * move.x);
        if (moved > 0.0) {
            double inverse = 1.0 / moved;
            move.x = static_cast<float>(move.x * inverse);
            move.y = static_cast<float>(move.y * inverse);
            move.z = static_cast<float>(inverse * move.z);
        }
        VectorAt(this, kFeelerStart) = VectorAt(o, kOldPosition);
        Mth::Vector& end = VectorAt(this, kFeelerEnd);
        end = VectorAt(ObjectOf(this), kPosition);
        float reach = PhysicsFloat(kForwardLength, ObjectOf(this));
        double aheadX = static_cast<double>(move.x) * reach;
        float aheadY = reach * move.y, aheadZ = reach * move.z, aheadW = reach * move.w;
        end.x = static_cast<float>(aheadX + end.x);
        end.y = aheadY + end.y;
        end.z = aheadZ + end.z;
        end.w = aheadW + end.w;
    }
    if (Feel(this, 0x10, 0)) {
        const Mth::Vector hit = VectorAt(this, kHitNormal);
        const Mth::Vector& w = VectorAt(this, kWallNormal);
        double alongWall = (static_cast<double>(hit.z) * w.z + static_cast<double>(hit.y) * w.y) +
                           static_cast<double>(hit.x) * w.x;
        const Mth::Vector& point = VectorAt(this, kHitPoint);
        Mth::Vector& p = VectorAt(ObjectOf(this), kPosition);
        p.z = hit.z + point.z;
        p.w = hit.w + point.w;
        p.x = hit.x + point.x;
        p.y = hit.y + point.y;
        if (At<uint8_t>(this, 0x22A) && !(std::fabs(alongWall) < 0.01f)) {
            // Round a corner: the new wall.
            VectorAt(this, kWallNormal) = hit;
            SetUp(this, VectorAt(this, kWallNormal));
            MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
            VectorAt(ObjectOf(this), kVelocity).RotateToPlane(VectorAt(this, kWallNormal));
        } else if (hit.y > kTopFacesUp) {
            // Onto the top: the ground (a grind first), upright on it, heading along the velocity.
            void* o = ObjectOf(this);
            VectorAt(o, kPosition) = VectorAt(this, kHitPoint);
            VectorAt(o, kFacing) = VectorAt(o, kVelocity);
            VectorAt(ObjectOf(this), kFacing).ProjectToPlane(hit);
            VectorAt(ObjectOf(this), kFacing).Normalize();
            VectorAt(ObjectOf(this), kUp) = hit;
            void* s = ObjectOf(this);
            VectorAt(s, kRight) = Mth::CrossProduct(VectorAt(s, kUp), VectorAt(s, kFacing));
            VectorAt(ObjectOf(this), kRight).Normalize();
            MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
            auto* queries = static_cast<Obj::CCompositeObject*>(ObjectOf(this))->GetComponent(kMatrixQueries);
            MatrixAt(queries, 0x18) = MatrixAt(At<void*>(queries, 0xC), kMatrix);
            VectorAt(this, 0x78) = VectorAt(ObjectOf(this), kUp);
            VectorAt(this, 0x58) = VectorAt(ObjectOf(this), kUp);
            VectorAt(this, kGroundNormal) = VectorAt(ObjectOf(this), kUp);
            TryGrind(true);
            if (StateOf(this) == kRail)
                return;
            ChangeState(this, kGround);
            static_cast<Obj::CObject*>(ObjectOf(this))->SelfEvent(kLanded, nullptr);
            return;
        } else {
            OffWallEdge(false);
        }
    }
    if (KeepOffCeilingAtWall())
        OffWallEdge(false);

    // A feeler down the skater's up: still on the wall?
    {
        void* o = object;
        float snap = PhysicsFloat(kGroundSnapUp, o);
        const Mth::Vector& up = VectorAt(ObjectOf(this), kUp);
        Mth::Vector& start = VectorAt(this, kFeelerStart);
        start = {snap * up.x, snap * up.y, snap * up.z, snap * up.w};
        const Mth::Vector& p = VectorAt(ObjectOf(this), kPosition);
        start.x = start.x + p.x, start.y = p.y + start.y, start.z = p.z + start.z, start.w = p.w + start.w;
        float down = Script::GetFloat(kDownCheckLength);
        const Mth::Vector& up2 = VectorAt(ObjectOf(this), kUp);
        Mth::Vector& end = VectorAt(this, kFeelerEnd);
        end = {down * up2.x, down * up2.y, down * up2.z, down * up2.w};
        const Mth::Vector& q = VectorAt(ObjectOf(this), kPosition);
        end.x = end.x + q.x, end.y = q.y + end.y, end.z = q.z + end.z, end.w = q.w + end.w;
    }
    if (Feel(this, 0x10, 0)) {
        // At the wall (just off it), along it.
        VectorAt(this, kWallNormal) = VectorAt(this, kHitNormal);
        void* o = ObjectOf(this);
        VectorAt(o, kPosition) = VectorAt(this, kHitPoint);
        Mth::Vector& p = VectorAt(ObjectOf(this), kPosition);
        const Mth::Vector& w = VectorAt(this, kWallNormal);
        p.x = p.x + w.x, p.y = w.y + p.y, p.z = w.z + p.z, p.w = w.w + p.w;
        VectorAt(ObjectOf(this), kVelocity).RotateToPlane(VectorAt(this, kWallNormal));
        SetUp(this, VectorAt(this, kWallNormal));
        MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
        if (!At<uint8_t>(this, 0x22A)) {
            // Not a wall to ride: pushed away from it into the air.
            AddScaled(VectorAt(ObjectOf(this), kVelocity), VectorAt(this, kWallNormal), kPushAway);
            SetUp(this, kUpAxis);
            MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
            ChangeState(this, kAir);
            static_cast<Obj::CObject*>(ObjectOf(this))->BroadcastEvent(kOffEdge, nullptr, 0.0f);
            static_cast<Obj::CObject*>(ObjectOf(this))->SelfEvent(kGroundGone, nullptr);
            TriggerFeeler(this, g_otherFeelerLeaveEvents, kOtherFeeler);
            if (TriggerTookOver(this))
                return;
        } else {
            if (At<int>(this, 0xFC) != At<int>(this, 0x1AC)) {
                TriggerFeeler(this, g_otherFeelerLeaveEvents, kOtherFeeler);
                if (TriggerTookOver(this))
                    return;
                TriggerFeeler(this, g_skaterEvents0064BBB0, kOtherFeeler);
                if (TriggerTookOver(this))
                    return;
            }
            void* skater = At<void*>(this, kSkaterComponent);
            if (!At<uint8_t>(skater, 0x13B0))
                static_cast<Obj::CTrickComponent*>(skater)->SetTrickObjects(false);
            AssignFeeler(&At<uint8_t>(this, kOtherFeeler), &At<uint8_t>(this, kFeeler));
            At<uint8_t>(At<void*>(this, kSoundComponent), 0x1D) = At<uint8_t>(this, 0x1AA);
        }
    } else {
        OffWallEdge(true);
        TriggerFeeler(this, g_otherFeelerLeaveEvents, kOtherFeeler);
        if (TriggerTookOver(this))
            return;
    }

    if (!Flag(this, 0x20) && InputOf(this)[0x1C0])
        SetFlag(this, 0x20, 1);
    // Jumping off: out from the wall and up, upright.
    if (JumpReleased()) {
        SetFlag(this, 0xD8, 1);
        TriggerFeeler(this, g_jumpTriggerEvents, kOtherFeeler);
        if (TriggerTookOver(this))
            return;
        float out = Script::GetFloat(kJumpOutSpeed);
        Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
        const Mth::Vector& w = VectorAt(this, kWallNormal);
        float y = out * w.y, z = out * w.z, ww = out * w.w;
        v.x = static_cast<float>(static_cast<double>(out) * w.x + v.x);
        v.y = y + v.y;
        v.z = z + v.z;
        v.w = ww + v.w;
        float& vy = VectorAt(ObjectOf(this), kVelocity).y;
        vy = Script::GetFloat(kJumpUpSpeed) + vy;
        VectorAt(ObjectOf(this), kUp) = kUpAxis;
        MatrixAt(ObjectOf(this), kMatrix).OrthoNormalizeAbout(1);
        MatrixAt(this, kDisplayMatrix) = MatrixAt(ObjectOf(this), kMatrix);
        At<int>(this, 0x88) = 0;
        VectorAt(this, 0x58) = VectorAt(ObjectOf(this), kUp);
        VectorAt(this, kGroundNormal) = VectorAt(this, 0x58);
        VectorAt(this, 0x78) = VectorAt(this, 0x58);
    }
    static_cast<Obj::CTrickComponent*>(At<void*>(this, kSkaterComponent))->PassToTrickObjects(At<uint32_t>(this, 0x1B0));
}

} // namespace Skater
