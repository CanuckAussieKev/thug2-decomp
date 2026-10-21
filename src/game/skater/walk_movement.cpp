// The Walk component's movement (Obj::CWalkComponent, see walk_physics.h): its control, speed, facing, the curb
// float and push-back, the jump's steering, the collision cache. From the disassembly of 0x00526260-0x00529F62.

#include <cmath>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/skater/walk_fields.h"
#include "game/skater/walk_physics.h"
#include "game/skater/x87_math.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

using namespace WalkFields;

// walk_parameters' names.
constexpr uint32_t kWallTurnSpeedThreshold = 0x0515A933, kMaxWallTurnSpeedThreshold = 0xE6C1CD0D,
                   kWallTurnFactor = 0x7A583B9B, kWalkSpeed = 0x79D182AD, kLerpUprightRate = 0x0F22C135,
                   kFeelerLength = 0x99978D2B, kWalkerHeight = 0x9EA1974A, kSnapDownHeight = 0xAF3E4251,
                   kDisplayOffsetRestoreRate = 0xB32C972B, kPushFeelerLength = 0xA20C43B7,
                   kPushStrength = 0x4D16F37D, kForwardTolerance = 0x4676A268, kWalkPoint = 0xC1528F7F;

constexpr float kOne = 1.0f;                     // 0x006453A8
constexpr float kSteepSlope = 0.95f;             // 0x0064BC7C (0.949999988f)
constexpr float kNearlyUpright = 0.999f;         // 0x006467B8 (0.999000013f)
constexpr float kPi = 3.14159274f;               // 0x0064C62C
constexpr float kTwoPi = 6.28318548f;            // 0x0064545C
constexpr float kMinFacingLength = 0.001f;       // 0x006461D8
constexpr float kUprightNudge = 0.01f;           // 0x0064544C
constexpr float kDegreesToRadians = 0.0174532924f; // 0x00645464
constexpr float kStickScale = 1.17647052f;       // 0x00647B28 (1 / 0.85)
constexpr float kWalkStart = 0.3f;               // 0x006467C0: the stick's push where walking starts

// 0x0043D990 (not rewritten yet): a + (b - a) * t.
Mth::Vector* Lerp(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b, float t)
{
    return GameFunction<Mth::Vector*(__cdecl*)(Mth::Vector*, const Mth::Vector*, const Mth::Vector*, float)>(0x0043D990)(
        out, a, b, t);
}

double FrameLength(void* walk) { return At<float>(walk, kFrameLength); }

} // namespace

// 0x00526260
void CWalkComponent::TurnFacingToVelocity()
{
    const Mth::Vector& vel = At<Mth::Vector>(this, kVel);
    float speed = static_cast<float>(
        X87::Sqrt((static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) +
                  static_cast<double>(vel.z) * vel.z));
    if (speed < WalkParameter(kWallTurnSpeedThreshold))
        return;
    Mth::Vector direction = vel;
    direction.Normalize();
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
    Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    if (rate >= kOne) {
        facing = direction;
        return;
    }
    // (below 1 - or NaN - the rate rounded to float)
    Mth::Vector turned;
    facing = *Lerp(&turned, &facing, &direction, static_cast<float>(rate));
    facing.Normalize();
}

// 0x00526430
double CWalkComponent::AdjustSpeedForSlope(float speed, const Mth::Vector* direction)
{
    const Mth::Vector& normal = At<Mth::Vector>(this, kGroundNormal);
    if (normal.y > kSteepSlope)
        return speed;
    if (speed <= WalkParameter(kWalkSpeed))
        return speed;
    // On a steep slope, faster than walking: slowed by how much the direction runs up or down it.
    Mth::Vector downhill = {normal.x, 0.0f, normal.z, normal.w};
    downhill.Normalize();
    double along = std::fabs((static_cast<double>(downhill.z) * direction->z +
                              static_cast<double>(downhill.y) * direction->y) +
                             static_cast<double>(downhill.x) * direction->x);
    return ((kOne - along) + along * normal.y) * speed;
}

// 0x00527420
void CWalkComponent::UpdateUpright()
{
    Mth::Vector& up = At<Mth::Vector>(this, kUp);
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
    Mth::Vector upright = {0.0f, 1.0f, 0.0f, 1.0f};
    Mth::Vector eased;
    up = *Lerp(&eased, &up, &upright, static_cast<float>(Tmr::GetSlomoFrameSeconds() * rate));
    up.Normalize();
}

// 0x00527720
void CWalkComponent::SetTransitionFacing(const Mth::Vector* facing, float rate)
{
    const Mth::Vector& current = At<Mth::Vector>(this, kFacing);
    double turn = X87::Atan2Difference(facing->x, facing->z, current.x, current.z);
    At<float>(this, kTransitionTurn) = static_cast<float>(turn);
    if (std::fabs(turn) > kPi) // the shorter way round
        At<float>(this, kTransitionTurn) = static_cast<float>(turn - (turn < 0.0 ? -1.0f : kOne) * kTwoPi);
    At<Mth::Vector>(this, kTransitionFacing) = *facing;
    At<float>(this, kTransitionRate) = rate;
}

// 0x005277B0
void CWalkComponent::UpdateCollisionCache()
{
    float reach = static_cast<float>(static_cast<double>(WalkParameter(kFeelerLength)) + kOne);
    float height = WalkParameter(kWalkerHeight);
    float below = WalkParameter(kSnapDownHeight);
    float above = static_cast<float>(static_cast<double>(height) + kOne);
    const Mth::Vector& pos = static_cast<CCompositeObject*>(m_object)->m_pos;
    Mth::BBox box;
    box.max = {pos.x + reach, pos.y + above, pos.z + reach, pos.w};
    below = static_cast<float>(static_cast<double>(below) + kOne);
    box.min = {pos.x - reach, pos.y - below, pos.z - reach, pos.w};
    Nx::CCollCache& cache = At<Nx::CCollCache>(this, kCollCache);
    cache.Update(box);
    GameVariable<Nx::CCollCache*>(0x00691974) = &cache; // the feelers' current cache
}

// 0x00527BE0
void CWalkComponent::RestoreDisplayOffset()
{
    int state = At<int>(this, kState);
    if (state == 2 || state == 4)
        return;
    float rate = WalkParameter(kDisplayOffsetRestoreRate);
    double step = Tmr::GetSlomoFrameSeconds() * rate;
    float& offset = At<float>(this, kDisplayOffset);
    offset = static_cast<float>(-static_cast<double>(offset) * step + offset);
}

// 0x00527C50
void CWalkComponent::UpdatePush()
{
    if (!At<bool>(this, kPushing))
        return;
    Mth::Vector& pos = At<Mth::Vector>(this, kPos);
    if (pos.y > At<float>(this, kPushHeight)) {
        At<bool>(this, kPushing) = false;
        return;
    }
    const Mth::Vector& normal = At<Mth::Vector>(this, kPushNormal);
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
    Mth::Vector& vel = At<Mth::Vector>(this, kVel);
    double into = (static_cast<double>(normal.z) * vel.z + static_cast<double>(normal.y) * vel.y) +
                  static_cast<double>(vel.x) * normal.x;
    if (into == 0.0)
        return;
    vel -= Mth::operator*(static_cast<float>(into), normal);
}

// 0x00527DB0
void CWalkComponent::CopyFromObject()
{
    auto* object = static_cast<CCompositeObject*>(m_object);
    At<Mth::Vector>(this, kPos) = object->m_pos;
    Mth::Vector& vel = At<Mth::Vector>(this, kVel);
    vel = object->m_vel;
    vel.y = 0.0f;
    At<float>(this, kVerticalVel) = object->m_vel.y;
    Mth::Vector& up = At<Mth::Vector>(this, kUp);
    up = object->m_matrix.row[1];
    Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    facing = object->m_matrix.row[2];
    At<float>(this, kFacingY) = facing.y;
    facing.y = 0.0f;
    double length = X87::Sqrt((static_cast<double>(facing.x) * facing.x + static_cast<double>(facing.y) * facing.y) +
                              static_cast<double>(facing.z) * facing.z);
    if (length < kMinFacingLength) {
        // Facing straight up or down: the way the up row leans, backwards; the up row nudged upright.
        const Mth::Vector& objectUp = object->m_matrix.row[1];
        facing = {-objectUp.x, -objectUp.y, -objectUp.z, -objectUp.w};
        facing.y = 0.0f;
        facing.Normalize();
        up.y = static_cast<float>(static_cast<double>(up.y) + kUprightNudge);
        up.Normalize();
        return;
    }
    double inverse = kOne / length;
    facing.x = static_cast<float>(inverse * facing.x);
    facing.y = static_cast<float>(inverse * facing.y);
    facing.z = static_cast<float>(inverse * facing.z);
    facing.w = static_cast<float>(inverse * facing.w);
}

// 0x00527F20
void CWalkComponent::UpdateControl()
{
    // The camera's flat forward (its at-row backwards), turned by the stick's angle - straight ahead within
    // forward_tolerance degrees.
    const Mth::Vector& at = At<CCompositeObject*>(this, kCamera)->m_matrix.row[2];
    auto* pad = reinterpret_cast<CControlPad*>(At<uint8_t*>(this, kInput) + 0x20);
    float forwardX = -at.x;
    float forwardZ = -at.z;
    double length = X87::Sqrt(static_cast<double>(forwardZ) * forwardZ + static_cast<double>(forwardX) * forwardX);
    if (length > 0.0) {
        double inverse = kOne / length;
        forwardX = static_cast<float>(forwardX * inverse);
        forwardZ = static_cast<float>(forwardZ * inverse);
    }
    float angle = pad->m_polar[1].angle;
    if (WalkParameter(kForwardTolerance) * static_cast<double>(kDegreesToRadians) > std::fabs(angle))
        angle = 0.0f;
    double turnedZ, turnedX;
    X87::Rotate(angle, forwardZ, forwardX, &turnedZ, &turnedX);
    Mth::Vector& control = At<Mth::Vector>(this, kControlDirection);
    control.x = static_cast<float>(turnedX);
    control.z = static_cast<float>(turnedZ);

    float& magnitude = At<float>(this, kControlMagnitude);
    bool& full = At<bool>(this, kControlFull);
    if (!(pad->m_stick[2] == 0.0f) || !(pad->m_stick[3] == 0.0f)) {
        // The stick: its push, full from 85 %.
        At<bool>(this, kAnalogControl) = true;
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
        At<bool>(this, kAnalogControl) = false;
        if (pad->m_polar[1].length == 0.0f) {
            magnitude = 0.0f;
            full = false;
        } else if (At<int>(this, kState) == 0 && !pad->m_x.m_pressed) {
            magnitude = WalkParameter(kWalkPoint);
            full = true;
        } else {
            magnitude = kOne;
            full = true;
        }
    }
    if (At<int>(this, kState) == 0 && At<bool>(this, kForwardControlLock))
        control = At<Mth::Vector>(this, kFacing);
}

// 0x00528120
double CWalkComponent::GetControlSpeed()
{
    float magnitude = At<float>(this, kControlMagnitude);
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
    double runSpeed = GameFunction<double(__thiscall*)(CWalkComponent*)>(0x0043DAA0)(this);
    return (runSpeed - walkSpeed) * t + walkSpeed;
}

} // namespace Obj

REPLACE(0x00526260, &Obj::CWalkComponent::TurnFacingToVelocity);
REPLACE(0x00526430, &Obj::CWalkComponent::AdjustSpeedForSlope);
REPLACE(0x00527420, &Obj::CWalkComponent::UpdateUpright);
REPLACE(0x00527720, &Obj::CWalkComponent::SetTransitionFacing);
REPLACE(0x005277B0, &Obj::CWalkComponent::UpdateCollisionCache);
REPLACE(0x00527BE0, &Obj::CWalkComponent::RestoreDisplayOffset);
REPLACE(0x00527C50, &Obj::CWalkComponent::UpdatePush);
REPLACE(0x00527DB0, &Obj::CWalkComponent::CopyFromObject);
REPLACE(0x00527F20, &Obj::CWalkComponent::UpdateControl);
REPLACE(0x00528120, &Obj::CWalkComponent::GetControlSpeed);
