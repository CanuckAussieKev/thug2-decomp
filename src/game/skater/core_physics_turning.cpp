// The skater's turns: on the ground, down slopes, off walls, jumping off rails, natas spins, leaning, recovering
// and staying upright in the air, and the acid drop's rotation.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/manual.h"
#include "game/skater/skater.h"
#include "game/skater/thaw_bmx.h"
#include "game/skater/balance_trick.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kRailJumpAngle = 0x38505EF3;        // Rail_Jump_Angle (degrees)
constexpr uint32_t kGroundRotation = 0x374E056B;       // Physics_Ground_Rotation
constexpr uint32_t kGroundSharpRotation = 0x7933A8EF;  // Physics_Ground_Sharp_Rotation
constexpr uint32_t kWallBounceMultiplier = 0xE07EE1A9; // Wall_Bounce_Angle_Multiplier
constexpr uint32_t kSlowTurnOnSlopes = 0x7DD5678B;     // Skater_Slow_Turn_on_slopes
constexpr uint32_t kPerfectRail = 0xCD09E062;          // CHEAT_PERFECT_RAIL
constexpr uint32_t kNatasSpinFactor = 0x6A94598A;      // natas_spin_factor
constexpr uint32_t kSpinCountSlop = 0x50C5CC2F;        // spin_count_slop
constexpr uint32_t kAirLeanStat = 0xCBFDD841;          // Physics_Air_Lean_stat
constexpr uint32_t kAirNoLeanTime = 0x5F70EF46;        // Physics_Air_No_Lean_Time
constexpr uint32_t kAirRampLeanTime = 0x12CD2D14;      // Physics_Air_Ramp_Lean_Time
constexpr uint32_t kRecoverRateStat = 0xE7116A96;      // Physics_recover_rate_stat
constexpr uint32_t kUprightSidewaysSpeed = 0xABD57877; // skater_upright_sideways_speed (degrees a second)

constexpr uint32_t kSpinningLeft = 0x3A5;     // (true for a positive turn)
constexpr uint32_t kTurnVelocityToo = 0x388;  // (0: the velocity turns with the skater on the ground)
constexpr uint32_t kNatasCounterClockwise = 0x1500;
constexpr uint32_t kNatasSpinTime = 0x1504;
constexpr uint32_t kBalance = 0x1524;         // (+0xD4: a balance meter; +0x104, +0x108)
constexpr uint32_t kTrickRotation = 0x13A4;   // (in the skater component: the trick's rotation total, degrees)
constexpr uint32_t kRotationDisplay = 0x1520; // [+0x18]: what shows the rotation total
constexpr uint32_t kAcidDropSlerp = 0x3F0;    // (a SlerpInterpolator)
constexpr uint32_t kAcidDropTime = 0x488;
constexpr uint32_t kAcidDropDuration = 0x48C;
constexpr uint32_t kAcidDropMatrix = 0x4A0;   // the slerp's last matrix

constexpr float kDegreesToRadians = 0.0174532924f;  // 0x00645464
constexpr float kMinusDegreesToRadians = -0.0174532924f; // 0x0064BCCC
constexpr float kRadiansToDegrees = 57.2957764f;    // 0x00645468
constexpr float kSlopeTurnSpeed = 0.001f;           // 0x006461D8
constexpr float kSlowGroundTurnSpeed = 10.0f;       // 0x00646710
constexpr float kSlowGroundTurnRate = 0.00166666671f; // 0x0064BD38 (1 / 600)
constexpr float kHalfPi = 1.57079637f;              // 0x00645484
constexpr float kPi = 3.14159274f;                  // 0x0064BB7C
constexpr float kNatasSpinScale = 4000.0f;          // 0x0064B8F8
constexpr float kNatasSpinBase = 5.0f;              // 0x006469AC
constexpr float kAcidDropEase = 1.5f;               // 0x00646744
constexpr float kAcidDropEaseScale = -2.0f;         // 0x00646970
constexpr float kRecoverFeelerLength = 500.0f;      // 0x0064BC80
constexpr float kRecoverSteepest = 0.2f;            // 0x006473AC
constexpr float kRecoverUpright = 0.9f;             // 0x00649FE4
constexpr float kUprightMaxTilt = 0.95f;            // 0x0064BC7C
constexpr float kUprightRight = 1.2f;               // 0x0064BD40
constexpr float kUprightLeft = -1.2f;               // 0x0064BD3C

// The velocity turned by `angle` about the y axis, as the original's inline x87 code.
void TurnVelocity(Mth::Vector& velocity, double angle)
{
    double s = X87Sin(angle), c = X87Cos(angle);
    double x = velocity.x, z = velocity.z;
    velocity.x = static_cast<float>(c * x + s * z);
    velocity.z = static_cast<float>(c * z + -(x * s));
}

// The skater's matrix and its display matrix turned by `angle` about their own y axes (each a CreateRotateYMatrix and
// a D3DXMatrixMultiply in the original).
void TurnMatrices(void* component, float angle)
{
    At<Mth::Matrix>(ObjectOf(component), kMatrix).RotateYLocal(angle);
    At<Mth::Matrix>(component, kDisplayMatrix).RotateYLocal(angle);
}

} // namespace

// 0x005002D0
void CorePhysicsComponent::TurnForRailJump()
{
    uint8_t* input = InputOf(this);
    float angle;
    if (ThawBmx::Active(this)) {
        // Not original: THAW 0x005CF010 - on the bike the stick's left / right (THAW's LEFTLEFT / LEFTRIGHT).
        const ThawBmx::BikeState* bike = ThawBmx::StateOf(this, false);
        if (bike && bike->stickLeft.down)
            angle = static_cast<float>(static_cast<double>(Script::GetFloat(kRailJumpAngle)) * kDegreesToRadians);
        else if (bike && bike->stickRight.down)
            angle = static_cast<float>(static_cast<double>(Script::GetFloat(kRailJumpAngle)) * kMinusDegreesToRadians);
        else
            return;
    } else if (input[0x60])
        angle = static_cast<float>(static_cast<double>(Script::GetFloat(kRailJumpAngle)) * kDegreesToRadians);
    else if (input[0x80])
        angle = static_cast<float>(static_cast<double>(Script::GetFloat(kRailJumpAngle)) * kMinusDegreesToRadians);
    else
        return;
    TurnVelocity(VectorAt(ObjectOf(this), kVelocity), angle);
    TurnMatrices(this, angle);
}

// 0x00503560
void CorePhysicsComponent::TurnOnGround()
{
    if (ThawBmx::Active(this)) { // (not original: the bike's own turning, THAW 0x005D61F0)
        ThawBmx::TurnOnGround(this);
        return;
    }
    uint8_t* pad = InputOf(this) + 0x20;
    float speed = static_cast<float>(LengthUnrounded(VectorAt(ObjectOf(this), kVelocity)));
    void* object = ObjectOf(this);
    // Left / right (pad +0x40 / +0x60); with the sharp turn button (+0x20) harder - and, slower than 10, the turn
    // ramps up over the first 600 ms the button is held.
    uint8_t* button;
    bool sharp;
    double rate;
    if (pad[0x40]) {
        button = pad + 0x40;
        sharp = pad[0x20] != 0;
        rate = PhysicsFloat(sharp ? kGroundSharpRotation : kGroundRotation, object);
    } else if (pad[0x60]) {
        button = pad + 0x60;
        sharp = pad[0x20] != 0;
        rate = -static_cast<double>(PhysicsFloat(sharp ? kGroundSharpRotation : kGroundRotation, object));
    } else {
        return;
    }
    if (sharp) {
        float full = static_cast<float>(rate);
        if (speed < kSlowGroundTurnSpeed) {
            int held = TimeSincePress(button);
            rate = held < 600 ? static_cast<double>(held) * full * kSlowGroundTurnRate : full;
        }
    }
    if (rate == 0.0)
        return;
    double step = rate * At<float>(this, kFrameLength);
    float angle = static_cast<float>(step);
    At<uint8_t>(this, kSpinningLeft) = step > 0.0 ? 1 : 0;
    if (!At<uint8_t>(this, kTurnVelocityToo))
        TurnVelocity(VectorAt(ObjectOf(this), kVelocity), angle);
    TurnMatrices(this, angle);
}

// 0x005039F0
float CorePhysicsComponent::BounceOffWall(const Mth::Vector* normal, float* turn, float scale)
{
    // Not original: THAW 0x005CCBF0 (the on-bike flag tested first, 0x005CCBF6) - on the bike rolling backwards into a
    // wall: stopped, no bounce (`turn` left as it is).
    if (ThawBmx::Active(this) && ThawBmx::RollingBackwards(this)) {
        VectorAt(ObjectOf(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        return 0.0f;
    }
    const Mth::Vector& side = VectorAt(ObjectOf(this), kMatrix);
    float cosine = static_cast<float>(static_cast<double>(side.z) * normal->z + static_cast<double>(side.y) * normal->y +
                                      static_cast<double>(normal->x) * side.x);
    if (cosine > 1.0f)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    double unrounded = std::acos(static_cast<double>(cosine)); // (the C runtime's, 0x00623630)
    float angle = static_cast<float>(unrounded);
    if (unrounded > kHalfPi)
        angle = angle - kPi;
    double amount = static_cast<double>(Script::GetFloat(kWallBounceMultiplier)) * angle * scale;
    *turn = static_cast<float>(amount);
    TurnVelocity(VectorAt(ObjectOf(this), kVelocity), amount); // (by the unrounded turn)
    TurnMatrices(this, *turn);
    return angle;
}

// 0x00501B00
bool CorePhysicsComponent::TurnDownSlope()
{
    if (!MayTurnOnSlope())
        return false;
    void* object = ObjectOf(this);
    // Along the velocity, or the facing when hardly moving: turned towards straight down the slope.
    Mth::Vector heading = LengthUnrounded(VectorAt(object, kVelocity)) < kSlopeTurnSpeed ? VectorAt(object, kForward)
                                                                                       : VectorAt(object, kVelocity);
    Mth::Vector down = {0.0f, -1.0f, 0.0f, 1.0f};
    down.ProjectToPlane(VectorAt(this, kGroundNormal));
    object = ObjectOf(this);
    double off = Mth::GetAngleAbout(heading, down, VectorAt(object, kUp));
    float angle = static_cast<float>(off);
    float sign = off < 0.0 ? -1.0f : 1.0f;
    double turn = static_cast<double>(PhysicsFloat(kSlowTurnOnSlopes, object)) * At<float>(this, kFrameLength) * sign;
    float step = static_cast<float>(turn);
    if (std::fabs(static_cast<double>(angle)) < std::fabs(turn))
        step = angle;
    TurnMatrices(this, step);
    return true;
}

// 0x00504730
void CorePhysicsComponent::NatasSpin()
{
    auto* career = At<Obj::CSkaterCareer*>(static_cast<void*>(Mdl::g_skateModule), 0x20);
    if (career->GetCheat(kPerfectRail)) {
        // (the perfect rail cheat: the balance held)
        career = At<Obj::CSkaterCareer*>(static_cast<void*>(Mdl::g_skateModule), 0x20);
        career->SetGlobalFlag(Script::GetInteger(kPerfectRail));
        At<uint32_t>(At<void*>(this, kBalance), 0x104) = 0;
        At<uint32_t>(At<void*>(this, kBalance), 0x108) = 0;
        GameVariable<uint8_t>(0x007CE4FD) = 1;
    }
    At<Obj::CBalance>(At<void*>(this, kBalance), 0xD4).Update();
    float factor = Script::GetFloat(kNatasSpinFactor);
    // (read as a double - the unrounded x87 result: GetLean 0x004F8F20 isn't called as a float function)
    double balance = std::fabs(GameFunction<double(__thiscall*)(void*)>(0x004F8F20)(At<void*>(this, kBalance)));
    // The spin slows the further off balance.
    double rate = factor - balance / (kNatasSpinScale / static_cast<double>(factor)) + kNatasSpinBase;
    if (At<uint8_t>(this, kNatasCounterClockwise))
        rate = -rate;
    At<uint8_t>(this, kSpinningLeft) = rate > 0.0 ? 1 : 0;
    float step = static_cast<float>(rate * At<float>(this, kFrameLength));
    TurnMatrices(this, step);
    float& total = At<float>(At<void*>(this, kSkaterComponent), kTrickRotation);
    total = static_cast<float>(static_cast<double>(step) * kRadiansToDegrees + total);
    // Shown as an integer - but (as Spin) not while short of a whole turn by more than the slop.
    uint8_t* flags = FlagsOf(this);
    bool show = true;
    if (flags[0x30] && !flags[0x78]) {
        float size = std::fabs(total);
        double snap = 360.0f - static_cast<double>(PhysicsFloat(kSpinCountSlop, ObjectOf(this))) + 0.1f;
        show = snap <= size;
    }
    if (show) {
        int shown = static_cast<int>(Ftol(At<float>(At<void*>(this, kSkaterComponent), kTrickRotation)));
        void* display = At<void*>(At<void*>(this, kRotationDisplay), 0x18);
        static_cast<Mdl::Score*>(display)->UpdateSpin(shown);
    }
    At<int>(this, kNatasSpinTime) = Now();
}

// 0x00504910
void CorePhysicsComponent::LeanInAir()
{
    uint8_t* flags = FlagsOf(this);
    if (flags[0x78] || flags[0xC0] || flags[0x30])
        return;
    uint8_t* pad = InputOf(this) + 0x20;
    if (pad[0x140] || pad[0x160])
        return;
    float lean = 0.0f;
    int held = 0;
    if (pad[0]) {
        lean = static_cast<float>(StatValue(ObjectOf(this), kAirLeanStat));
        held = TimeSincePress(pad);
    }
    if (pad[0x20]) {
        lean = static_cast<float>(-StatValue(ObjectOf(this), kAirLeanStat));
        held = TimeSincePress(pad + 0x20);
    }
    // None for a while after the button is pressed, then ramping up to full.
    float start = Script::GetFloat(kAirNoLeanTime);
    float end = Script::GetFloat(kAirRampLeanTime);
    float time = static_cast<float>(held);
    if (static_cast<double>(held) < start || static_cast<double>(held) == start)
        return;
    if (time < end)
        lean = static_cast<float>((static_cast<double>(time) - start) * lean / (static_cast<double>(end) - start));
    At<Mth::Matrix>(ObjectOf(this), kMatrix).RotateXLocal(lean * At<float>(this, kFrameLength));
    At<Mth::Matrix>(this, kDisplayMatrix).RotateXLocal(lean * At<float>(this, kFrameLength));
}

// 0x00504A60
void CorePhysicsComponent::RotateInAcidDrop()
{
    if (!FlagsOf(this)[0x78])
        return;
    double elapsed = static_cast<double>(At<float>(this, kFrameLength)) + At<float>(this, kAcidDropTime);
    At<float>(this, kAcidDropTime) = static_cast<float>(elapsed);
    double t = elapsed / At<float>(this, kAcidDropDuration);
    if (t > 1.0)
        t = 1.0f;
    // Eased: 3t^2 - 2t^3.
    float eased = static_cast<float>((t - kAcidDropEase) * t * t * kAcidDropEaseScale);
    Mth::Matrix slerped;
    At<Mth::SlerpInterpolator>(this, kAcidDropSlerp).GetMatrix(slerped, eased);
    // The change since the last frame's matrix applied to the skater's.
    Mth::Matrix last = At<Mth::Matrix>(this, kAcidDropMatrix);
    last.InvertOrthonormal();
    Mth::Matrix change;
    Mth::Multiply(change, last, slerped);
    Mth::Matrix& matrix = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    Mth::Multiply(matrix, matrix, change);
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    At<Mth::Matrix>(this, kAcidDropMatrix) = slerped;
}

// 0x00504B40
void CorePhysicsComponent::RecoverInAir()
{
    if (FlagsOf(this)[0x78])
        return;
    if (VectorAt(ObjectOf(this), kVelocity).y > 0.0f && At<void*>(At<void*>(this, kRide), 0x18))
        return;
    // Ground (not vert, not too steep) within 500 below.
    Nx::CFeeler feeler;
    feeler.Construct();
    feeler.m_line.start = VectorAt(ObjectOf(this), kPosition);
    feeler.m_line.end = VectorAt(ObjectOf(this), kPosition);
    feeler.m_line.end.y = feeler.m_line.end.y - kRecoverFeelerLength;
    if (!feeler.GetCollision(true, false))
        return;
    if (feeler.m_hit.faceFlags & 8)
        return;
    if (feeler.m_normal.y < kRecoverSteepest)
        return;
    if (FlagsOf(this)[0x30]) {
        SetTimedFlag(this, 0x30, 0);
        At<uint8_t>(this, 0x2D0) = 0;
    }
    SetTimedFlag(this, 0x80, 1);
    void* object = ObjectOf(this);
    if (VectorAt(object, kUp).y > kRecoverUpright)
        return;
    // Tipped over: turned back upright about the horizontal axis across the up vector.
    float rate = static_cast<float>(StatValue(object, kRecoverRateStat));
    object = ObjectOf(this);
    const Mth::Vector& up = VectorAt(object, kUp);
    Mth::Vector flat = {up.x, 0.0f, up.z, up.w};
    flat.Normalize();
    Mth::Vector axis = {-flat.z, 0.0f, flat.x, 0.0f};
    At<Mth::Matrix>(object, kMatrix).Rotate(axis, rate * At<float>(this, kFrameLength));
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
}

// 0x00504DF0
void CorePhysicsComponent::KeepUpright()
{
    void* object = ObjectOf(this);
    const Mth::Vector& forward = VectorAt(object, kForward);
    const Mth::Vector& up = VectorAt(object, kUp);
    if (std::fabs(forward.y) > kUprightMaxTilt)
        return;
    // Leaning sideways (the up vector across the facing): rolled back at skater_upright_sideways_speed.
    double sideways = static_cast<double>(forward.x) * up.z + -static_cast<double>(forward.z) * up.x +
                      static_cast<double>(up.y) * 0.0f;
    double angle;
    if (sideways > static_cast<double>(At<float>(this, kFrameLength)) * kUprightRight) {
        angle = static_cast<double>(Script::GetFloat(kUprightSidewaysSpeed)) * kDegreesToRadians *
                At<float>(this, kFrameLength);
    } else if (sideways < static_cast<double>(At<float>(this, kFrameLength)) * kUprightLeft) {
        angle = -(static_cast<double>(Script::GetFloat(kUprightSidewaysSpeed)) * kDegreesToRadians *
                  At<float>(this, kFrameLength));
    } else {
        return;
    }
    At<Mth::Matrix>(ObjectOf(this), kMatrix).RotateZLocal(static_cast<float>(angle));
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
}

} // namespace Skater

REPLACE(0x005002D0, &Skater::CorePhysicsComponent::TurnForRailJump);
REPLACE(0x00503560, &Skater::CorePhysicsComponent::TurnOnGround);
REPLACE(0x005039F0, &Skater::CorePhysicsComponent::BounceOffWall);
REPLACE(0x00501B00, &Skater::CorePhysicsComponent::TurnDownSlope);
REPLACE(0x00504730, &Skater::CorePhysicsComponent::NatasSpin);
REPLACE(0x00504910, &Skater::CorePhysicsComponent::LeanInAir);
REPLACE(0x00504A60, &Skater::CorePhysicsComponent::RotateInAcidDrop);
REPLACE(0x00504B40, &Skater::CorePhysicsComponent::RecoverInAir);
REPLACE(0x00504DF0, &Skater::CorePhysicsComponent::KeepUpright);
