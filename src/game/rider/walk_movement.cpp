// The Walk component's movement (GameObjects::CFootMovement, see walk_physics.h): its control, speed and facing, the
// push-back, the jump's steering, the collision cache, and its object's position, orientation and velocity. From the
// disassembly of 0x00526260-0x00529F62.

#include <cmath>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/vec4.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/walk_hang.h"
#include "game/rider/walk_fields.h"
#include "game/rider/walk_physics.h"
#include "engine/maths/x87_math.h"
#include "platform/stopwatch.h"
#include "platform/crt.h"

namespace GameObjects {

namespace {

using namespace WalkFields;

// walk_parameters' names.
constexpr uint32_t kWallTurnSpeedThreshold = 0x0515A933, kMaxWallTurnSpeedThreshold = 0xE6C1CD0D,
                   kWallTurnFactor = 0x7A583B9B, kWalkSpeed = 0x79D182AD, kLerpUprightRate = 0x0F22C135,
                   kFeelerLength = 0x99978D2B, kWalkerHeight = 0x9EA1974A, kSnapDownHeight = 0xAF3E4251,
                   kDisplayOffsetRestoreRate = 0xB32C972B, kPushFeelerLength = 0xA20C43B7,
                   kPushStrength = 0x4D16F37D, kForwardTolerance = 0x4676A268, kWalkPoint = 0xC1528F7F,
                   kJumpAdjustSpeed = 0x896C8888, kJumpAccelFactor = 0xF085443B,
                   kIgnoreVelocityThreshold = 0x8AFB8788, kSlopLockAngle = 0xDB05E84E,
                   kRotateInPlaceRate = 0xB557804B, kWalkRotateFactor = 0x7B446C98, kWallRotateFactor = 0xE0456382,
                   kWorseTurnFactor = 0x6CB2E5DE, kWorseWorseTurnFactor = 0xA26760F5, kDecelFactor = 0xACFA4E0C,
                   kLowSpeedDecelFactor = 0x0A2AA811, kRunAccelRate = 0x4F47C998, kWalkAccelRate = 0x6590A49B,
                   kRotateUprightDuration = 0xB0675803, kProbeElevation = 0x6DA7F696;
constexpr uint32_t kStand = 0x9B46E749, kRun = 0xAF895B3F;

constexpr float kOne = 1.0f;                     // 0x006453A8
constexpr float kSteepSlope = 0.95f;             // 0x0064BC7C (0.949999988f)
constexpr float kNearlyUpright = 0.999f;         // 0x006467B8 (0.999000013f)
constexpr float kPi = 3.14159274f;               // 0x0064C62C
constexpr float kTwoPi = 6.28318548f;            // 0x0064545C
constexpr float kMinFacingLength = 0.001f;       // 0x006461D8
constexpr float kUprightNudge = 0.01f;           // 0x0064544C
constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kStickScale = 1.17647052f;       // 0x00647B28 (1 / 0.85)
constexpr float kWalkStart = 0.3f;               // 0x006467C0: the stick's push where walking starts
constexpr float kMinMoveSpeed = 0.01f;           // 0x0064544C
constexpr float kNoFlat = 9.99999975e-05f;       // 0x006453D4
constexpr float kDegrees = 57.2957763671875f;    // 0x00645468

// |acos(value)| in degrees (the game's C runtime's _CIacos, 0x00623630), kept in the register until rounded to
// float, as the original.
float AcosDegrees(float value)
{
    float result;
    void* function = Crt::AcosX87();
    float degrees = kDegrees;
    __asm {
        fld value
        call function
        fmul degrees
        fabs
        fstp result
    }
    return result;
}

// a + (b - a) * t.
Maths::Vec4* Lerp(Maths::Vec4* out, const Maths::Vec4* a, const Maths::Vec4* b, float t) { return Walk::Lerp(out, a, b, t); }

double FrameLength(void* walk) { return At<float>(walk, kFrameLength); }

} // namespace

// 0x00526260
void CFootMovement::TurnFacingToVelocity()
{
    const Maths::Vec4& vel = At<Maths::Vec4>(this, kVel);
    float speed = static_cast<float>(
        X87::Sqrt((static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) +
                  static_cast<double>(vel.z) * vel.z));
    if (speed < WalkParameter(kWallTurnSpeedThreshold))
        return;
    Maths::Vec4 direction = vel;
    direction.MakeUnit();
    // The turn this frame: wall_turn_factor a second at max_wall_turn_speed_threshold and above, less from the
    // threshold up.
    double rate;
    if (speed > WalkParameter(kMaxWallTurnSpeedThreshold)) {
        rate = static_cast<double>(WalkParameter(kWallTurnFactor)) * FrameLength(this);
    } else {
        float most = WalkParameter(kMaxWallTurnSpeedThreshold);
        float least = WalkParameter(kWallTurnSpeedThreshold);
        float factor = WalkParameter(kWallTurnFactor);
        rate = (static_cast<double>(speed) - least) / (static_cast<double>(most) - least) *
               (static_cast<double>(factor) * FrameLength(this));
    }
    Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    if (rate >= kOne) {
        facing = direction;
        return;
    }
    // (below 1 - or NaN - the rate rounded to float)
    Maths::Vec4 turned;
    facing = *Lerp(&turned, &facing, &direction, static_cast<float>(rate));
    facing.MakeUnit();
}

// 0x00526430
double CFootMovement::AdjustSpeedForSlope(float speed, const Maths::Vec4* direction)
{
    const Maths::Vec4& normal = At<Maths::Vec4>(this, kFloorUpVector);
    if (normal.y > kSteepSlope)
        return speed;
    if (speed <= WalkParameter(kWalkSpeed))
        return speed;
    // On a steep slope, faster than walking: slowed by how much the direction runs up or down it.
    Maths::Vec4 downhill = {normal.x, 0.0f, normal.z, normal.w};
    downhill.MakeUnit();
    double along = std::fabs((static_cast<double>(downhill.z) * direction->z +
                              static_cast<double>(downhill.y) * direction->y) +
                             static_cast<double>(downhill.x) * direction->x);
    return ((kOne - along) + along * normal.y) * speed;
}

// 0x00527420
void CFootMovement::UpdateUpright()
{
    Maths::Vec4& up = At<Maths::Vec4>(this, kUp);
    if (At<uint32_t>(this, kUp + 4) == 0x3F800000) // (up.y exactly 1)
        return;
    if (up.y > kNearlyUpright) {
        up = {0.0f, 1.0f, 0.0f, 1.0f};
        At<float>(this, kUpright) = 0.0f;
        return;
    }
    if (!(At<float>(this, kUpright) == 0.0f))
        return;
    float rate = WalkParameter(kLerpUprightRate);
    Maths::Vec4 upright = {0.0f, 1.0f, 0.0f, 1.0f};
    Maths::Vec4 eased;
    up = *Lerp(&eased, &up, &upright, static_cast<float>(FrameClock::GetSlomoFrameSeconds() * rate));
    up.MakeUnit();
}

// 0x00527720
void CFootMovement::SetTransitionFacing(const Maths::Vec4* facing, float rate)
{
    const Maths::Vec4& current = At<Maths::Vec4>(this, kFacing);
    double turn = X87::Atan2Difference(facing->x, facing->z, current.x, current.z);
    At<float>(this, kTransitionTurn) = static_cast<float>(turn);
    if (std::fabs(turn) > kPi) // the shorter way round
        At<float>(this, kTransitionTurn) = static_cast<float>(turn - (turn < 0.0 ? -1.0f : kOne) * kTwoPi);
    At<Maths::Vec4>(this, kTransitionFacing) = *facing;
    At<float>(this, kTransitionRate) = rate;
}

// 0x005277B0
void CFootMovement::RefreshHitCache()
{
    float reach = static_cast<float>(static_cast<double>(WalkParameter(kFeelerLength)) + kOne);
    float height = WalkParameter(kWalkerHeight);
    float below = WalkParameter(kSnapDownHeight);
    float above = static_cast<float>(static_cast<double>(height) + kOne);
    const Maths::Vec4& pos = static_cast<CGameEntity*>(m_entity)->m_atPoint;
    Maths::BBox box;
    box.max = {pos.x + reach, pos.y + above, pos.z + reach, pos.w};
    below = static_cast<float>(static_cast<double>(below) + kOne);
    box.min = {pos.x - reach, pos.y - below, pos.z - reach, pos.w};
    Engine3D::CHitMeshCache& cache = At<Engine3D::CHitMeshCache>(this, kHitMeshStore);
    cache.TickFrame(box);
    Engine3D::g_currentCache = &cache; // the feelers' current cache
}

// 0x00527BE0
void CFootMovement::RestoreDisplayOffset()
{
    int state = At<int>(this, kState);
    if (state == 2 || state == 4)
        return;
    float rate = WalkParameter(kDisplayOffsetRestoreRate);
    double step = FrameClock::GetSlomoFrameSeconds() * rate;
    float& offset = At<float>(this, kModelLift);
    offset = static_cast<float>(-static_cast<double>(offset) * step + offset);
}

// 0x00527C50
void CFootMovement::UpdatePush()
{
    if (!At<bool>(this, kPushing))
        return;
    Maths::Vec4& pos = At<Maths::Vec4>(this, kPos);
    if (pos.y > At<float>(this, kPushHeight)) {
        At<bool>(this, kPushing) = false;
        return;
    }
    const Maths::Vec4& normal = At<Maths::Vec4>(this, kPushNormal);
    float distance = static_cast<float>(((static_cast<double>(normal.z) * pos.z + static_cast<double>(normal.y) * pos.y) +
                                         static_cast<double>(normal.x) * pos.x) -
                                        At<float>(this, kPushPlaneDistance));
    if (!(-WalkParameter(kPushFeelerLength) < distance))
        return;
    // Within reach of the plane: pushed away from it, and the velocity into it taken away.
    double push = static_cast<double>(WalkParameter(kPushStrength)) * FrameLength(this);
    double pushX = push * normal.x; // (x's kept unrounded, as the original's register)
    float pushY = static_cast<float>(push * normal.y);
    float pushZ = static_cast<float>(push * normal.z);
    float pushW = static_cast<float>(push * normal.w);
    pos.x = static_cast<float>(pos.x - pushX);
    pos.y = pos.y - pushY;
    pos.z = pos.z - pushZ;
    pos.w = pos.w - pushW;
    Maths::Vec4& vel = At<Maths::Vec4>(this, kVel);
    double into = (static_cast<double>(normal.z) * vel.z + static_cast<double>(normal.y) * vel.y) +
                  static_cast<double>(vel.x) * normal.x;
    if (into == 0.0)
        return;
    vel -= Maths::operator*(static_cast<float>(into), normal);
}

// 0x00527DB0
void CFootMovement::CopyFromObject()
{
    auto* object = static_cast<CGameEntity*>(m_entity);
    At<Maths::Vec4>(this, kPos) = object->m_atPoint;
    Maths::Vec4& vel = At<Maths::Vec4>(this, kVel);
    vel = object->m_vel;
    vel.y = 0.0f;
    At<float>(this, kUpSpeed) = object->m_vel.y;
    Maths::Vec4& up = At<Maths::Vec4>(this, kUp);
    up = object->m_basisFrame.row[1];
    Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    facing = object->m_basisFrame.row[2];
    At<float>(this, kFacingY) = facing.y;
    facing.y = 0.0f;
    double length = X87::Sqrt((static_cast<double>(facing.x) * facing.x + static_cast<double>(facing.y) * facing.y) +
                              static_cast<double>(facing.z) * facing.z);
    if (length < kMinFacingLength) {
        // Facing straight up or down: the way the up row leans, backwards; the up row nudged upright.
        const Maths::Vec4& objectUp = object->m_basisFrame.row[1];
        facing = {-objectUp.x, -objectUp.y, -objectUp.z, -objectUp.w};
        facing.y = 0.0f;
        facing.MakeUnit();
        up.y = static_cast<float>(static_cast<double>(up.y) + kUprightNudge);
        up.MakeUnit();
        return;
    }
    double inverse = kOne / length;
    facing.x = static_cast<float>(inverse * facing.x);
    facing.y = static_cast<float>(inverse * facing.y);
    facing.z = static_cast<float>(inverse * facing.z);
    facing.w = static_cast<float>(inverse * facing.w);
}

// 0x00527F20
void CFootMovement::UpdateControl()
{
    // The camera's flat forward (its at-row backwards), turned by the stick's angle - straight ahead within
    // forward_tolerance degrees.
    const Maths::Vec4& at = At<CGameEntity*>(this, kCamera)->m_basisFrame.row[2];
    auto* pad = reinterpret_cast<CPadSnapshot*>(At<uint8_t*>(this, kInput) + 0x20);
    float forwardX = -at.x;
    float forwardZ = -at.z;
    double length = X87::Sqrt(static_cast<double>(forwardZ) * forwardZ + static_cast<double>(forwardX) * forwardX);
    if (length > 0.0) {
        double inverse = kOne / length;
        forwardX = static_cast<float>(forwardX * inverse);
        forwardZ = static_cast<float>(forwardZ * inverse);
    }
    float angle = pad->m_polar[1].angle;
    if (WalkParameter(kForwardTolerance) * static_cast<double>(kRadiansPerDegree) > std::fabs(angle))
        angle = 0.0f;
    double turnedZ, turnedX;
    X87::SpinBy(angle, forwardZ, forwardX, &turnedZ, &turnedX);
    Maths::Vec4& control = At<Maths::Vec4>(this, kStickHeading);
    control.x = static_cast<float>(turnedX);
    control.z = static_cast<float>(turnedZ);

    float& magnitude = At<float>(this, kStickDeflection);
    bool& full = At<bool>(this, kControlFull);
    if (!(pad->m_stick[2] == 0.0f) || !(pad->m_stick[3] == 0.0f)) {
        // The stick: its push, full from 85 %.
        At<bool>(this, kStickSteers) = true;
        double push = StickLength(pad->m_stick[2], pad->m_stick[3], pad->m_polar[1].angle) *
                      kStickScale;
        magnitude = static_cast<float>(push);
        if (push < kOne || std::isnan(push)) {
            full = false;
        } else {
            magnitude = kOne;
            full = true;
        }
    } else {
        // The d-pad: on the ground without X held, walk_point (the original calls 0x0043DA70 for it); else full.
        At<bool>(this, kStickSteers) = false;
        if (pad->m_polar[1].length == 0.0f) {
            magnitude = 0.0f;
            full = false;
        } else if (At<int>(this, kState) == 0 && !pad->m_posX.m_heldDown) {
            magnitude = WalkParameter(kWalkPoint);
            full = true;
        } else {
            magnitude = kOne;
            full = true;
        }
    }
    if (At<int>(this, kState) == 0 && At<bool>(this, kLockedToHeading))
        control = At<Maths::Vec4>(this, kFacing);
}

// 0x00528120
double CFootMovement::GetControlSpeed()
{
    float magnitude = At<float>(this, kStickDeflection);
    if (magnitude == 0.0f)
        return 0.0;
    float walkPoint = WalkParameter(kWalkPoint);
    if (magnitude <= walkPoint) {
        // Walking: from walk_start's push up to walk_point, up to walk_speed.
        At<bool>(this, kRunning) = false;
        float walkSpeed = WalkParameter(kWalkSpeed);
        return (static_cast<double>(magnitude) - kWalkStart) / (static_cast<double>(walkPoint) - kWalkStart) * walkSpeed;
    }
    // Running: from walk_speed at walk_point up to the run speed at a full push.
    At<bool>(this, kRunning) = true;
    float walkSpeed = WalkParameter(kWalkSpeed);
    float t = static_cast<float>((static_cast<double>(magnitude) - walkPoint) / (static_cast<double>(kOne) - walkPoint));
    double runSpeed = RunSpeed();
    return (runSpeed - walkSpeed) * t + walkSpeed;
}

// 0x00526850
void CFootMovement::SteerJump()
{
    float& delay = At<float>(this, kLeapDuration);
    if (!(delay == 0.0f)) {
        // (still counting down: no steering yet)
        double left = static_cast<double>(delay) - FrameLength(this);
        delay = left < 0.0 ? 0.0f : static_cast<float>(left);
        return;
    }
    // The velocity apart from what the jump carries along unsteered, eased toward what the push asks for: across
    // the jump's direction and along it.
    Maths::Vec4& vel = At<Maths::Vec4>(this, kVel);
    const Maths::Vec4& carried = At<Maths::Vec4>(this, kJumpBaseVel);
    vel = {vel.x - carried.x, vel.y - carried.y, vel.z - carried.z, vel.w - carried.w};
    const Maths::Vec4& dir = At<Maths::Vec4>(this, kLeapHeading);
    const Maths::Vec4& control = At<Maths::Vec4>(this, kStickHeading);
    float magnitude = At<float>(this, kStickDeflection);
    float sideX = -dir.z, sideZ = dir.x; // (the jump's sideways direction: y 0, w 1)
    float sideTarget = static_cast<float>(((static_cast<double>(sideZ) * control.z + static_cast<double>(sideX) * control.x) +
                                           static_cast<double>(control.y) * 0.0) *
                                          WalkParameter(kJumpAdjustSpeed) * magnitude);
    float side = static_cast<float>((static_cast<double>(sideZ) * vel.z + static_cast<double>(sideX) * vel.x) +
                                    static_cast<double>(vel.y) * 0.0);
    side = static_cast<float>((static_cast<double>(sideTarget) - side) *
                                  (static_cast<double>(WalkParameter(kJumpAccelFactor)) * FrameLength(this)) +
                              side);
    float forwardTarget = static_cast<float>(((static_cast<double>(control.z) * dir.z + static_cast<double>(control.y) * dir.y) +
                                              static_cast<double>(control.x) * dir.x) *
                                             WalkParameter(kJumpAdjustSpeed) * magnitude);
    float forward = static_cast<float>((static_cast<double>(vel.z) * dir.z + static_cast<double>(vel.y) * dir.y) +
                                       static_cast<double>(vel.x) * dir.x);
    bool ease;
    if (!At<bool>(this, kJumpUnsteered) && At<float>(this, kStickDeflection) == 0.0f) {
        // no push: along the jump, eased to a stop
        forwardTarget = 0.0f;
        forward = 0.0f;
        ease = true;
    } else {
        if (At<bool>(this, kJumpUnsteered))
            side = 0.0f;
        // Faster ahead than asked: kept. Pushing back while moving ahead: from the speed there.
        ease = !(forwardTarget >= 0.0f && forward > forwardTarget);
        if (ease && forwardTarget < 0.0f && forward > 0.0f)
            forwardTarget = forward + forwardTarget;
    }
    if (ease)
        forward = static_cast<float>((static_cast<double>(forwardTarget) - forward) *
                                         (static_cast<double>(WalkParameter(kJumpAccelFactor)) * FrameLength(this)) +
                                     forward);
    float sideY = side * 0.0f;
    float sideZpart = sideZ * side;
    vel.x = static_cast<float>(static_cast<double>(sideX) * side + static_cast<double>(forward) * dir.x);
    vel.y = forward * dir.y + sideY;
    vel.z = forward * dir.z + sideZpart;
    vel.w = forward * dir.w + side;
    vel = {carried.x + vel.x, carried.y + vel.y, carried.z + vel.z, carried.w + vel.w};
}

// 0x00528950
void CFootMovement::UpdateGroundVelocity(Maths::Vec4* velocity)
{
    float speed = static_cast<float>(std::sqrt(
        (static_cast<double>(velocity->x) * velocity->x + static_cast<double>(velocity->y) * velocity->y) +
        static_cast<double>(velocity->z) * velocity->z));
    double wanted = GetControlSpeed();
    float target = static_cast<float>(wanted);
    At<uint32_t>(this, kAnimState) = wanted <= 0.0 ? kStand : kRun;

    // The way it's going: the velocity's (past ignore_velocity_threshold), else the push's, else the velocity's
    // (past 0.01), else the facing.
    float magnitude = At<float>(this, kStickDeflection);
    Maths::Vec4 direction;
    double directionW;
    if (speed > WalkParameter(kIgnoreVelocityThreshold)) {
        double inverse = kOne / static_cast<double>(speed);
        float inverseF = static_cast<float>(inverse);
        direction = {inverseF * velocity->x, inverseF * velocity->y, inverseF * velocity->z, 0.0f};
        directionW = inverse * velocity->w;
    } else if (!(magnitude == 0.0f)) {
        direction = At<Maths::Vec4>(this, kStickHeading);
        directionW = direction.w;
    } else if (speed > kMinMoveSpeed) {
        direction = Maths::operator/(*velocity, speed);
        directionW = direction.w;
    } else {
        direction = At<Maths::Vec4>(this, kFacing);
        directionW = direction.w;
    }
    direction.w = static_cast<float>(directionW);
    Maths::Vec4 result = direction;

    if (magnitude > 0.0f) {
        const Maths::Vec4& control = At<Maths::Vec4>(this, kStickHeading);
        Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
        // Pushed against the way it's going: no speed asked for.
        if ((static_cast<double>(direction.z) * control.z + static_cast<double>(direction.y) * control.y) +
                static_cast<double>(direction.x) * control.x <=
            0.0)
            target = 0.0f;
        double cosine = (static_cast<double>(direction.z) * facing.z + static_cast<double>(direction.y) * facing.y) +
                        static_cast<double>(direction.x) * facing.x;
        float cosineF = static_cast<float>(cosine);
        float clamped = cosine > kOne ? kOne : (cosineF < -kOne ? -kOne : cosineF);
        if (AcosDegrees(clamped) > WalkParameter(kSlopLockAngle)) {
            // Far off the facing: turned in place at rotate_in_place_rate (walking: x walk_rotate_factor; against a
            // wall, or a wall where it turns to: x wall_rotate_factor), going the pushed way.
            float rate = WalkParameter(kRotateInPlaceRate);
            float turn = static_cast<float>(FrameClock::GetSlomoFrameSeconds() * At<float>(this, kStickDeflection) * rate *
                                            kRadiansPerDegree);
            bool left = static_cast<double>(facing.x) * control.z - static_cast<double>(control.x) * facing.z < 0.0;
            if (!At<bool>(this, kRunning))
                turn = static_cast<float>(static_cast<double>(WalkParameter(kWalkRotateFactor)) * turn);
            if (At<bool>(this, kFlag186C)) {
                turn = static_cast<float>(static_cast<double>(WalkParameter(kWallRotateFactor)) * turn);
            } else {
                double turnedX, turnedZ;
                X87::SpinBy(left ? turn : -turn, facing.x, facing.z, &turnedX, &turnedZ);
                float aheadX = static_cast<float>(turnedX), aheadZ = static_cast<float>(turnedZ);
                Engine3D::CProbeRay feeler;
                feeler.MakeNew();
                Maths::Vec4& start = feeler.m_line.start;
                start = At<Maths::Vec4>(this, kPos);
                start.y = static_cast<float>(static_cast<double>(start.y) + WalkParameter(kProbeElevation));
                float reach = WalkParameter(kFeelerLength);
                float none = reach * 0.0f;
                feeler.m_line.end = {aheadX * reach + start.x, none + start.y, aheadZ * reach + start.z, none + start.w};
                // (the original reads wall_rotate_factor through 0x0043DA70)
                if (feeler.GetHitShape(false, false))
                    turn = static_cast<float>(static_cast<double>(WalkParameter(kWallRotateFactor)) * turn);
            }
            double newX, newZ;
            X87::SpinBy(left ? turn : -turn, facing.x, facing.z, &newX, &newZ);
            facing.z = static_cast<float>(newZ);
            facing.x = static_cast<float>(newX);
            // Turned past the push: onto it.
            bool stillLeft = newX * control.z - static_cast<double>(control.x) * facing.z < 0.0;
            if (left != stillLeft)
                facing = control;
            result = control;
        } else {
            // Near the facing: the facing eased toward the push, the more the further off (worse_turn_factor to
            // worse_worse_turn_factor), going the facing's way.
            double off = kOne - ((static_cast<double>(control.z) * facing.z + static_cast<double>(control.y) * facing.y) +
                                 static_cast<double>(control.x) * facing.x);
            off = off + off;
            float offF = static_cast<float>(off);
            float k = off > kOne ? kOne : (offF < 0.0f ? 0.0f : offF);
            float worse = WalkParameter(kWorseTurnFactor);
            float worst = WalkParameter(kWorseWorseTurnFactor);
            float rate = static_cast<float>(((static_cast<double>(worse) - worst) * k + worst) * At<float>(this, kStickDeflection));
            double step = FrameClock::GetSlomoFrameSeconds() * rate;
            if (step >= kOne) {
                facing = control;
            } else {
                Maths::Vec4 eased;
                facing = *Lerp(&eased, &facing, &control, static_cast<float>(step));
                facing.MakeUnit();
            }
            result = facing;
        }
    }

    // The speed: toward what's asked (as the slope allows) - up at walk_accel_rate / run_accel_rate, down by
    // decel_factor (low_speed_decel_factor stopping from walking speed).
    At<float>(this, kSpeed) = target;
    float allowed = static_cast<float>(AdjustSpeedForSlope(target, &direction));
    double newSpeed;
    if (speed > allowed) {
        float factor = WalkParameter(kDecelFactor);
        if (allowed == 0.0f && !(speed > WalkParameter(kWalkSpeed)))
            factor = WalkParameter(kLowSpeedDecelFactor);
        newSpeed = (static_cast<double>(allowed) - speed) * (FrameClock::GetSlomoFrameSeconds() * factor) + speed;
    } else {
        float accel = WalkParameter(At<bool>(this, kRunning) ? kRunAccelRate : kWalkAccelRate);
        newSpeed = FrameClock::GetSlomoFrameSeconds() * accel + speed;
        if (newSpeed > allowed)
            newSpeed = allowed;
    }
    *velocity = {static_cast<float>(result.x * newSpeed), static_cast<float>(result.y * newSpeed),
                 static_cast<float>(result.z * newSpeed), static_cast<float>(result.w * newSpeed)};
}

// 0x00529800
void CFootMovement::UpdateRotateUpright()
{
    float& left = At<float>(this, kUprightTime);
    if (left == 0.0f)
        return;
    // This frame's share of the turn (+0x730 about +0x734) over rotate_upright_duration.
    float duration = WalkParameter(kRotateUprightDuration);
    float angle = static_cast<float>(FrameLength(this) / duration * At<float>(this, kUprightAngle));
    Maths::Mat4 rotation;
    Maths::BuildAxisAngleMatrix(rotation, At<Maths::Vec4>(this, kUprightAxis), angle);
    auto* object = static_cast<CGameEntity*>(m_entity);
    // D3DXMatrixMultiply (0x005BFA8C)
    D3dx::MatrixMultiply(
        &object->m_basisFrame, &object->m_basisFrame, &rotation);
    object->m_displayMatrix = object->m_basisFrame;
    double remaining = static_cast<double>(left) - FrameLength(this);
    left = remaining < 0.0 ? 0.0f : static_cast<float>(remaining);
}

// 0x005298D0
void CFootMovement::ApplyTransition()
{
    auto* object = static_cast<CGameEntity*>(m_entity);
    const Maths::Vec4& at = At<Maths::Vec4>(this, kTransitionPos);
    const Maths::Vec4& offset = At<Maths::Vec4>(this, kClimbOffset);
    object->m_atPoint = {at.x + offset.x, at.y + offset.y, at.z + offset.z, at.w + offset.w};
    // Upright, facing the transition's facing (row 0's w 1, as the original).
    const Maths::Vec4& facing = At<Maths::Vec4>(this, kTransitionFacing);
    Maths::Mat4 matrix;
    matrix.row[0] = {facing.z, 0.0f, -facing.x, 1.0f};
    matrix.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    matrix.row[2] = facing;
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    object->m_basisFrame = matrix;
    At<float>(this, kModelLift) = At<float>(this, kTransitionDisplayOffset);
    // Then the transition's member function, if any (+0x1948, this adjusted by +0x194C).
    auto callback = At<void(__thiscall*)(void*)>(this, kTransitionCallback);
    if (callback)
        callback(reinterpret_cast<uint8_t*>(this) + At<int>(this, kTransitionCallback + 4));
}

// 0x00529CE0
void CFootMovement::CopyToObject()
{
    // The facing tilted back up by its y (+0x744), keeping it unit.
    float facingY = At<float>(this, kFacingY);
    double flat = kOne - static_cast<double>(facingY) * facingY;
    double k = flat > 0.0 ? std::sqrt(flat) : static_cast<double>(kNoFlat);
    Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    facing = {static_cast<float>(k * facing.x), static_cast<float>(k * facing.y), static_cast<float>(k * facing.z),
              static_cast<float>(k * facing.w)};
    facing.y = facingY;
    // The orientation from the up vector and the facing.
    const Maths::Vec4& up = At<Maths::Vec4>(this, kUp);
    Maths::Mat4 matrix;
    Maths::Vec4& right = matrix.row[0];
    right = {static_cast<float>(static_cast<double>(up.y) * facing.z - static_cast<double>(up.z) * facing.y),
             static_cast<float>(static_cast<double>(up.z) * facing.x - static_cast<double>(up.x) * facing.z),
             static_cast<float>(static_cast<double>(up.x) * facing.y - static_cast<double>(up.y) * facing.x), 0.0f};
    right.MakeUnit();
    matrix.row[1] = up;
    matrix.row[2] = {static_cast<float>(static_cast<double>(up.z) * right.y - static_cast<double>(up.y) * right.z),
                     static_cast<float>(static_cast<double>(up.x) * right.z - static_cast<double>(up.z) * right.x),
                     static_cast<float>(static_cast<double>(up.y) * right.x - static_cast<double>(up.x) * right.y), 0.0f};
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto* object = static_cast<CGameEntity*>(m_entity);
    object->m_atPoint = At<Maths::Vec4>(this, kPos);
    object->m_basisFrame = matrix;
    object->m_displayMatrix = matrix;
    // The object's velocity by state.
    Maths::Vec4& vel = object->m_vel;
    switch (At<int>(this, kState)) {
    case 0: // on the ground, in the air
    case 1:
        vel = At<Maths::Vec4>(this, kVel);
        vel.y = At<float>(this, kUpSpeed);
        break;
    case 2: { // hanging: sideways along the ledge
        float scale = At<float>(this, kClimbScale);
        vel = {scale * -facing.z, scale * 0.0f, scale * facing.x, scale * 1.0f};
        break;
    }
    case 3: // climbing: up at its speed
        vel = {0.0f, At<float>(this, kSpeed), 0.0f, 1.0f};
        break;
    default:
        vel = {0.0f, 0.0f, 0.0f, 1.0f};
        break;
    }
    // The model raised along the up vector by the display offset.
    float offset = At<float>(this, kModelLift);
    At<Maths::Vec4>(At<void*>(this, kModel), 0x1F8) = {matrix.row[1].x * offset, matrix.row[1].y * offset,
                                                       matrix.row[1].z * offset, matrix.row[1].w * offset};
}

} // namespace Obj
