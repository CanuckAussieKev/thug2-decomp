// The skater's core physics component: the ground state's update.

#include <cmath>
#include <cstdint>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/ride_tracker.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/collision_events.h"
#include "game/skater/core_fields.h"
#include "game/skater/core_physics.h"
#include "game/skater/manual.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/trick_component.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace CoreFields;

namespace {

constexpr uint32_t kGroundGravity = 0x6618A713;   // Physics_Ground_Gravity
constexpr uint32_t kSkitchSpeedMatch = 0xDEF25B34; // skitch_speed_match
constexpr uint32_t kSkitch = 0x3506CE64;          // skitch (the balance trick's kind)
constexpr uint32_t kManual = 0xEF24413B;          // Manual
constexpr uint32_t kNoseManual = 0x0AC90769;      // (another manual kind)
constexpr uint32_t kPerfectSkitch = 0x69A1CE96;   // CHEAT_PERFECT_SKITCH
constexpr uint32_t kPerfectManual = 0xB38341C9;   // CHEAT_PERFECT_MANUAL

constexpr float kRadiansToDegrees = 57.2957764f; // 0x00645468
constexpr float kFullTurn = 360.0f;              // 0x00645EA8
constexpr float kMoving = 0.001f;                // 0x006461D8
constexpr float kSkitchFeelUp = 36.0f;           // 0x0064B9CC
constexpr float kSkitchFeelDown = 200.0f;        // 0x0064BDB0

Obj::CSkaterCareer* Career() { return At<Obj::CSkaterCareer*>(GameVariable<uint8_t*>(0x007CE478), 0x20); }

// A cheat that keeps a balance trick perfect: its global flag set, the balance's lean (`lean`, two words) zeroed.
void PerfectCheat(void* component, uint32_t cheat, uint32_t lean)
{
    if (!Career()->GetCheat(cheat))
        return;
    Career()->SetGlobalFlag(Script::GetInteger(cheat));
    At<uint32_t>(At<void*>(component, kBalanceTrick), lean) = 0;
    At<uint32_t>(At<void*>(component, kBalanceTrick), lean + 4) = 0;
    GameVariable<uint8_t>(0x007CE4FD) = 1;
}

} // namespace

// 0x0050EE70
void CorePhysicsComponent::DoGroundPhysics()
{
    for (uint32_t flag : {0x48u, 0x30u, 0x38u, 0x78u, 0xA8u, 0x80u, 0xB0u})
        SetFlag(this, flag, 0);
    At<void*>(this, kRailNode) = nullptr;
    At<uint32_t>(this, kLastRailScript) = 0;
    At<uint8_t>(this, 0x4E8) = 0;
    At<uint8_t>(this, 0x2D0) = 0;
    At<uint8_t>(this, 0x2E4) = 0;
    At<int>(this, 0x14CC) = 0;

    // The heading (+0x348, degrees round from the facing).
    void* object = ObjectOf(this);
    double heading = static_cast<double>(Mth::FastACos(VectorAt(object, kFacing).x)) * kRadiansToDegrees;
    At<float>(this, 0x348) = static_cast<float>(heading);
    if (VectorAt(object, kFacing).z < 0.0f)
        At<float>(this, 0x348) = static_cast<float>(kFullTurn - heading);
    if (ThawBmx::Active(this)) // (not original: THAW 0x005E4579, the time on vert ground noted - thaw_bmx_physics.cpp)
        ThawBmx::NoteVertGround(this);
    // Moving: the velocity turned into the ground's plane.
    if (LengthOf(VectorAt(object, kVelocity)) > kMoving)
        VectorAt(object, kVelocity).RotateToPlane(VectorAt(this, kGroundNormal));
    // Gravity along the ground (+0x360's instead going up while +0x350 is set).
    Mth::Vector gravity = {0.0f, PhysicsFloat(kGroundGravity, ObjectOf(this)), 0.0f, 1.0f};
    if (!(At<float>(this, 0x350) == 0.0f) && VectorAt(ObjectOf(this), kVelocity).y > 0.0f)
        gravity = {0.0f, At<float>(this, 0x360), 0.0f, 1.0f};
    gravity.ProjectToPlane(VectorAt(this, kGroundNormal));
    bool bike = ThawBmx::Active(this); // (not original: THAW's bike branches, thaw_bmx_physics.cpp)
    if (bike)
        ThawBmx::OnGround(this);
    if (!bike || ThawBmx::GroundGravityApplies(this, gravity)) {
        float t = At<float>(this, kFrameLength);
        Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
        float gy = gravity.y * t, gz = gravity.z * t, gw = gravity.w * t;
        v.x = static_cast<float>(static_cast<double>(gravity.x) * t + v.x);
        v.y = gy + v.y;
        v.z = gz + v.z;
        v.w = gw + v.w;
    }

    // The balance trick.
    At<uint8_t>(this, 0x303) = 0;
    uint32_t kind = At<uint32_t>(At<void*>(this, kBalanceTrick), 0x18);
    if (kind == 0) {
        if (bike ? ThawBmx::ShouldBrake(this) : ShouldBrake()) {
            Brake();
        } else {
            At<uint8_t>(this, 0x324) = 0;
            if (bike ? ThawBmx::CanKick(this) && ThawBmx::MayAccelerate(this) : CanPushOff())
                PushOff();
        }
    } else if (kind == kSkitch) {
        reinterpret_cast<Obj::CBalance*>(At<uint8_t*>(this, kBalanceTrick) + 0x12C)->Update();
        PerfectCheat(this, kPerfectSkitch, 0x15C);
    } else if (kind == kNoseManual || kind == kManual) {
        At<uint8_t>(this, 0x324) = 0;
        if (At<uint8_t>(this, 0x302)) {
            At<uint8_t>(this, 0x303) = 1;
            if (CanPushOff())
                PushOff();
        }
        reinterpret_cast<Obj::CBalance*>(At<uint8_t*>(this, kBalanceTrick) + 0x24)->Update();
        static_cast<Obj::CTrickComponent*>(At<void*>(this, kSkaterComponent))->SetTrickObjects(true);
        PerfectCheat(this, kPerfectManual, 0x54);
    }
    CollisionEvents::Checkpoint(this, "gravity"); // (not original)
    ApplyGroundFriction();
    CheckSideFeelers();
    CollisionEvents::Checkpoint(this, "friction-side-feelers"); // (not original)

    // The move this frame - skitching, none of its own: the skitched object's velocity (scaled), 0x00501DB0.
    Mth::Vector step;
    {
        float t = At<float>(this, kFrameLength);
        const Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
        step = {t * v.x, t * v.y, t * v.z, t * v.w};
    }
    if (Flag(this, 0x88)) {
        Obj::CRideTracker* tracker = Tracker(this);
        if (tracker && tracker->m_object.mp_object) {
            void* skitched = tracker->m_object.mp_object;
            float match = Script::GetFloat(kSkitchSpeedMatch);
            const Mth::Vector& theirs = VectorAt(skitched, kVelocity);
            VectorAt(ObjectOf(this), kVelocity) = {match * theirs.x, match * theirs.y, match * theirs.z,
                                                   match * theirs.w};
        }
        step = {0.0f, 0.0f, 0.0f, 1.0f};
        Skitch();
    }
    // Carried by what the skater rides on (not while skitching - that's the ride).
    if (Obj::CRideTracker* tracker = Tracker(this)) {
        if (!tracker->m_object.mp_object || !tracker->Update(&VectorAt(ObjectOf(this), kPosition))) {
            StopRiding(this);
        } else if (!Flag(this, 0x88)) {
            void* o = ObjectOf(this);
            VectorAt(o, kPosition) += Tracker(this)->m_carried;
            VectorAt(ObjectOf(this), kOldPosition) = VectorAt(ObjectOf(this), kPosition);
            VectorAt(this, 0x98) = VectorAt(ObjectOf(this), kPosition);
            if (Tracker(this)->m_turned) {
                auto multiply =
                    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C);
                Mth::Matrix& m = MatrixAt(ObjectOf(this), kMatrix);
                multiply(&m, &m, &Tracker(this)->m_turn);
                multiply(&MatrixAt(this, kDisplayMatrix), &MatrixAt(this, kDisplayMatrix), &Tracker(this)->m_turn);
            }
        }
    }

    MoveOnGround(step);
    CollisionEvents::Checkpoint(this, "moved"); // (not original)
    if (StateOf(this) != kGround)
        return;

    // Still on the ground.
    if (!At<uint8_t>(this, 0x2D8))
        TurnOnGround();
    if (bike && StateOf(this) == kGround)
        ThawBmx::ManualPitch(this); // (not original: THAW 0x005E54BA)
    CollisionEvents::Checkpoint(this, "turned"); // (not original)
    if (!At<uint8_t>(this, 0x388))
        AlongFacing(&VectorAt(ObjectOf(this), kVelocity));
    CollisionEvents::Checkpoint(this, "along-facing-4FF590"); // (not original)
    if (!Flag(this, 0x88)) {
        CheckSideCollisions();
        LeanPushOffWall();
    }
    CollisionEvents::Checkpoint(this, "side-collisions"); // (not original)
    // (not original: THAW 0x005CCDD0 returns at once on the bike: rolling backwards faster
    // than Skater_Flip_Speed (1) the skater is turned round to ride fakie, the bike isn't - it rolls on backwards in
    // THAW's backwards balance, bike_backwards_balance_trick)
    if (!At<uint8_t>(this, 0x388) && !bike)
        TurnRoundIfBackwards();
    CollisionEvents::Checkpoint(this, "turn-round-4FF920"); // (not original)
    if (JumpReleased()) {
        StartVertAir();
        SetFlag(this, 0x48, 1);
        SetFlag(this, 0xD8, 1);
    }
    static_cast<Obj::CTrickComponent*>(At<void*>(this, kSkaterComponent))->PassToTrickObjects(At<uint32_t>(this, 0x1B0));
    At<int>(this, 0x30C) = 0;
}

// (Part of 0x0050EE70) The move along the ground: the step, then kept to the ground (skitching: a feeler down the
// up for the terrain under the skater). Stopped short of the step (on the ground, the first time): the rest of the
// step once more, along the velocity.
void CorePhysicsComponent::MoveOnGround(Mth::Vector step)
{
    bool again = false;
    for (;;) {
        Mth::Vector& p = VectorAt(ObjectOf(this), kPosition);
        Mth::Vector before = p; // (x, y, z used)
        p.x = step.x + p.x;
        p.y = step.y + p.y;
        p.z = step.z + p.z;
        p.w = step.w + p.w;
        if (!Flag(this, 0x88))
            CheckWallAhead();
        CollisionEvents::Checkpoint(this, "wall-ahead-check"); // (not original)
        if (Flag(this, 0x88)) {
            // Skitching: a feeler from 36 above to 200 below, along the up, for the ground's terrain.
            void* object = ObjectOf(this);
            const Mth::Vector& up = VectorAt(object, kUp);
            const Mth::Vector& position = VectorAt(object, kPosition);
            float upY = up.y * kSkitchFeelUp, upZ = up.z * kSkitchFeelUp, upW = up.w * kSkitchFeelUp;
            Mth::Vector& start = VectorAt(this, kFeeler);       // (the feeler's own line)
            Mth::Vector& end = VectorAt(this, kFeeler + 0x10);
            start.z = upZ + position.z;
            start.w = upW + position.w;
            start.x = static_cast<float>(static_cast<double>(up.x) * kSkitchFeelUp + position.x);
            start.y = upY + position.y;
            float downY = up.y * kSkitchFeelDown, downZ = up.z * kSkitchFeelDown, downW = up.w * kSkitchFeelDown;
            end.z = position.z - downZ;
            end.w = position.w - downW;
            end.x = static_cast<float>(position.x - static_cast<double>(up.x) * kSkitchFeelDown);
            end.y = position.y - downY;
            if (Feel(this, 0x10, 0)) {
                uint8_t terrain = At<uint8_t>(this, kSurfaceFlags);
                reinterpret_cast<Obj::CSkaterStateComponent*>(FlagsOf(this))->SetTerrain(terrain);
                At<uint8_t>(At<void*>(this, kSoundComponent), 0x1C) = terrain;
            }
        } else {
            CheckForGround();
        }
        CollisionEvents::Checkpoint(this, "ground-check"); // (not original)
        // How far it got against the step.
        const Mth::Vector& now = VectorAt(ObjectOf(this), kPosition);
        double dx = static_cast<double>(now.x) - before.x, dy = static_cast<double>(now.y) - before.y;
        double dz = static_cast<double>(now.z) - before.z;
        double moved = std::sqrt((dx * dx + dz * dz) + dy * dy);
        float stepLength = static_cast<float>(std::sqrt(
            (static_cast<double>(step.z) * step.z + static_cast<double>(step.y) * step.y) +
            static_cast<double>(step.x) * step.x));
        if (again || StateOf(this) != kGround || !(moved < static_cast<double>(stepLength) - 0.1f))
            return;
        // The rest: along the velocity (x, y the unrounded unit vector, z rounded), the step's length, scaled by
        // what's left of it.
        const Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
        double vx = v.x, vy = v.y;
        float vz = v.z;
        float vw = v.w;
        double length = std::sqrt((static_cast<double>(vz) * vz + vy * vy) + vx * vx);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            vx *= inverse;
            vy *= inverse;
            vz = static_cast<float>(vz * inverse);
        }
        again = true;
        float sx = static_cast<float>(vx * stepLength);
        double sy = vy * stepLength;
        double sz = static_cast<double>(vz) * stepLength;
        float sw = vw * stepLength;
        double left = 1.0 - moved / stepLength;
        float leftRounded = static_cast<float>(left);
        step.x = static_cast<float>(left * sx);
        step.y = static_cast<float>(sy * leftRounded);
        step.z = static_cast<float>(sz * leftRounded);
        step.w = sw * leftRounded;
    }
}

} // namespace Skater

REPLACE(0x0050EE70, &Skater::CorePhysicsComponent::DoGroundPhysics);
