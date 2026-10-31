// The skater core physics component's script member functions (its table at 0x006826C8): the flags and values
// scripts set and ask about, and a few small actions (0x004FBE80-0x004FDD47).

#include <cmath>
#include <cstdint>

#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/rider.h"
#include "game/skate/skate_module.h"

namespace Skater {

uint32_t g_coreMemberFunctions[660]{
    0x584CF9E9, 0x5BD63B29, 0, 0, 0, 0,
    0x0C7A712C, 0x5BD63B29, 0, 0, 0, 0,
    0x8F66B80B, 0x5BD63B29, 0, 0, 0, 0,
    0x4ADC6C2A, 0x5BD63B29, 0, 0, 0, 0,
    0x262D42D5, 0x5BD63B29, 0, 0, 0, 0,
    0xCA6B3809, 0x5BD63B29, 0, 0, 0, 0,
    0x9C6A7E41, 0x5BD63B29, 0, 0, 0, 0,
    0x32DCC9CF, 0x5BD63B29, 0, 0, 0, 0,
    0x5E8F9E77, 0x5BD63B29, 0, 0, 0, 0,
    0x0C890A84, 0x5BD63B29, 0, 0, 0, 0,
    0xBBF2B570, 0x5BD63B29, 0, 0, 0, 0,
    0xDE583E34, 0x5BD63B29, 0, 0, 0, 0,
    0x1996B797, 0x5BD63B29, 0, 0, 0, 0,
    0x1F8BBD05, 0x5BD63B29, 0, 0, 0, 0,
    0xEBBCF455, 0x5BD63B29, 0, 0, 0, 0,
    0xBC19E291, 0x5BD63B29, 0, 0, 0, 0,
    0x02F66333, 0x5BD63B29, 0, 0, 0, 0,
    0x68BF6C13, 0x5BD63B29, 0, 0, 0, 0,
    0xE8DEB0D7, 0x5BD63B29, 0, 0, 0, 0,
    0x34DCFC97, 0x5BD63B29, 0, 0, 0, 0,
    0x0257947E, 0x5BD63B29, 0, 0, 0, 0,
    0x01BAA1D9, 0x5BD63B29, 0, 0, 0, 0,
    0x9FCFDFEB, 0x5BD63B29, 0, 0, 0, 0,
    0xFEFF11CC, 0x5BD63B29, 0, 0, 0, 0,
    0xFE243FEF, 0x5BD63B29, 0, 0, 0, 0,
    0xE168CFE0, 0x5BD63B29, 0, 0, 0, 0,
    0x29FBA9DF, 0x5BD63B29, 0, 0, 0, 0,
    0x17846595, 0x5BD63B29, 0, 0, 0, 0,
    0x259CB90D, 0x5BD63B29, 0, 0, 0, 0,
    0x1D1FD4F0, 0x5BD63B29, 0, 0, 0, 0,
    0x383B939B, 0x5BD63B29, 0, 0, 0, 0,
    0x54EF2B79, 0x5BD63B29, 0, 0, 0, 0,
    0x0E2998B9, 0x5BD63B29, 0, 0, 0, 0,
    0x6FC5AAE0, 0x5BD63B29, 0, 0, 0, 0,
    0xBD4303F4, 0x5BD63B29, 0, 0, 0, 0,
    0xA901A50C, 0x5BD63B29, 0, 0, 0, 0,
    0xE0DF1F0A, 0x5BD63B29, 0, 0, 0, 0,
    0x8C3D7864, 0x5BD63B29, 0, 0, 0, 0,
    0x0E1D5168, 0x5BD63B29, 0, 0, 0, 0,
    0x948EBF96, 0x5BD63B29, 0, 0, 0, 0,
    0x947FCCF4, 0x5BD63B29, 0, 0, 0, 0,
    0xACB82C02, 0x5BD63B29, 0, 0, 0, 0,
    0x510F983B, 0x5BD63B29, 0, 0, 0, 0,
    0x71B993B7, 0x5BD63B29, 0, 0, 0, 0,
    0xBE371B7B, 0x5BD63B29, 0, 0, 0, 0,
    0x55934543, 0x5BD63B29, 0, 0, 0, 0,
    0xB424244D, 0x5BD63B29, 0, 0, 0, 0,
    0x224CDDC9, 0x5BD63B29, 0, 0, 0, 0,
    0x6C8A1316, 0x5BD63B29, 0, 0, 0, 0,
    0x448E3630, 0x5BD63B29, 0, 0, 0, 0,
    0xF37CE040, 0x5BD63B29, 0, 0, 0, 0,
    0x133E1293, 0x5BD63B29, 0, 0, 0, 0,
    0x0F922431, 0x5BD63B29, 0, 0, 0, 0,
    0xDFB8F052, 0x5BD63B29, 0, 0, 0, 0,
    0xC5B4B390, 0x5BD63B29, 0, 0, 0, 0,
    0xB9EEBFF6, 0x5BD63B29, 0, 0, 0, 0,
    0xEC681A59, 0x5BD63B29, 0, 0, 0, 0,
    0x065A559A, 0x5BD63B29, 0, 0, 0, 0,
    0x461E5B92, 0x5BD63B29, 0, 0, 0, 0,
    0xA14A467F, 0x5BD63B29, 0, 0, 0, 0,
    0x90F1D8D5, 0x5BD63B29, 0, 0, 0, 0,
    0x5E8D9B80, 0x5BD63B29, 0, 0, 0, 0,
    0xB94BC0C6, 0x5BD63B29, 0, 0, 0, 0,
    0xE47FF4B5, 0x5BD63B29, 0, 0, 0, 0,
    0x05144783, 0x5BD63B29, 0, 0, 0, 0,
    0xF7E21884, 0x5BD63B29, 0, 0, 0, 0,
    0x0BFF3271, 0x5BD63B29, 0, 0, 0, 0,
    0x2A547E37, 0x5BD63B29, 0, 0, 0, 0,
    0xAAE7E1D8, 0x5BD63B29, 0, 0, 0, 0,
    0xA038260D, 0x5BD63B29, 0, 0, 0, 0,
    0xD27F89E8, 0x5BD63B29, 0, 0, 0, 0,
    0x25B393E7, 0x5BD63B29, 0, 0, 0, 0,
    0xB6222B3A, 0x5BD63B29, 0, 0, 0, 0,
    0x9D8BDE21, 0x5BD63B29, 0, 0, 0, 0,
    0x83A46968, 0x5BD63B29, 0, 0, 0, 0,
    0x6A0834C5, 0x5BD63B29, 0, 0, 0, 0,
    0x675CF35E, 0x5BD63B29, 0, 0, 0, 0,
    0xD3E1A093, 0x5BD63B29, 0, 0, 0, 0,
    0x3BAAC2C5, 0x5BD63B29, 0, 0, 0, 0,
    0xB195C726, 0x5BD63B29, 0, 0, 0, 0,
    0xB649958F, 0x5BD63B29, 0, 0, 0, 0,
    0x11B99941, 0x5BD63B29, 0, 0, 0, 0,
    0xEFFC54DC, 0x5BD63B29, 0, 0, 0, 0,
    0xE3822CCE, 0x5BD63B29, 0, 0, 0, 0,
    0xD5BCAB93, 0x5BD63B29, 0, 0, 0, 0,
    0x9AF4A4C2, 0x5BD63B29, 0, 0, 0, 0,
    0x8A8EBE72, 0x5BD63B29, 0, 0, 0, 0,
    0xC5623AEE, 0x5BD63B29, 0, 0, 0, 0,
    0xD7ECF37C, 0x5BD63B29, 0, 0, 0, 0,
    0x43A7516D, 0x5BD63B29, 0, 0, 0, 0,
    0x5F48F4D1, 0x5BD63B29, 0, 0, 0, 0,
    0x5F43DDF3, 0x5BD63B29, 0, 0, 0, 0,
    0x68F6DB80, 0x5BD63B29, 0, 0, 0, 0,
    0x1B4A5FF9, 0x5BD63B29, 0, 0, 0, 0,
    0x007CDADB, 0x5BD63B29, 0, 0, 0, 0,
    0x27C92E33, 0x5BD63B29, 0, 0, 0, 0,
    0x8476CA5B, 0x5BD63B29, 0, 0, 0, 0,
    0xF900547B, 0x5BD63B29, 0, 0, 0, 0,
    0xFC9E2886, 0x5BD63B29, 0, 0, 0, 0,
    0x3981FBF0, 0x5BD63B29, 0, 0, 0, 0,
    0x821BF006, 0x5BD63B29, 0, 0, 0, 0,
    0x2C30B07A, 0x5BD63B29, 0, 0, 0, 0,
    0x1EB4121A, 0x5BD63B29, 0, 0, 0, 0,
    0xA751E3D8, 0x5BD63B29, 0, 0, 0, 0,
    0x03B9946F, 0x5BD63B29, 0, 0, 0, 0,
    0xADCA7EE6, 0x5BD63B29, 0, 0, 0, 0,
    0x0898E60F, 0x5BD63B29, 0, 0, 0, 0,
    0xB17D17CD, 0x5BD63B29, 0, 0, 0, 0,
    0xF22B4480, 0x5BD63B29, 0, 0, 0, 0,
    0, 0x5BD63B29, 0, 0, 0, 0};

using namespace Physics;

namespace {

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;    // SkaterCorePhysics (the component type)

// Checksums.
constexpr uint32_t kOn = 0xF649D637;                   // on
constexpr uint32_t kRadius = 0xC48391A5;               // radius
constexpr uint32_t kSpeed = 0xF0D90109;                // speed
constexpr uint32_t kExtraPushThird = 0xDFDFEAB8;       // (name not recovered)
constexpr uint32_t kAirtime = 0xAD6BCDB4;              // (GetAirtime's result; name not recovered)
constexpr uint32_t kSeconds = 0xD029F619;              // seconds
constexpr uint32_t kSecond = 0x49E0EE96;               // second
constexpr uint32_t kFrames = 0x019176C5;               // frames
constexpr uint32_t kFrame = 0x4A07C332;                // frame
constexpr uint32_t kCarPlantUpwardBoost = 0xCB49B3F2;  // CarPlant_Upward_boost
constexpr uint32_t kCarPlantForwardBoost = 0xB7422173; // CarPlant_Forward_boost
constexpr uint32_t kSlide = 0x8D10119D;                // slide
constexpr uint32_t kDefault = 0x1CA1FF20;              // Default
constexpr uint32_t kRollingFriction = 0x78F80EC4;      // Physics_Rolling_Friction
constexpr uint32_t kBackside = 0x47953F00;             // backside
constexpr uint32_t kFrontside = 0x996D5512;            // Frontside
constexpr uint32_t kCancelLimits = 0xC6F342D9;         // (overridelimits' first flag; name not recovered)
constexpr uint32_t kEnd = 0xFF03CC4E;                  // end
constexpr uint32_t kMax = 0x6289DD76;                  // max
constexpr uint32_t kMaxMax = 0x73A19E3A;               // (overridelimits' second limit; name not recovered)
constexpr uint32_t kTime = 0x906B67BA;                 // time
constexpr uint32_t kFriction = 0xEDF3E7F4;             // Friction
constexpr uint32_t kGroundGravity = 0x6618A713;        // Physics_Ground_Gravity
constexpr uint32_t kGravity = 0xA5E2DA58;              // gravity
constexpr uint32_t kUntilCancelled = 0xDF312928;       // (overridelimits: time -1; name not recovered)
constexpr uint32_t kCurrentLevel = 0xF7832A91;         // CurrentLevel
constexpr uint32_t kDuration = 0x79A07F3F;             // duration
constexpr uint32_t kOff = 0xD443A2BC;                  // Off
constexpr uint32_t kX = 0x7323E97C;                    // x
constexpr uint32_t kY = 0x0424D9EA;                    // y
constexpr uint32_t kZ = 0x9D2D8850;                    // z
constexpr uint32_t kPos = 0x7F261953;                  // Pos
constexpr uint32_t kLastHeading = 0x9BC7FC76;          // last_heading
constexpr uint32_t kAmmo = 0x107081D5;                 // ammo
constexpr uint32_t kAmmoType = 0x1D1A8B59;             // ammo_type
constexpr uint32_t kPitchBailDir = 0xD65EFDE5;         // pitch_bail_dir
constexpr uint32_t kCameraLookAround = 0x81C7122D;     // CameraLookAround (a component)
constexpr uint32_t kFocusTime = 0xDA479533;            // focus_time
constexpr uint32_t kFocusAllowed = 0xF6BE5E07;         // focus_allowed
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;  // SkaterFlipAndRotate (a component)

// The component's fields used here.
constexpr uint32_t kNearLedge = 0x249;                 // (BadLedge)
constexpr uint32_t kExtraPushRadius = 0x2C0;
constexpr uint32_t kExtraPushSpeed = 0x2C4;
constexpr uint32_t kExtraPushThirdValue = 0x2C8;
constexpr uint32_t kPitchBail = 0x2D4;
constexpr uint32_t kPowerslide = 0x2D8;
constexpr uint32_t kAutoKick = 0x2D9;
constexpr uint32_t kModelRotating = 0x2DB;
constexpr uint32_t kModelRotationLeft = 0x2E0;
constexpr uint32_t kOnVehicle = 0x2ED;
constexpr uint32_t kVehicle = 0x2F0;
constexpr uint32_t kProjectiles = 0x2F4;
constexpr uint32_t kProjectileCount = 0x2F8;
constexpr uint32_t kProjectileType = 0x2FC;
constexpr uint32_t kRevert = 0x300;
constexpr uint32_t kFocus = 0x301;
constexpr uint32_t kMotoskateboard = 0x302;
constexpr uint32_t kManualTurnaround = 0x304;
constexpr uint32_t kProjectileInput = 0x308;
constexpr uint32_t kNoKick = 0x310;
constexpr uint32_t kCanBrake = 0x311;
constexpr uint32_t kRollingFrictionValue = 0x314;
constexpr uint32_t kSpecialFrictionIndex = 0x318;
constexpr uint32_t kSpecialFrictionStart = 0x31C;
constexpr uint32_t kSpecialFrictionTime = 0x320;
constexpr uint32_t kBraking = 0x324;
constexpr uint32_t kLastGroundPos = 0x338;
constexpr uint32_t kLastHeadingValue = 0x348;
constexpr uint32_t kLimitsTime = 0x350;                // -1: until cancelled, -2: for the level
constexpr uint32_t kLimitsMax = 0x354;
constexpr uint32_t kLimitsMaxMax = 0x358;
constexpr uint32_t kLimitsFriction = 0x35C;
constexpr uint32_t kLimitsGravity = 0x360;
constexpr uint32_t kNoSpin = 0x370;
constexpr uint32_t kBail = 0x371;
constexpr uint32_t kLandedFromSpine = 0x372;
constexpr uint32_t kLandedFromVert = 0x373;
constexpr uint32_t kAllowLipNoGrind = 0x375;
constexpr uint32_t kAirStart = 0x380;
constexpr uint32_t kAirEnd = 0x384;
constexpr uint32_t kLockVelocity = 0x388;
constexpr uint32_t kNoRailTricks = 0x3A4;
constexpr uint32_t kLastSpinBackside = 0x3A5;
constexpr uint32_t kGrindTweak = 0x3A8;
constexpr uint32_t kTantrum = 0x1508;
constexpr uint32_t kTagging = 0x1509;
constexpr uint32_t kThrowing = 0x14C8;
constexpr uint32_t kSoundComponent = 0x1514;           // (+0x1E: sliding)

// The timed flags (offsets into the flags block).
constexpr uint32_t kFlagCrouched = 0x20;
constexpr uint32_t kFlagFlipped = 0x28;
constexpr uint32_t kFlagVertAir = 0x30;
constexpr uint32_t kFlagSlide = 0x58;
constexpr uint32_t kFlagSpin = 0x68;
constexpr uint32_t kFlagBail = 0x70;
constexpr uint32_t kFlagSkitching = 0x88;
constexpr uint32_t kFlagCancelGround = 0x90;
constexpr uint32_t kFlagCancelWallpush = 0xB8;
constexpr uint32_t kFlagFirstTimeOnRail = 0xC8;

// The skater object's field +0x110 (switched inverts flipped unless it's set).
constexpr uint32_t kObjectRegularStance = 0x110;
// The skater object's index into the profiles' table (*0x007CE478, 8-byte entries, +0x395 the auto kick).
constexpr uint32_t kObjectProfile = 0x2BC;

// The game's constants.
constexpr float kMillisecondsPerSecond = 1000.0f;      // 0x006453C0
constexpr float kMillisecondsPerFrame = 16.0f;         // 0x00645D68
constexpr float kSecondsPerMillisecond = 0.001f;       // 0x006461D8
constexpr float kFlipLeftMin = 10.0f;                  // 0x00646710
constexpr float kSlowSquared = 100.0f;                 // 0x006463D4
constexpr float kStillSquared = 0.0001f;               // 0x006453D4
constexpr float kWallNormalY = 0.1f;                   // 0x00645D74
constexpr float kVertFeelerRaise = 50.0f;              // 0x006463CC
constexpr float kFacingDistance = 200.0f;              // (IsFacingWall's default)
constexpr uint32_t kLimitsTimeDefault = 0x551184E7;    // (float bits: about 1e13)
constexpr uint32_t kLimitsFrictionDefault = 0x360637BD; // (float bits: about 2e-6)

uint8_t& Byte(void* component, uint32_t offset) { return At<uint8_t>(component, offset); }
int Is(uint8_t value) { return value ? 1 : 0; }
Scripts::CStruct* ResultsOf(Scripts::CScript* script) { return script->m_args; }

// The time in the air: in the air or on a wall (states 1, 2) since it began, else the last one's length.
uint32_t TimeAloft(void* component)
{
    int state = At<int>(FlagsOf(component), kState);
    if (state == 1 || state == 2)
        return Since(At<uint32_t>(component, kAirStart));
    return At<uint32_t>(component, kAirEnd) - At<uint32_t>(component, kAirStart);
}

// The skater's facing, made unit (the x and y products left unrounded as the original keeps them on the x87 stack),
// as the end of a feeler from `start` `distance` long.
void FeelAlongFacing(Engine3D::CProbeRay& feeler, void* object, float distance)
{
    const Maths::Vec4& facing = VectorAt(object, kForward);
    double x = facing.x;
    double y = facing.y;
    float z = facing.z;
    double length = std::sqrt((static_cast<double>(z) * z + y * y) + x * x);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        x *= inverse;
        y *= inverse;
        z = static_cast<float>(z * inverse);
    }
    Maths::Vec4& start = feeler.m_line.start;
    Maths::Vec4& end = feeler.m_line.end;
    end.x = static_cast<float>(x * distance + start.x);
    end.y = static_cast<float>(y * distance + start.y);
    end.z = static_cast<float>(static_cast<double>(z) * distance + start.z);
}

// The feeler of IsFacingWall and its relatives: the unnamed distance (200), from the skater's position (raised by
// `raise` when set).
bool FeelAhead(void* component, Scripts::CStruct* params, Engine3D::CProbeRay& feeler, bool raise)
{
    float distance = kFacingDistance;
    params->FetchFloat(0u, &distance, 0);
    feeler.MakeNew();
    void* object = ObjectOf(component);
    const Maths::Vec4& position = VectorAt(object, kPosition);
    feeler.m_line.start = position;
    if (raise)
        feeler.m_line.start.y = static_cast<float>(static_cast<double>(position.y) + kVertFeelerRaise);
    feeler.m_line.end = feeler.m_line.start;
    FeelAlongFacing(feeler, object, distance);
    return feeler.GetHitShape(true, false) != 0;
}

// A wall the feeler hit: not vert, its normal's y within 0.1 of 0 (or not a number).
bool HitWall(const Engine3D::CProbeRay& feeler)
{
    if (feeler.m_hit.faceFlags & 8)
        return false;
    return !(std::fabs(static_cast<double>(feeler.m_surfaceDir.y)) > kWallNormalY);
}

} // namespace

// 0x004FBE80
void __cdecl CorePhysicsComponent::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterCorePhysics),
                                        g_coreMemberFunctions, 0x18);
}

// 0x004FBEA0
int CorePhysicsComponent::AirTimeThreshold(Scripts::CStruct* params)
{
    float value = 0.0f;
    params->FetchFloat(0u, &value, 0);
    if (params->HasBareFlag(kSeconds) || params->HasBareFlag(kSecond))
        return static_cast<int>(Ftol(static_cast<double>(value) * kMillisecondsPerSecond));
    if (params->HasBareFlag(kFrames) || params->HasBareFlag(kFrame))
        return static_cast<int>(Ftol(static_cast<double>(value) * kMillisecondsPerFrame));
    return static_cast<int>(Ftol(value));
}

// --- the component's own bytes ---

// 0x004FBF40
int CorePhysicsComponent::CanBrakeOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kCanBrake) = 0;
    return 1;
}
// 0x004FBF50
int CorePhysicsComponent::CanBrakeOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kCanBrake) = 1;
    return 1;
}
// 0x004FBF60
int CorePhysicsComponent::CanKickOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kNoKick) = 0;
    return 1;
}
// 0x004FBF70
int CorePhysicsComponent::CanKickOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kNoKick) = 1;
    return 1;
}
// 0x004FBF80
int CorePhysicsComponent::ForceAutoKickOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kAutoKick) = 1;
    return 1;
}
// 0x004FBF90
int CorePhysicsComponent::ForceAutoKickOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kAutoKick) = 0;
    return 1;
}
// 0x004FBFA0
int CorePhysicsComponent::AutoKickIsOff(Scripts::CStruct*, Scripts::CScript*) { return Byte(this, kAutoKick) ? 0 : 1; }
// 0x004FBFB0
int CorePhysicsComponent::SkaterVehicleOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kOnVehicle) = 1;
    return 1;
}
// 0x004FBFC0
int CorePhysicsComponent::SkaterVehicleOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kOnVehicle) = 0;
    return 1;
}
// 0x004FBFD0
int CorePhysicsComponent::IsSkaterOnVehicle(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kOnVehicle)); }
// 0x004FC000
int CorePhysicsComponent::CanSpin(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kNoSpin) = 0;
    return 1;
}
// 0x004FC010
int CorePhysicsComponent::BailOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kBail) = 1;
    return 1;
}
// 0x004FC020
int CorePhysicsComponent::BailOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kBail) = 0;
    return 1;
}
// 0x004FC030
int CorePhysicsComponent::BailIsOn(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kBail)); }
// 0x004FC080
int CorePhysicsComponent::BadLedge(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kNearLedge)); }
// 0x004FC090
int CorePhysicsComponent::LandedFromSpine(Scripts::CStruct*, Scripts::CScript*)
{
    return Is(Byte(this, kLandedFromSpine));
}
// 0x004FC0A0
int CorePhysicsComponent::LandedFromVert(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kLandedFromVert)); }
// 0x004FC0B0
int CorePhysicsComponent::SetLandedFromVert(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kLandedFromVert) = 1;
    return 1;
}
// 0x004FC0C0
int CorePhysicsComponent::ResetLandedFromVert(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kLandedFromVert) = 0;
    return 1;
}
// 0x004FC0D0
int CorePhysicsComponent::AllowLipNoGrind(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kAllowLipNoGrind) = 1;
    return 1;
}
// 0x004FC0E0
int CorePhysicsComponent::ClearAllowLipNoGrind(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kAllowLipNoGrind) = 0;
    return 1;
}
// 0x004FC0F0
int CorePhysicsComponent::NoRailTricks(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kNoRailTricks) = 1;
    return 1;
}
// 0x004FC100
int CorePhysicsComponent::AllowRailTricks(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kNoRailTricks) = 0;
    return 1;
}
// 0x004FC1A0
int CorePhysicsComponent::InRevert(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kRevert) = 1;
    return 1;
}
// 0x004FC1B0
int CorePhysicsComponent::OutRevert(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kRevert) = 0;
    return 1;
}
// 0x004FC1C0
int CorePhysicsComponent::IsInRevert(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kRevert)); }
// 0x004FC1D0
int CorePhysicsComponent::ProjectilesOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kProjectiles) = 1;
    return 1;
}
// 0x004FC1E0
int CorePhysicsComponent::ProjectilesOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kProjectiles) = 0;
    return 1;
}
// 0x004FC1F0
int CorePhysicsComponent::FocusOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kFocus) = 1;
    return 1;
}
// 0x004FC200
int CorePhysicsComponent::IsSkaterFocused(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kFocus)); }
// 0x004FC210
int CorePhysicsComponent::ResetProjectileInput(Scripts::CStruct*, Scripts::CScript*)
{
    At<int>(this, kProjectileInput) = -1;
    return 1;
}
// 0x004FC230
int CorePhysicsComponent::EnterPowerslide(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kPowerslide) = 1;
    return 1;
}
// 0x004FC240
int CorePhysicsComponent::ExitPowerslide(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kPowerslide) = 0;
    return 1;
}
// 0x004FC250
int CorePhysicsComponent::MotoSkateboardOn(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kMotoskateboard) = 1;
    return 1;
}
// 0x004FC260
int CorePhysicsComponent::MotoSkateboardOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kMotoskateboard) = 0;
    return 1;
}
// 0x004FC270
int CorePhysicsComponent::IsOnMotoskateboard(Scripts::CStruct*, Scripts::CScript*)
{
    return Is(Byte(this, kMotoskateboard));
}
// 0x004FC280
int CorePhysicsComponent::SkaterIsFlipping(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kModelRotating)); }
// 0x004FC290
int CorePhysicsComponent::ResetIsFlipping(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kModelRotating) = 0;
    return 1;
}
// 0x004FC2A0
int CorePhysicsComponent::SetThrowingProjectile(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kThrowing) = 1;
    return 1;
}
// 0x004FC2B0
int CorePhysicsComponent::UnsetThrowingProjectile(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kThrowing) = 0;
    return 1;
}
// 0x004FC2C0
int CorePhysicsComponent::IsThrowingProjectile(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kThrowing)); }
// 0x004FC2D0
int CorePhysicsComponent::EndManualTurnaround(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kManualTurnaround) = 0;
    return 1;
}
// 0x004FC2E0
int CorePhysicsComponent::EnterTantrum(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kTantrum) = 1;
    return 1;
}
// 0x004FC2F0
int CorePhysicsComponent::ExitTantrum(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kTantrum) = 0;
    return 1;
}
// 0x004FC300
int CorePhysicsComponent::InTantrum(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kTantrum)); }
// 0x004FC310
int CorePhysicsComponent::EnterTagging(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kTagging) = 1;
    return 1;
}
// 0x004FC320
int CorePhysicsComponent::ExitTagging(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kTagging) = 0;
    return 1;
}
// 0x004FC330
int CorePhysicsComponent::IsTagging(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kTagging)); }
// 0x004FC9B0
int CorePhysicsComponent::Braking(Scripts::CStruct*, Scripts::CScript*) { return Is(Byte(this, kBraking)); }

// --- the timed flags ---

// 0x004FC780
int CorePhysicsComponent::Flipped(Scripts::CStruct*, Scripts::CScript*) { return Is(FlagsOf(this)[kFlagFlipped]); }
// 0x004FC7A0
int CorePhysicsComponent::Switched(Scripts::CStruct*, Scripts::CScript*)
{
    bool flipped = FlagsOf(this)[kFlagFlipped] != 0;
    if (At<uint8_t>(ObjectOf(this), kObjectRegularStance))
        return flipped ? 1 : 0;
    return flipped ? 0 : 1;
}
// 0x004FC7E0
int CorePhysicsComponent::Crouched(Scripts::CStruct*, Scripts::CScript*) { return Is(FlagsOf(this)[kFlagCrouched]); }
// 0x004FC800
int CorePhysicsComponent::FirstTimeOnThisRail(Scripts::CStruct*, Scripts::CScript*)
{
    return Is(FlagsOf(this)[kFlagFirstTimeOnRail]);
}
// 0x004FC820
int CorePhysicsComponent::Skitching(Scripts::CStruct*, Scripts::CScript*) { return Is(FlagsOf(this)[kFlagSkitching]); }
// 0x004FC840
int CorePhysicsComponent::CancelWallpush(Scripts::CStruct*, Scripts::CScript*)
{
    SetTimedFlag(this, kFlagCancelWallpush, 1);
    return 1;
}
// 0x004FCF90
int CorePhysicsComponent::NoSpin(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kNoSpin) = 1;
    SetTimedFlag(this, kFlagSpin, 0);
    return 1;
}
// 0x004FCFC0
int CorePhysicsComponent::InBail(Scripts::CStruct*, Scripts::CScript*)
{
    SetTimedFlag(this, kFlagBail, 1);
    return 1;
}
// 0x004FCFF0
int CorePhysicsComponent::NotInBail(Scripts::CStruct*, Scripts::CScript*)
{
    SetTimedFlag(this, kFlagBail, 0);
    return 1;
}
// 0x004FD020
int CorePhysicsComponent::IsInBail(Scripts::CStruct*, Scripts::CScript*) { return Is(FlagsOf(this)[kFlagBail]); }
// 0x004FD1C0
int CorePhysicsComponent::InVertAir(Scripts::CStruct*, Scripts::CScript*) { return Is(FlagsOf(this)[kFlagVertAir]); }
// 0x004FD530
int CorePhysicsComponent::OverrideCancelGround(Scripts::CStruct* params, Scripts::CScript*)
{
    SetTimedFlag(this, kFlagCancelGround, params->HasBareFlag(kOff) ? 0 : 1);
    return 1;
}
// 0x004FD630
int CorePhysicsComponent::SetVertAir(Scripts::CStruct*, Scripts::CScript*)
{
    SetTimedFlag(this, kFlagVertAir, 1);
    return 1;
}

// --- values from the parameters ---

// 0x004FBFE0
int CorePhysicsComponent::SetSkaterVehicle(Scripts::CStruct* params, Scripts::CScript*)
{
    params->ReadHashValue(0u, &At<uint32_t>(this, kVehicle), 1);
    return 1;
}
// 0x004FC040
int CorePhysicsComponent::LockVelocityDirection(Scripts::CStruct* params, Scripts::CScript*)
{
    Byte(this, kLockVelocity) = params->HasBareFlag(kOn);
    return 1;
}
// 0x004FC060
int CorePhysicsComponent::SetGrindTweak(Scripts::CStruct* params, Scripts::CScript*)
{
    params->FetchIntValue(0u, &At<int>(this, kGrindTweak), 0);
    return 1;
}
// 0x004FC180
int CorePhysicsComponent::SetAmmoType(Scripts::CStruct* params, Scripts::CScript*)
{
    params->ReadHashValue(0u, &At<uint32_t>(this, kProjectileType), 0);
    return 1;
}
// 0x004FC110
int CorePhysicsComponent::SetExtraPush(Scripts::CStruct* params, Scripts::CScript*)
{
    float* radius = &At<float>(this, kExtraPushRadius);
    float* speed = &At<float>(this, kExtraPushSpeed);
    float* third = &At<float>(this, kExtraPushThirdValue);
    *radius = 48.0f;
    *speed = 100.0f;
    *third = 6.0f;
    params->FetchFloat(kRadius, radius, 0);
    params->FetchFloat(kSpeed, speed, 0);
    params->FetchFloat(kExtraPushThird, third, 0);
    return 1;
}
// 0x004FC340
int CorePhysicsComponent::DoingFlip(Scripts::CStruct*, Scripts::CScript*)
{
    if (Byte(this, kModelRotating) && At<float>(this, kModelRotationLeft) > kFlipLeftMin)
        return 1;
    return 0;
}

// --- the air time ---

// 0x004FC880
int CorePhysicsComponent::AirTimeLessThan(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t time = TimeAloft(this);
    return time < static_cast<uint32_t>(AirTimeThreshold(params)) ? 1 : 0;
}
// 0x004FC8E0
int CorePhysicsComponent::AirTimeGreaterThan(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t time = TimeAloft(this);
    return static_cast<uint32_t>(AirTimeThreshold(params)) < time ? 1 : 0;
}
// 0x004FC940
int CorePhysicsComponent::GetAirtime(Scripts::CStruct*, Scripts::CScript* script)
{
    uint32_t time = TimeAloft(this);
    float seconds = static_cast<float>(static_cast<double>(time) * kSecondsPerMillisecond);
    ResultsOf(script)->PushFloat(kAirtime, seconds);
    return 1;
}

// 0x004FC9C0
int CorePhysicsComponent::RestoreAutoKick(Scripts::CStruct*, Scripts::CScript*)
{
    int profile = At<int>(ObjectOf(this), kObjectProfile);
    uint8_t* profiles = reinterpret_cast<uint8_t*>(GameModules::g_skateModule);
    Byte(this, kAutoKick) = profiles[profile * 8 + 0x395];
    return 1;
}

// 0x004FC9F0
int CorePhysicsComponent::DoCarPlantBoost(Scripts::CStruct*, Scripts::CScript*)
{
    void* object = ObjectOf(this);
    Maths::Vec4& velocity = VectorAt(object, kVelocity);
    float upward = velocity.y;
    if (upward < 0.0f)
        upward = 0.0f;
    velocity.y = upward;
    velocity.y = static_cast<float>(static_cast<double>(PhysicsFloat(kCarPlantUpwardBoost, object)) + velocity.y);
    // Along the horizontal velocity; slower than 10: the facing; that barely anything either: (1, 0, 1).
    Maths::Vec4 direction(velocity.x, 0.0f, velocity.z, velocity.w);
    if (static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x < kSlowSquared) {
        const Maths::Vec4& facing = VectorAt(object, kForward);
        direction = Maths::Vec4(facing.x, 0.0f, facing.z, facing.w);
        double flat = static_cast<double>(facing.z) * facing.z + static_cast<double>(facing.x) * facing.x;
        if (flat < kStillSquared)
            direction = Maths::Vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    double length = std::sqrt(static_cast<double>(direction.z) * direction.z +
                              static_cast<double>(direction.x) * direction.x);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        direction.x = static_cast<float>(direction.x * inverse);
        direction.y = static_cast<float>(0.0 * inverse);
        direction.z = static_cast<float>(direction.z * inverse);
    }
    double boost = PhysicsFloat(kCarPlantForwardBoost, object);
    float y = static_cast<float>(direction.y * boost);
    float z = static_cast<float>(direction.z * boost);
    float w = static_cast<float>(direction.w * boost);
    Maths::Vec4& v = VectorAt(ObjectOf(this), kVelocity);
    v.x = static_cast<float>(v.x + direction.x * boost);
    v.y += y;
    v.z += z;
    v.w += w;
    return 1;
}

// 0x004FD040
int CorePhysicsComponent::SetRailSound(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t sound = 0;
    params->ReadHashValue(0u, &sound, 1);
    bool slide = sound == kSlide;
    SetTimedFlag(this, kFlagSlide, slide ? 1 : 0);
    At<uint8_t>(At<void*>(this, kSoundComponent), 0x1E) = slide ? 1 : 0;
    return 1;
}

// 0x004FD0D0
int CorePhysicsComponent::SetRollingFriction(Scripts::CStruct* params, Scripts::CScript*)
{
    void* object = ObjectOf(this);
    float* friction = &At<float>(this, kRollingFrictionValue);
    params->FetchFloat(0u, friction, 0);
    if (params->HasBareFlag(kDefault) && At<float>(this, kSpecialFrictionTime) == 0.0f)
        *friction = PhysicsFloat(kRollingFriction, object);
    return 1;
}

// 0x004FD140
int CorePhysicsComponent::LastSpinWas(Scripts::CStruct* params, Scripts::CScript*)
{
    if (params->HasBareFlag(kBackside)) {
        bool flipped = FlagsOf(this)[kFlagFlipped] != 0;
        bool backside = Byte(this, kLastSpinBackside) != 0;
        return (flipped ? !backside : backside) ? 1 : 0;
    }
    if (params->HasBareFlag(kFrontside)) {
        bool flipped = FlagsOf(this)[kFlagFlipped] != 0;
        bool backside = Byte(this, kLastSpinBackside) != 0;
        return (flipped ? backside : !backside) ? 1 : 0;
    }
    return 1;
}

// 0x004FD1E0
int CorePhysicsComponent::TurnToFaceVelocity(Scripts::CStruct*, Scripts::CScript*)
{
    void* object = ObjectOf(this);
    const Maths::Vec4& velocity = VectorAt(object, kVelocity);
    double x = velocity.x, y = velocity.y, z = velocity.z;
    double lengthSquared = (x * x + y * y) + z * z;
    if (lengthSquared < kStillSquared)
        return 1;
    Maths::Vec4& right = VectorAt(object, kMatrix);
    Maths::Vec4& up = VectorAt(object, kUp);
    Maths::Vec4& facing = VectorAt(object, kForward);
    facing = VectorAt(object, kVelocity);
    facing.MakeUnit();
    // right = up x facing
    double rx = static_cast<double>(facing.z) * up.y - static_cast<double>(facing.y) * up.z;
    double ry = static_cast<double>(facing.x) * up.z - static_cast<double>(facing.z) * up.x;
    right.w = 0.0f;
    float rz = static_cast<float>(static_cast<double>(facing.y) * up.x - static_cast<double>(facing.x) * up.y);
    right.z = rz;
    right.x = static_cast<float>(rx);
    right.y = static_cast<float>(ry);
    right.MakeUnit();
    // up = facing x right
    double ux = static_cast<double>(right.z) * facing.y - static_cast<double>(right.y) * facing.z;
    double uy = static_cast<double>(right.x) * facing.z - static_cast<double>(right.z) * facing.x;
    up.w = 0.0f;
    float uz = static_cast<float>(static_cast<double>(right.y) * facing.x - static_cast<double>(right.x) * facing.y);
    up.z = uz;
    up.x = static_cast<float>(ux);
    up.y = static_cast<float>(uy);
    up.MakeUnit();
    At<Maths::Mat4>(this, kDisplayMatrix) = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    return 1;
}

// 0x004FD360
int CorePhysicsComponent::OverrideLimits(Scripts::CStruct* params, Scripts::CScript*)
{
    void* object = ObjectOf(this);
    float& time = At<float>(this, kLimitsTime);
    if (params->HasBareFlag(kCancelLimits)) {
        time = 0.0f;
        return 1;
    }
    if (At<uint32_t>(this, kLimitsTime) == 0xC0000000u) // -2: for the level
        return 1;
    if (params->HasBareFlag(kEnd)) {
        time = 0.0f;
        return 1;
    }
    float* max = &At<float>(this, kLimitsMax);
    params->FetchFloat(kMax, max, 1);
    float* maxMax = &At<float>(this, kLimitsMaxMax);
    At<uint32_t>(this, kLimitsMaxMax) = *reinterpret_cast<uint32_t*>(max);
    params->FetchFloat(kMaxMax, maxMax, 0);
    At<uint32_t>(this, kLimitsTime) = kLimitsTimeDefault;
    params->FetchFloat(kTime, &time, 0);
    float* friction = &At<float>(this, kLimitsFriction);
    At<uint32_t>(this, kLimitsFriction) = kLimitsFrictionDefault;
    params->FetchFloat(kFriction, friction, 0);
    float* gravity = &At<float>(this, kLimitsGravity);
    *gravity = PhysicsFloat(kGroundGravity, object);
    params->FetchFloat(kGravity, gravity, 0);
    if (params->HasBareFlag(kUntilCancelled))
        time = -1.0f;
    if (params->HasBareFlag(kCurrentLevel))
        time = -2.0f;
    return 1;
}

// 0x004FD4A0
int CorePhysicsComponent::SetSpecialFriction(Scripts::CStruct* params, Scripts::CScript*)
{
    Scripts::CArray* frictions = nullptr;
    params->FetchList(0u, &frictions, 0);
    int& index = At<int>(this, kSpecialFrictionIndex);
    int count = static_cast<int>(frictions->m_extent); // (no array: the original reads through null too)
    if (index >= count)
        index = count - 1;
    At<float>(this, kRollingFrictionValue) = frictions->FetchFloat(static_cast<uint32_t>(index));
    if (index == 0)
        At<int>(this, kSpecialFrictionStart) = Now();
    ++index;
    params->FetchFloat(kDuration, &At<float>(this, kSpecialFrictionTime), 0);
    return 1;
}

// --- results for the script ---

// 0x004FD580
int CorePhysicsComponent::GetMatrixNormal(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultsOf(script)->PushFloat(kX, VectorAt(ObjectOf(this), kUp).x);
    ResultsOf(script)->PushFloat(kY, VectorAt(ObjectOf(this), kUp).y);
    ResultsOf(script)->PushFloat(kZ, VectorAt(ObjectOf(this), kUp).z);
    return 1;
}
// 0x004FD5E0
int CorePhysicsComponent::GetLastGroundPos(Scripts::CStruct*, Scripts::CScript* script)
{
    const Maths::Vec4& position = VectorAt(this, kLastGroundPos);
    ResultsOf(script)->PutVec3(kPos, position.x, position.y, position.z);
    return 1;
}
// 0x004FD610
int CorePhysicsComponent::GetLastGroundHeading(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultsOf(script)->PushFloat(kLastHeading, At<float>(this, kLastHeadingValue));
    return 1;
}
// 0x004FDC30
int CorePhysicsComponent::GetAmmo(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultsOf(script)->PutInt(kAmmo, At<int>(this, kProjectileCount));
    return 1;
}
// 0x004FDC50
int CorePhysicsComponent::GetAmmoType(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultsOf(script)->PutNameKey(kAmmoType, At<uint32_t>(this, kProjectileType));
    return 1;
}
// 0x004FDC70
int CorePhysicsComponent::GetPitchBailDir(Scripts::CStruct*, Scripts::CScript* script)
{
    ResultsOf(script)->PutNameKey(kPitchBailDir, At<uint32_t>(this, kPitchBail));
    return 1;
}

// --- feelers ahead ---

// 0x004FD660
int CorePhysicsComponent::IsFacingWall(Scripts::CStruct* params, Scripts::CScript*)
{
    Engine3D::CProbeRay feeler;
    if (FeelAhead(this, params, feeler, false) && HitWall(feeler))
        return 1;
    return 0;
}

// 0x004FD840
int CorePhysicsComponent::TurnFromWall(Scripts::CStruct* params, Scripts::CScript*)
{
    Engine3D::CProbeRay feeler;
    if (!FeelAhead(this, params, feeler, false) || !HitWall(feeler))
        return 0;
    VectorAt(ObjectOf(this), kVelocity).ChangeHeading(feeler.m_surfaceDir);
    VectorAt(ObjectOf(this), kForward).ChangeHeading(feeler.m_surfaceDir);
    static_cast<GameObjects::CRiderSpinAndFlip*>(
        static_cast<GameObjects::CGameEntity*>(ObjectOf(this))->FetchModule(kSkaterFlipAndRotate))
        ->Flip();
    At<Maths::Mat4>(this, kDisplayMatrix) = At<Maths::Mat4>(ObjectOf(this), kMatrix);
    return 1;
}

// 0x004FDA80
int CorePhysicsComponent::IsFacingVert(Scripts::CStruct* params, Scripts::CScript*)
{
    Engine3D::CProbeRay feeler;
    if (FeelAhead(this, params, feeler, true) && (feeler.m_hit.faceFlags & 8))
        return 1;
    return 0;
}

// --- focus ---

// 0x004FDC90
int CorePhysicsComponent::FocusOff(Scripts::CStruct*, Scripts::CScript*)
{
    Byte(this, kFocus) = 0;
    auto* camera = static_cast<GameObjects::CGameEntity*>(static_cast<GameObjects::CRider*>(ObjectOf(this))->GetSkaterCamera());
    static_cast<GameObjects::CFreeLookModule*>(camera->FetchModule(kCameraLookAround))->SetLocked(false);
    return 1;
}
// 0x004FDCC0
int CorePhysicsComponent::GetFocusTimeLeft(Scripts::CStruct*, Scripts::CScript* script)
{
    auto* score = static_cast<uint8_t*>(static_cast<GameObjects::CRider*>(ObjectOf(this))->GetPointsTotal());
    if (!score)
        return 0;
    ResultsOf(script)->PushFloat(kFocusTime, At<float>(score, 0));
    return 1;
}
// 0x004FDCF0
int CorePhysicsComponent::GetFocusTimesAllowed(Scripts::CStruct*, Scripts::CScript* script)
{
    auto* score = static_cast<uint8_t*>(static_cast<GameObjects::CRider*>(ObjectOf(this))->GetPointsTotal());
    if (!score)
        return 0;
    ResultsOf(script)->PutInt(kFocusAllowed, At<int>(score, 4));
    return 1;
}
// 0x004FDD20
int CorePhysicsComponent::SetFocusTimesAllowed(Scripts::CStruct* params, Scripts::CScript*)
{
    auto* score = static_cast<uint8_t*>(static_cast<GameObjects::CRider*>(ObjectOf(this))->GetPointsTotal());
    if (!score)
        return 0;
    params->FetchIntValue(0u, &At<int>(score, 4), 0);
    return 1;
}

} // namespace Skater

using Skater::CorePhysicsComponent;
