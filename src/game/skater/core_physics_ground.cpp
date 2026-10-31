// The skater core physics component: friction, braking and pushing off, the ground's normal and going up vert
// (0x004FE1F0-0x004FEE0B).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/walk_hang.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/skater.h"
#include "game/ed/park_editor.h"

namespace Skater {

using namespace Physics;

void __cdecl TrackingPrint(int index, const Mth::Vector* a, const Mth::Vector* b);

namespace {

// Physics values, stats and script values (names not recovered unless given).
constexpr uint32_t kSlopeTurnSpeed = 0x62A1FA03;
constexpr uint32_t kSlopeTurnNormal = 0xC3527EF2;
constexpr uint32_t kPushSpeedCrouched = 0x92E0247C;
constexpr uint32_t kPushSpeed = 0x4610C2E3;
constexpr uint32_t kPushCrouched = 0x3D24128E;
constexpr uint32_t kPush = 0x5F9B864D;
constexpr uint32_t kFrictionCrouched = 0xBED96EDA;
constexpr uint32_t kFriction = 0x1A78B6FC;
constexpr uint32_t kNormalBlendRate = 0xD8120182;
constexpr uint32_t kVertPushOut = 0x78384871;
constexpr uint32_t kOff = 0xD443A2BC;               // Off

// The game's constants.
constexpr float kBarelyMovingSquared = 0.00001f;     // 0x006453B8
constexpr float kFramesPerSecond = 60.0f;            // 0x00645EA0
constexpr float kSlow = 50.0f;                       // 0x006463CC
constexpr float kLockedSlow = 0.5f;                  // 0x006453BC
constexpr float kStill = 0.0001f;                    // 0x006453D4
constexpr float kNormalFeeler = 72.0f;               // 0x00646244
constexpr float kFlat = 0.001f;                      // 0x006461D8

// The component's fields used here.
constexpr uint32_t kDrawnNormal = 0x58;
constexpr uint32_t kOldNormal = 0x78;
constexpr uint32_t kNormalBlend = 0x88;
constexpr uint32_t kDisplayUp = 0x28;
constexpr uint32_t kDisplayFacing = 0x38;
constexpr uint32_t kTakeOffPoint = 0x268;
constexpr uint32_t kWallNormal = 0x278;
constexpr uint32_t kVertFacing = 0x288;
constexpr uint32_t kVertTime = 0x298;
constexpr uint32_t kAutoKick = 0x2D9;
constexpr uint32_t kCrouchStat = 0x303;
constexpr uint32_t kNoKick = 0x310;
constexpr uint32_t kCanBrake = 0x311;
constexpr uint32_t kRollingFrictionValue = 0x314;
constexpr uint32_t kLimitsTime = 0x350;
constexpr uint32_t kLimitsFriction = 0x35C;
constexpr uint32_t kLockVelocity = 0x388;

// The state component's flags.
constexpr uint32_t kFlagCrouched = 0x20;
constexpr uint32_t kFlagVertAir = 0x30;
constexpr uint32_t kFlagVertWall = 0x38;
constexpr uint32_t kFlagOnVert = 0x40;
constexpr uint32_t kFlagSpin = 0x68;

// The pad's buttons (offsets into the input component).
constexpr uint32_t kBrakeButton = 0x40;
constexpr uint32_t kLeftButton = 0x60;
constexpr uint32_t kRightButton = 0x80;
constexpr uint32_t kKickButton = 0x180;

bool IsCrouched(void* component) { return FlagsOf(component)[kFlagCrouched] != 0; }
double SpeedOf(const Mth::Vector& v) { return LengthUnrounded(v); }

} // namespace

// 0x004FE1F0
void CorePhysicsComponent::ApplyFriction(float friction)
{
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    float lengthSquared = static_cast<float>(LengthSqrUnrounded(velocity));
    if (lengthSquared < kBarelyMovingSquared)
        return;
    float slowing = static_cast<float>(static_cast<double>(lengthSquared) * At<float>(this, kFrameLength) * friction *
                                       kFramesPerSecond);
    Mth::Vector change = velocity;
    change.SetLength(slowing);
    double changeSquared = (static_cast<double>(change.z) * change.z + static_cast<double>(change.x) * change.x) +
                           static_cast<double>(change.y) * change.y;
    if (changeSquared > lengthSquared) {
        velocity = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    velocity.x -= change.x;
    velocity.y -= change.y;
    velocity.z -= change.z;
    velocity.w -= change.w;
}

// 0x004FE320
bool CorePhysicsComponent::ShouldBrake()
{
    uint8_t* input = InputOf(this);
    if (!At<uint8_t>(this, kAutoKick) && !input[kKickButton] && SpeedOf(VectorAt(ObjectOf(this), kVelocity)) < kSlow)
        return true;
    if (!input[kBrakeButton] || !At<uint8_t>(this, kCanBrake))
        return false;
    if (SpeedOf(VectorAt(ObjectOf(this), kVelocity)) < kSlow)
        return true;
    if (!input[kRightButton] && !input[kLeftButton])
        return true;
    void* object = ObjectOf(this);
    const Mth::Vector& v = VectorAt(object, kVelocity);
    const Mth::Vector& f = VectorAt(object, kForward);
    double along = (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(f.x) * v.x;
    return along < 0.0;
}

// 0x004FE420
bool CorePhysicsComponent::MayTurnOnSlope()
{
    float speed = static_cast<float>(SpeedOf(VectorAt(ObjectOf(this), kVelocity)));
    if (!(Script::GetFloat(kSlopeTurnSpeed) > speed))
        return false;
    return Script::GetFloat(kSlopeTurnNormal) > At<float>(this, kGroundNormal + 4);
}

// 0x004FE490
bool CorePhysicsComponent::CanPushOff()
{
    uint8_t* input = InputOf(this);
    if (At<uint8_t>(this, kNoKick))
        return false;
    if (!At<uint8_t>(this, kAutoKick) && !input[kKickButton])
        return false;
    if (input[kBrakeButton])
        return false;
    float speed = static_cast<float>(SpeedOf(VectorAt(ObjectOf(this), kVelocity)));
    bool crouched = IsCrouched(this) || At<uint8_t>(this, kCrouchStat);
    float limit = static_cast<float>(StatValue(ObjectOf(this), crouched ? kPushSpeedCrouched : kPushSpeed));
    return !(limit < speed);
}

// 0x004FE530
void CorePhysicsComponent::PushOff()
{
    void* object = ObjectOf(this);
    Mth::Vector direction;
    if (At<uint8_t>(this, kLockVelocity)) {
        direction = VectorAt(object, kVelocity);
        const Mth::Vector& v = direction;
        double speed = std::sqrt((static_cast<double>(v.z) * v.z + static_cast<double>(v.y) * v.y) +
                                 static_cast<double>(v.x) * v.x);
        if (speed < kLockedSlow)
            direction = VectorAt(object, kForward);
        else
            direction.Normalize();
    } else {
        direction = VectorAt(object, kForward);
    }
    bool crouched = IsCrouched(this) || At<uint8_t>(this, kCrouchStat);
    float push = static_cast<float>(StatValue(object, crouched ? kPushCrouched : kPush));
    double frame = At<float>(this, kFrameLength);
    float x = static_cast<float>(static_cast<double>(direction.x) * push * frame);
    double y = static_cast<double>(direction.y) * push * frame;
    double z = static_cast<double>(static_cast<float>(static_cast<double>(direction.z) * push)) * frame;
    float w = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(direction.w) * push)) * frame);
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    velocity.x += x;
    velocity.y = static_cast<float>(velocity.y + y);
    velocity.z = static_cast<float>(velocity.z + z);
    velocity.w += w;
}

// 0x004FE6A0
void CorePhysicsComponent::ApplyStanceFriction()
{
    void* object = ObjectOf(this);
    float crouched = PhysicsFloat(kFrictionCrouched, object);
    float standing = PhysicsFloat(kFriction, object);
    if (At<float>(this, kLimitsTime) != 0.0f) {
        crouched = At<float>(this, kLimitsFriction);
        standing = crouched;
    }
    ApplyFriction(IsCrouched(this) ? crouched : standing);
}

// 0x004FE730
void CorePhysicsComponent::ApplyRollingFriction()
{
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    double x = velocity.x;
    double w = velocity.w;
    float vy = velocity.y;
    float vz = velocity.z;
    double speed = std::sqrt((static_cast<double>(vz) * vz + static_cast<double>(vy) * vy) + x * x);
    float speedRounded = static_cast<float>(speed);
    if (speed < kStill) {
        velocity = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    double k = static_cast<double>(At<float>(this, kFrameLength)) / speedRounded * At<float>(this, kRollingFrictionValue) *
               kFramesPerSecond;
    float dx = static_cast<float>(k * x);
    float dy = static_cast<float>(vy * k);
    float dz = static_cast<float>(vz * k);
    double dw = w * k;
    double changeSquared = (static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
    if (static_cast<double>(speedRounded) * speedRounded < changeSquared) {
        velocity = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    velocity.x -= dx;
    velocity.y -= dy;
    velocity.z -= dz;
    velocity.w = static_cast<float>(velocity.w - dw);
}

// 0x004FE860
void CorePhysicsComponent::TurnToNormal(Mth::Vector normal)
{
    uint8_t* settings = reinterpret_cast<uint8_t*>(Ed::g_parkEditor);
    if (At<uint32_t>(settings, 0x40) != kOff && normal.y > 0.0f) {
        const Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
        Mth::Vector end;
        end.x = static_cast<float>(static_cast<float>(normal.x * static_cast<double>(kNormalFeeler)) + position.x);
        end.y = static_cast<float>(static_cast<float>(normal.y * static_cast<double>(kNormalFeeler)) + position.y);
        end.z = static_cast<float>(static_cast<float>(normal.z * static_cast<double>(kNormalFeeler)) + position.z);
        end.w = static_cast<float>(normal.w * static_cast<double>(kNormalFeeler) + position.w);
        Nx::CFeeler feeler;
        feeler.Construct(&position, &end);
        if (feeler.GetCollision(true, false))
            normal = Mth::Vector(0.0f, 1.0f, 0.0f, 1.0f);
    }
    if (VectorAt(this, kGroundNormal) != normal) {
        VectorAt(this, kGroundNormal) = normal;
        VectorAt(this, kOldNormal) = VectorAt(this, kDrawnNormal);
        At<float>(this, kNormalBlend) = 1.0f;
        VectorAt(ObjectOf(this), kUp) = normal;
        At<Mth::Matrix>(ObjectOf(this), kMatrix).OrthoNormalizeAbout(1);
    }
}

// 0x004FEA50
void CorePhysicsComponent::UpdateGroundNormal()
{
    float& blend = At<float>(this, kNormalBlend);
    if (blend != 0.0f) {
        if (VectorAt(this, kOldNormal) == VectorAt(this, kGroundNormal)) {
            blend = 0.0f;
        } else {
            double left = blend - static_cast<double>(Script::GetFloat(kNormalBlendRate)) * At<float>(this, kFrameLength) *
                                      kFramesPerSecond;
            float t = static_cast<float>(left);
            blend = t;
            if (left > 0.0 || left != left) {
                Mth::Vector blended;
                Walk::Lerp(&blended, &VectorAt(this, kGroundNormal), &VectorAt(this, kOldNormal), t);
                VectorAt(this, kDrawnNormal) = blended;
                VectorAt(this, kDrawnNormal).Normalize();
            } else {
                blend = 0.0f;
                VectorAt(this, kDrawnNormal) = VectorAt(this, kGroundNormal);
            }
        }
    }
    if (VectorAt(this, kDisplayUp) != VectorAt(this, kDrawnNormal)) {
        VectorAt(this, kDisplayUp) = VectorAt(this, kDrawnNormal);
        Mth::Matrix& display = At<Mth::Matrix>(this, kDisplayMatrix);
        display.OrthoNormalizeAbout(1);
        display.row[0] = VectorAt(ObjectOf(this), kMatrix);
        VectorAt(this, kDisplayFacing) = VectorAt(ObjectOf(this), kForward);
    }
}

// 0x004FEBA0
void CorePhysicsComponent::StartVertAir()
{
    if (!FlagsOf(this)[kFlagOnVert]) {
        SetTimedFlag(this, kFlagVertAir, 0);
        return;
    }
    void* object = ObjectOf(this);
    TrackingPrint(3, &VectorAt(object, kOldPosition), &VectorAt(object, kPosition));
    Mth::Vector across = VectorAt(this, kGroundNormal);
    VectorAt(this, kWallNormal) = VectorAt(this, kGroundNormal);
    VectorAt(this, kTakeOffPoint) = VectorAt(ObjectOf(this), kOldPosition);
    At<float>(this, kTakeOffPoint + 4) = static_cast<float>(At<float>(this, kTakeOffPoint + 4) - 1.0);
    across.y = 0.0f;
    if (!(std::sqrt(static_cast<double>(across.z) * across.z + static_cast<double>(across.x) * across.x) > kFlat)) {
        SetTimedFlag(this, kFlagVertAir, 0);
        return;
    }
    across.Normalize();
    VectorAt(ObjectOf(this), kVelocity).RotateToPlane(across);
    TurnToNormal(across);
    VectorAt(this, kVertFacing) = VectorAt(ObjectOf(this), kForward);
    At<float>(this, kVertFacing + 4) = -At<float>(this, kVertFacing + 4);
    double out = Script::GetFloat(kVertPushOut);
    Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
    float y = static_cast<float>(across.y * out);
    float z = static_cast<float>(across.z * out);
    float w = static_cast<float>(across.w * out);
    position.x = static_cast<float>(across.x * out + position.x);
    position.y += y;
    position.z += z;
    position.w += w;
    SetTimedFlag(this, kFlagVertAir, 1);
    SetTimedFlag(this, kFlagVertWall, 1);
    SetTimedFlag(this, kFlagSpin, 1);
    At<float>(this, kVertTime) = 6.0f;
}

} // namespace Skater

using Skater::CorePhysicsComponent;
