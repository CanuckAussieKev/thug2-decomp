// The skater core physics component's script member functions (its table at 0x006826C8): the flags and values
// scripts set and ask about, and a few small actions (0x004FBE80-0x004FDD47).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/core/enum_ops.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"
#include "game/rider/skater_sound.h"
#include "game/rider/skater_state.h"
#include "game/skate/point_tally.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/render/coll_obj.h"
#include <bit>

namespace skater {
namespace {
constexpr float kProbeRadius = 48.0f;
constexpr float kProbeThird = 6.0f;
constexpr uint32_t kJump = 0x584CF9E9; // Jump
constexpr uint32_t kSkaterCorePhysicsName = 0x5BD63B29; // SkaterCorePhysics
constexpr uint32_t kFlippedName = 0x0C7A712C; // Flipped
constexpr uint32_t kSwitched = 0x8F66B80B; // Switched
constexpr uint32_t kCrouchedName = 0x4ADC6C2A; // Crouched
constexpr uint32_t kFirstTimeOnThisRail = 0x262D42D5; // FirstTimeOnThisRail
constexpr uint32_t kStartSkitch = 0xCA6B3809; // StartSkitch
constexpr uint32_t kSkitchingName = 0x9C6A7E41; // Skitching
constexpr uint32_t kStopSkitch = 0x32DCC9CF; // StopSkitch
constexpr uint32_t kCancelWallpushName = 0x5E8F9E77; // CancelWallpush
constexpr uint32_t kAirTimeLessThan = 0x0C890A84; // AirTimeLessThan
constexpr uint32_t kAirTimeGreaterThan = 0xBBF2B570; // AirTimeGreaterThan
constexpr uint32_t kGetAirtime = 0xDE583E34; // GetAirtime
constexpr uint32_t kGetAirTimeLeft = 0x1996B797; // GetAirTimeLeft
constexpr uint32_t kBraking = 0x1F8BBD05; // Braking
constexpr uint32_t kCanBrakeOff = 0xEBBCF455; // CanBrakeOff
constexpr uint32_t kCanBrakeOn = 0xBC19E291; // CanBrakeOn
constexpr uint32_t kCanKick = 0x02F66333; // CanKick
constexpr uint32_t kCanKickOn = 0x68BF6C13; // CanKickOn
constexpr uint32_t kCanKickOff = 0xE8DEB0D7; // CanKickOff
constexpr uint32_t kForceAutoKickOn = 0x34DCFC97; // ForceAutoKickOn
constexpr uint32_t kForceAutoKickOff = 0x0257947E; // ForceAutoKickOff
constexpr uint32_t kAutoKickIsOff = 0x01BAA1D9; // AutoKickIsOff
constexpr uint32_t kRestoreAutoKick = 0x9FCFDFEB; // RestoreAutoKick
constexpr uint32_t kSkaterVehicleOn = 0xFEFF11CC; // SkaterVehicleOn
constexpr uint32_t kSkaterVehicleOff = 0xFE243FEF; // SkaterVehicleOff
constexpr uint32_t kIsSkaterOnVehicle = 0xE168CFE0; // IsSkaterOnVehicle
constexpr uint32_t kSetSkaterVehicle = 0x29FBA9DF; // SetSkaterVehicle
constexpr uint32_t kDoCarPlantBoost = 0x17846595; // DoCarPlantBoost
constexpr uint32_t kHandleLipOllieDirection = 0x259CB90D; // HandleLipOllieDirection
constexpr uint32_t kOrientToNormal = 0x1D1FD4F0; // OrientToNormal
constexpr uint32_t kSetSpeed = 0x383B939B; // SetSpeed
constexpr uint32_t kNoSpin = 0x54EF2B79; // NoSpin
constexpr uint32_t kCanSpin = 0x0E2998B9; // CanSpin
constexpr uint32_t kInBail = 0x6FC5AAE0; // InBail
constexpr uint32_t kNotInBail = 0xBD4303F4; // NotInBail
constexpr uint32_t kIsInBail = 0xA901A50C; // IsInBail
constexpr uint32_t kBailOn = 0xE0DF1F0A; // BailOn
constexpr uint32_t kBailOff = 0x8C3D7864; // BailOff
constexpr uint32_t kBailIsOn = 0x0E1D5168; // BailIsOn
constexpr uint32_t kSetState = 0x948EBF96; // SetState
constexpr uint32_t kSetRailSound = 0x947FCCF4; // SetRailSound
constexpr uint32_t kLockVelocityDirection = 0xACB82C02; // LockVelocityDirection
constexpr uint32_t kSetRollingFriction = 0x510F983B; // SetRollingFriction
constexpr uint32_t kSetGrindTweak = 0x71B993B7; // SetGrindTweak
constexpr uint32_t kBadLedge = 0xBE371B7B; // BadLedge
constexpr uint32_t kSkateInAble = 0x55934543; // SkateInAble
constexpr uint32_t kNearGround = 0xB424244D; // NearGround
constexpr uint32_t kTriggerGrindOff = 0x224CDDC9; // TriggerGrindOff
constexpr uint32_t kLastSpinWas = 0x6C8A1316; // LastSpinWas
constexpr uint32_t kLandedFromSpine = 0x448E3630; // LandedFromSpine
constexpr uint32_t kLandedFromVert = 0xF37CE040; // LandedFromVert
constexpr uint32_t kSetLandedFromVert = 0x133E1293; // SetLandedFromVert
constexpr uint32_t kResetLandedFromVert = 0x0F922431; // ResetLandedFromVert
constexpr uint32_t kInVertAir = 0xDFB8F052; // InVertAir
constexpr uint32_t kAllowLipNoGrind = 0xC5B4B390; // AllowLipNoGrind
constexpr uint32_t kClearAllowLipNoGrind = 0xB9EEBFF6; // ClearAllowLipNoGrind
constexpr uint32_t kNoRailTricks = 0xEC681A59; // NoRailTricks
constexpr uint32_t kAllowRailTricks = 0x065A559A; // AllowRailTricks
constexpr uint32_t kTurnToFaceVelocity = 0x461E5B92; // TurnToFaceVelocity
constexpr uint32_t kCancelRotateDisplayandUpdateMatrix = 0xA14A467F; // CancelRotateDisplayandUpdateMatrix
constexpr uint32_t kOverrideLimits = 0x90F1D8D5; // OverrideLimits
constexpr uint32_t kSetSpecialFriction = 0x5E8D9B80; // SetSpecialFriction
constexpr uint32_t kOverrideCancelGround = 0xB94BC0C6; // OverrideCancelGround
constexpr uint32_t kSetExtraPush = 0xE47FF4B5; // SetExtraPush
constexpr uint32_t kGetMatrixNormal = 0x05144783; // GetMatrixNormal
constexpr uint32_t kGetLastGroundPos = 0xF7E21884; // GetLastGroundPos
constexpr uint32_t kGetLastGroundHeading = 0x0BFF3271; // GetLastGroundHeading
constexpr uint32_t kTurnOnFlag30 = 0x2A547E37; // text unknown (the member that turns the +0x30 flag on)
constexpr uint32_t kIsFacingWall = 0xAAE7E1D8; // IsFacingWall
constexpr uint32_t kFaceWallAndFlip = 0xA038260D; // text unknown (IsFacingWall feeler, then the facing turned along the wall and the skater flipped)
constexpr uint32_t kIsFacingVert = 0xD27F89E8; // IsFacingVert
constexpr uint32_t kSetAmmo = 0x25B393E7; // SetAmmo
constexpr uint32_t kGetAmmo = 0xB6222B3A; // GetAmmo
constexpr uint32_t kSetAmmoType = 0x9D8BDE21; // SetAmmoType
constexpr uint32_t kGetAmmoType = 0x83A46968; // GetAmmoType
constexpr uint32_t kGetPitchBailDir = 0x6A0834C5; // GetPitchBailDir
constexpr uint32_t kForceBreakVert = 0x675CF35E; // ForceBreakVert (letter case unknown)
constexpr uint32_t kInRevert = 0xD3E1A093; // InRevert
constexpr uint32_t kOutRevert = 0x3BAAC2C5; // OutRevert
constexpr uint32_t kIsInRevert = 0xB195C726; // IsInRevert
constexpr uint32_t kProjectilesOn = 0xB649958F; // ProjectilesOn
constexpr uint32_t kProjectilesOff = 0x11B99941; // ProjectilesOff
constexpr uint32_t kFocusOn = 0xEFFC54DC; // FocusOn
constexpr uint32_t kFocusOff = 0xE3822CCE; // FocusOff
constexpr uint32_t kIsSkaterFocused = 0xD5BCAB93; // IsSkaterFocused
constexpr uint32_t kResetProjectileInput = 0x9AF4A4C2; // ResetProjectileInput
constexpr uint32_t kEnterPowerslide = 0x8A8EBE72; // EnterPowerslide
constexpr uint32_t kExitPowerslide = 0xC5623AEE; // ExitPowerslide
constexpr uint32_t kWalkingWallplant = 0xD7ECF37C; // WalkingWallplant
constexpr uint32_t kMotoSkateboardOn = 0x43A7516D; // MotoSkateboardOn
constexpr uint32_t kMotoSkateboardOff = 0x5F48F4D1; // MotoSkateboardOff
constexpr uint32_t kIsOnMotoSkateboard = 0x5F43DDF3; // IsOnMotoSkateboard
constexpr uint32_t kSkaterIsFlipping = 0x68F6DB80; // SkaterIsFlipping
constexpr uint32_t kResetIsFlipping = 0x1B4A5FF9; // ResetIsFlipping
constexpr uint32_t kSetThrowingProjectile = 0x007CDADB; // SetThrowingProjectile
constexpr uint32_t kUnsetThrowingProjectile = 0x27C92E33; // UnsetThrowingProjectile
constexpr uint32_t kIsThrowingProjectile = 0x8476CA5B; // IsThrowingProjectile
constexpr uint32_t kCheckNatasExitDirection = 0xF900547B; // CheckNatasExitDirection
constexpr uint32_t kEndManualTurnaround = 0xFC9E2886; // EndManualTurnaround
constexpr uint32_t kGetFocusTimeLeft = 0x3981FBF0; // GetFocusTimeLeft
constexpr uint32_t kGetFocusTimesAllowed = 0x821BF006; // GetFocusTimesAllowed
constexpr uint32_t kSetFocusTimesAllowed = 0x2C30B07A; // SetFocusTimesAllowed
constexpr uint32_t kEnterTantrum = 0x1EB4121A; // EnterTantrum
constexpr uint32_t kExitTantrum = 0xA751E3D8; // ExitTantrum
constexpr uint32_t kInTantrum = 0x03B9946F; // InTantrum
constexpr uint32_t kDoingFlip = 0xADCA7EE6; // DoingFlip
constexpr uint32_t kEnterTagging = 0x0898E60F; // EnterTagging
constexpr uint32_t kExitTagging = 0xB17D17CD; // ExitTagging
constexpr uint32_t kIsTagging = 0xF22B4480; // IsTagging
} // namespace

std::array<script::MemberFunctionEntry, kCoreMemberFunctionCount>& core_member_functions()
{
    static std::array<script::MemberFunctionEntry, kCoreMemberFunctionCount> core_member_functions{{
        {.callsign_ = kJump, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kFlippedName, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSwitched, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kCrouchedName, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kFirstTimeOnThisRail, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kStartSkitch, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSkitchingName, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kStopSkitch, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kCancelWallpushName, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kAirTimeLessThan, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kAirTimeGreaterThan, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kGetAirtime, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kGetAirTimeLeft, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kBraking, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kCanBrakeOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kCanBrakeOn, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kCanKick, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kCanKickOn, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kCanKickOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kForceAutoKickOn, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kForceAutoKickOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kAutoKickIsOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kRestoreAutoKick, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSkaterVehicleOn, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kSkaterVehicleOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kIsSkaterOnVehicle, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetSkaterVehicle, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kDoCarPlantBoost, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kHandleLipOllieDirection, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kOrientToNormal, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetSpeed, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kNoSpin, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kCanSpin, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kInBail, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kNotInBail, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kIsInBail, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kBailOn, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kBailOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kBailIsOn, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetState, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kSetRailSound, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kLockVelocityDirection, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetRollingFriction, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetGrindTweak, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kBadLedge, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSkateInAble, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kNearGround, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kTriggerGrindOff, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kLastSpinWas, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kLandedFromSpine, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kLandedFromVert, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetLandedFromVert, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kResetLandedFromVert, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kInVertAir, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kAllowLipNoGrind, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kClearAllowLipNoGrind, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kNoRailTricks, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kAllowRailTricks, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kTurnToFaceVelocity, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kCancelRotateDisplayandUpdateMatrix, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kOverrideLimits, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetSpecialFriction, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kOverrideCancelGround, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetExtraPush, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kGetMatrixNormal, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kGetLastGroundPos, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kGetLastGroundHeading, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kTurnOnFlag30, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kIsFacingWall, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kFaceWallAndFlip, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kIsFacingVert, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetAmmo, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kGetAmmo, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetAmmoType, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kGetAmmoType, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kGetPitchBailDir, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kForceBreakVert, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kInRevert, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kOutRevert, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kIsInRevert, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kProjectilesOn, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kProjectilesOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kFocusOn, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kFocusOff, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kIsSkaterFocused, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kResetProjectileInput, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kEnterPowerslide, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kExitPowerslide, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kWalkingWallplant, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kMotoSkateboardOn, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kMotoSkateboardOff, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kIsOnMotoSkateboard, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kSkaterIsFlipping, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kResetIsFlipping, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetThrowingProjectile, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kUnsetThrowingProjectile, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kIsThrowingProjectile, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kCheckNatasExitDirection, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kEndManualTurnaround, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kGetFocusTimeLeft, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kGetFocusTimesAllowed, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kSetFocusTimesAllowed, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kEnterTantrum, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kExitTantrum, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kInTantrum, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kDoingFlip, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kEnterTagging, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = kExitTagging, .owner_kind_ = kSkaterCorePhysicsName},
        {.callsign_ = kIsTagging, .owner_kind_ = kSkaterCorePhysicsName}, {.callsign_ = 0, .owner_kind_ = kSkaterCorePhysicsName}}};
    return core_member_functions;
}

using namespace physics;

namespace {

constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29; // SkaterCorePhysics (the component type)

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
constexpr uint32_t kCoastDrag = 0x78F80EC4;            // Physics_Rolling_Friction
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

// The timed flags (offsets into the flags block).

// The skater object's field +0x110 (switched inverts flipped unless it's set).
// The skater object's index into the profiles' table (*0x007CE478, 8-byte entries, +0x395 the auto kick).

// The game's constants.
constexpr float kMillisecondsPerSecond = 1000.0f;       // 0x006453C0
constexpr float kMillisecondsPerFrame = 16.0f;          // 0x00645D68
constexpr float kSecondsPerMillisecond = 0.001f;        // 0x006461D8
constexpr float kFlipLeftMin = 10.0f;                   // 0x00646710
constexpr float kSlowSquared = 100.0f;                  // 0x006463D4
constexpr float kStillSquared = 0.0001f;                // 0x006453D4
constexpr float kWallNormalY = 0.1f;                    // 0x00645D74
constexpr float kVertFeelerRaise = 50.0f;               // 0x006463CC
constexpr float kFacingDistance = 200.0f;               // (IsFacingWall's default)
constexpr uint32_t kLimitsTimeDefault = 0x551184E7;     // (float bits: about 1e13)
constexpr uint32_t kLimitsFrictionDefault = 0x360637BD; // (float bits: about 2e-6)

int get_is(uint8_t value) { return value ? 1 : 0; }

// The skater's facing, made unit (the x and y products left unrounded as the original keeps them on the x87 stack),
// as the end of a feeler from `start` `distance` long.
void feel_along_facing(render::ProbeRay& feeler, objects::GameEntity* object, float distance)
{
    const maths::Vec4& facing = core::at(object->basis_frame().row, 2);
    double x = facing.x;
    double y = facing.y;
    float z = facing.z;
    const double length = std::sqrt((static_cast<double>(z) * z + y * y) + x * x);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        x *= inverse;
        y *= inverse;
        z = static_cast<float>(z * inverse);
    }
    const maths::Vec4& start = feeler.line().start;
    maths::Vec4& end = feeler.line().end;
    end.x = static_cast<float>(x * distance + start.x);
    end.y = static_cast<float>(y * distance + start.y);
    end.z = static_cast<float>(static_cast<double>(z) * distance + start.z);
}

// A wall the feeler hit: not vert, its normal's y within 0.1 of 0 (or not a number).
bool hit_wall(const render::ProbeRay& feeler)
{
    if (core::any(feeler.hit().face_flags_ & render::FaceFlag::kVert))
        return false;
    return !(std::fabs(static_cast<double>(feeler.surface_dir().y)) > kWallNormalY);
}

} // namespace

// The time in the air: in the air or on a wall (states 1, 2) since it began, else the last one's length.
uint32_t SkaterPhysicsModule::time_aloft()
{
    const int state = state_->cur_phase();
    if (state == 1 || state == 2)
        return get_since(air_start_);
    return air_end_ - air_start_;
}

// The feeler of IsFacingWall and its relatives: the unnamed distance (200), from the skater's position (raised by
// `raise` when set).
bool SkaterPhysicsModule::feel_ahead(script::Struct* params, render::ProbeRay& feeler, bool raise) const
{
    float distance = kFacingDistance;
    params->fetch_float(0u, &distance, 0);
    // (the feeler made by the caller - 0x00417760 here in the original)
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& position = object->at_point();
    feeler.line().start = position;
    if (raise)
        feeler.line().start.y = static_cast<float>(static_cast<double>(position.y) + kVertFeelerRaise);
    feeler.line().end = feeler.line().start;
    feel_along_facing(feeler, object, distance);
    return feeler.get_hit_shape(true, false) != 0;
}

// 0x004FBE80
void SkaterPhysicsModule::publish_method_table()
{
    script::register_member_function_table(core_member_functions());
}

// 0x004FBEA0
int SkaterPhysicsModule::air_time_threshold(script::Struct* params)
{
    float value = 0.0f;
    params->fetch_float(0u, &value, 0);
    if (params->has_bare_flag(kSeconds) || params->has_bare_flag(kSecond))
        return static_cast<int>(ftol(static_cast<double>(value) * kMillisecondsPerSecond));
    if (params->has_bare_flag(kFrames) || params->has_bare_flag(kFrame))
        return static_cast<int>(ftol(static_cast<double>(value) * kMillisecondsPerFrame));
    return static_cast<int>(ftol(value));
}

// --- the component's own bytes ---

// 0x004FBF40
int SkaterPhysicsModule::can_brake_off(script::Struct*, script::Script*)
{
    can_brake_ = 0;
    return 1;
}
// 0x004FBF50
int SkaterPhysicsModule::can_brake_on(script::Struct*, script::Script*)
{
    can_brake_ = 1;
    return 1;
}
// 0x004FBF60
int SkaterPhysicsModule::can_kick_on(script::Struct*, script::Script*)
{
    no_kick_ = 0;
    return 1;
}
// 0x004FBF70
int SkaterPhysicsModule::can_kick_off(script::Struct*, script::Script*)
{
    no_kick_ = 1;
    return 1;
}
// 0x004FBF80
int SkaterPhysicsModule::force_auto_kick_on(script::Struct*, script::Script*)
{
    auto_kick_ = 1;
    return 1;
}
// 0x004FBF90
int SkaterPhysicsModule::force_auto_kick_off(script::Struct*, script::Script*)
{
    auto_kick_ = 0;
    return 1;
}
// 0x004FBFA0
int SkaterPhysicsModule::auto_kick_is_off(script::Struct*, script::Script*) const { return auto_kick_ ? 0 : 1; }
// 0x004FBFB0
int SkaterPhysicsModule::skater_vehicle_on(script::Struct*, script::Script*)
{
    on_vehicle_ = 1;
    return 1;
}
// 0x004FBFC0
int SkaterPhysicsModule::skater_vehicle_off(script::Struct*, script::Script*)
{
    on_vehicle_ = 0;
    return 1;
}
// 0x004FBFD0
int SkaterPhysicsModule::is_skater_on_vehicle(script::Struct*, script::Script*) const { return get_is(on_vehicle_); }
// 0x004FC000
int SkaterPhysicsModule::can_spin(script::Struct*, script::Script*)
{
    no_spin_ = 0;
    return 1;
}
// 0x004FC010
int SkaterPhysicsModule::bail_on(script::Struct*, script::Script*)
{
    bail_on_ = 1;
    return 1;
}
// 0x004FC020
int SkaterPhysicsModule::bail_off(script::Struct*, script::Script*)
{
    bail_on_ = 0;
    return 1;
}
// 0x004FC030
int SkaterPhysicsModule::bail_is_on(script::Struct*, script::Script*) const { return get_is(bail_on_); }
// 0x004FC080
int SkaterPhysicsModule::bad_ledge(script::Struct*, script::Script*) const { return get_is(bad_ledge_); }
// 0x004FC090
int SkaterPhysicsModule::came_down_off_spine(script::Struct*, script::Script*) const
{
    return get_is(came_down_off_spine_);
}
// 0x004FC0A0
int SkaterPhysicsModule::landed_from_vert(script::Struct*, script::Script*) const { return get_is(landed_from_vert_); }
// 0x004FC0B0
int SkaterPhysicsModule::set_landed_from_vert(script::Struct*, script::Script*)
{
    landed_from_vert_ = 1;
    return 1;
}
// 0x004FC0C0
int SkaterPhysicsModule::reset_landed_from_vert(script::Struct*, script::Script*)
{
    landed_from_vert_ = 0;
    return 1;
}
// 0x004FC0D0
int SkaterPhysicsModule::allow_lip_no_grind(script::Struct*, script::Script*)
{
    allow_lip_no_grind_ = 1;
    return 1;
}
// 0x004FC0E0
int SkaterPhysicsModule::clear_allow_lip_no_grind(script::Struct*, script::Script*)
{
    allow_lip_no_grind_ = 0;
    return 1;
}
// 0x004FC0F0
int SkaterPhysicsModule::no_rail_tricks(script::Struct*, script::Script*)
{
    no_rail_tricks_ = 1;
    return 1;
}
// 0x004FC100
int SkaterPhysicsModule::allow_rail_tricks(script::Struct*, script::Script*)
{
    no_rail_tricks_ = 0;
    return 1;
}
// 0x004FC1A0
int SkaterPhysicsModule::in_revert(script::Struct*, script::Script*)
{
    in_revert_ = 1;
    return 1;
}
// 0x004FC1B0
int SkaterPhysicsModule::out_revert(script::Struct*, script::Script*)
{
    in_revert_ = 0;
    return 1;
}
// 0x004FC1C0
int SkaterPhysicsModule::is_in_revert(script::Struct*, script::Script*) const { return get_is(in_revert_); }
// 0x004FC1D0
int SkaterPhysicsModule::projectiles_on(script::Struct*, script::Script*)
{
    projectiles_on_ = 1;
    return 1;
}
// 0x004FC1E0
int SkaterPhysicsModule::projectiles_off(script::Struct*, script::Script*)
{
    projectiles_on_ = 0;
    return 1;
}
// 0x004FC1F0
int SkaterPhysicsModule::focus_on(script::Struct*, script::Script*)
{
    focus_on_ = 1;
    return 1;
}
// 0x004FC200
int SkaterPhysicsModule::is_skater_focused(script::Struct*, script::Script*) const { return get_is(focus_on_); }
// 0x004FC210
int SkaterPhysicsModule::reset_projectile_input(script::Struct*, script::Script*)
{
    projectile_input_ = -1;
    return 1;
}
// 0x004FC230
int SkaterPhysicsModule::enter_powerslide(script::Struct*, script::Script*)
{
    powerslide_ = 1;
    return 1;
}
// 0x004FC240
int SkaterPhysicsModule::exit_powerslide(script::Struct*, script::Script*)
{
    powerslide_ = 0;
    return 1;
}
// 0x004FC250
int SkaterPhysicsModule::moto_skateboard_on(script::Struct*, script::Script*)
{
    motoskateboard_ = 1;
    return 1;
}
// 0x004FC260
int SkaterPhysicsModule::moto_skateboard_off(script::Struct*, script::Script*)
{
    motoskateboard_ = 0;
    return 1;
}
// 0x004FC270
int SkaterPhysicsModule::is_on_motoskateboard(script::Struct*, script::Script*) const
{
    return get_is(motoskateboard_);
}
// 0x004FC280
int SkaterPhysicsModule::skater_is_flipping(script::Struct*, script::Script*) const { return get_is(model_rotating_); }
// 0x004FC290
int SkaterPhysicsModule::reset_is_flipping(script::Struct*, script::Script*)
{
    model_rotating_ = 0;
    return 1;
}
// 0x004FC2A0
int SkaterPhysicsModule::set_throwing_projectile(script::Struct*, script::Script*)
{
    throwing_projectile_ = 1;
    return 1;
}
// 0x004FC2B0
int SkaterPhysicsModule::unset_throwing_projectile(script::Struct*, script::Script*)
{
    throwing_projectile_ = 0;
    return 1;
}
// 0x004FC2C0
int SkaterPhysicsModule::is_throwing_projectile(script::Struct*, script::Script*) const
{
    return get_is(throwing_projectile_);
}
// 0x004FC2D0
int SkaterPhysicsModule::end_manual_turnaround(script::Struct*, script::Script*)
{
    manual_turnaround_ = 0;
    return 1;
}
// 0x004FC2E0
int SkaterPhysicsModule::enter_tantrum(script::Struct*, script::Script*)
{
    tantrum_ = 1;
    return 1;
}
// 0x004FC2F0
int SkaterPhysicsModule::exit_tantrum(script::Struct*, script::Script*)
{
    tantrum_ = 0;
    return 1;
}
// 0x004FC300
int SkaterPhysicsModule::in_tantrum(script::Struct*, script::Script*) const { return get_is(tantrum_); }
// 0x004FC310
int SkaterPhysicsModule::enter_tagging(script::Struct*, script::Script*)
{
    tagging_ = 1;
    return 1;
}
// 0x004FC320
int SkaterPhysicsModule::exit_tagging(script::Struct*, script::Script*)
{
    tagging_ = 0;
    return 1;
}
// 0x004FC330
int SkaterPhysicsModule::is_tagging(script::Struct*, script::Script*) const { return get_is(tagging_); }
// 0x004FC9B0
int SkaterPhysicsModule::get_braking(script::Struct*, script::Script*) const { return get_is(braking_); }

// --- the timed flags ---

// 0x004FC780
int SkaterPhysicsModule::get_flipped(script::Struct*, script::Script*)
{
    return get_is(state_->flag(PoseFlag::kFlipped).on);
}
// 0x004FC7A0
int SkaterPhysicsModule::get_switched(script::Struct*, script::Script*)
{
    const bool flipped = state_->flag(PoseFlag::kFlipped).on != 0;
    if (core::down_cast<objects::Rider>(entity())->goofy())
        return flipped ? 1 : 0;
    return flipped ? 0 : 1;
}
// 0x004FC7E0
int SkaterPhysicsModule::get_crouched(script::Struct*, script::Script*)
{
    return get_is(state_->flag(PoseFlag::kCrouched).on);
}
// 0x004FC800
int SkaterPhysicsModule::first_time_on_this_rail(script::Struct*, script::Script*)
{
    return get_is(state_->flag(PoseFlag::kFirstTimeOnRail).on);
}
// 0x004FC820
int SkaterPhysicsModule::skitching(script::Struct*, script::Script*)
{
    return get_is(state_->flag(PoseFlag::kSkitching).on);
}
// 0x004FC840
int SkaterPhysicsModule::cancel_wallpush(script::Struct*, script::Script*)
{
    set_timed_flag(this, PoseFlag::kCancelWallpush, 1);
    return 1;
}
// 0x004FCF90
int SkaterPhysicsModule::no_spin(script::Struct*, script::Script*)
{
    no_spin_ = 1;
    set_timed_flag(this, PoseFlag::kSpin, 0);
    return 1;
}
// 0x004FCFC0
int SkaterPhysicsModule::in_bail(script::Struct*, script::Script*)
{
    set_timed_flag(this, PoseFlag::kBailing, 1);
    return 1;
}
// 0x004FCFF0
int SkaterPhysicsModule::not_in_bail(script::Struct*, script::Script*)
{
    set_timed_flag(this, PoseFlag::kBailing, 0);
    return 1;
}
// 0x004FD020
int SkaterPhysicsModule::is_in_bail(script::Struct*, script::Script*)
{
    return get_is(state_->flag(PoseFlag::kBailing).on);
}
// 0x004FD1C0
int SkaterPhysicsModule::in_vert_air(script::Struct*, script::Script*)
{
    return get_is(state_->flag(PoseFlag::kVertAir).on);
}
// 0x004FD530
int SkaterPhysicsModule::override_cancel_ground(script::Struct* params, script::Script*)
{
    set_timed_flag(this, PoseFlag::kCancelGround, params->has_bare_flag(kOff) ? 0 : 1);
    return 1;
}
// 0x004FD630
int SkaterPhysicsModule::set_vert_air(script::Struct*, script::Script*)
{
    set_timed_flag(this, PoseFlag::kVertAir, 1);
    return 1;
}

// --- values from the parameters ---

// 0x004FBFE0
int SkaterPhysicsModule::set_skater_vehicle(script::Struct* params, script::Script*)
{
    params->read_hash_value(0u, &vehicle_, 1);
    return 1;
}
// 0x004FC040
int SkaterPhysicsModule::lock_velocity_direction(script::Struct* params, script::Script*)
{
    lock_velocity_ = params->has_bare_flag(kOn);
    return 1;
}
// 0x004FC060
int SkaterPhysicsModule::change_rail_adjust(script::Struct* params, script::Script*)
{
    params->fetch_int_value(0u, &rail_adjust_, 0);
    return 1;
}
// 0x004FC180
int SkaterPhysicsModule::set_ammo_type(script::Struct* params, script::Script*)
{
    params->read_hash_value(0u, &ammo_type_, 0);
    return 1;
}
// 0x004FC110
int SkaterPhysicsModule::set_extra_push(script::Struct* params, script::Script*)
{
    float* radius = &side_reach_;
    float* speed = &side_push_;
    float* third = &side_turn_;
    *radius = kProbeRadius;
    *speed = 100.0f;
    *third = kProbeThird;
    params->fetch_float(kRadius, radius, 0);
    params->fetch_float(kSpeed, speed, 0);
    params->fetch_float(kExtraPushThird, third, 0);
    return 1;
}
// 0x004FC340
int SkaterPhysicsModule::doing_flip(script::Struct*, script::Script*) const
{
    if (model_rotating_ && model_rotation_left_ > kFlipLeftMin)
        return 1;
    return 0;
}

// --- the air time ---

// 0x004FC880
int SkaterPhysicsModule::air_time_less_than(script::Struct* params, script::Script*)
{
    const uint32_t time = time_aloft();
    return time < ints::as_unsigned(air_time_threshold(params)) ? 1 : 0;
}
// 0x004FC8E0
int SkaterPhysicsModule::air_time_greater_than(script::Struct* params, script::Script*)
{
    const uint32_t time = time_aloft();
    return ints::as_unsigned(air_time_threshold(params)) < time ? 1 : 0;
}
// 0x004FC940
int SkaterPhysicsModule::get_airtime(script::Struct*, script::Script* script)
{
    const uint32_t time = time_aloft();
    auto seconds = static_cast<float>(static_cast<double>(time) * kSecondsPerMillisecond);
    script->args().get()->push_float(checksums::Name{kAirtime}, seconds);
    return 1;
}

// 0x004FC9C0
int SkaterPhysicsModule::restore_auto_kick(script::Struct*, script::Script*)
{
    const int profile = core::down_cast<objects::Rider>(entity())->view_slot();
    auto_kick_ = core::at(modules::Skate::sole()->participants(), profile).auto_kick_;
    return 1;
}

// 0x004FC9F0
int SkaterPhysicsModule::do_car_plant_boost(script::Struct*, script::Script*)
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4& velocity = object->vel();
    float upward = velocity.y;
    upward = std::max(upward, 0.0f);
    velocity.y = upward;
    velocity.y =
        static_cast<float>(static_cast<double>(tuning_float_lookup(kCarPlantUpwardBoost, object)) + velocity.y);
    // Along the horizontal velocity; slower than 10: the facing; that barely anything either: (1, 0, 1).
    maths::Vec4 direction(velocity.x, 0.0f, velocity.z, velocity.w);
    if (static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x < kSlowSquared) {
        const maths::Vec4& facing = core::at(object->basis_frame().row, 2);
        direction = maths::Vec4(facing.x, 0.0f, facing.z, facing.w);
        const double flat = static_cast<double>(facing.z) * facing.z + static_cast<double>(facing.x) * facing.x;
        if (flat < kStillSquared)
            direction = maths::Vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    const double length =
        std::sqrt(static_cast<double>(direction.z) * direction.z + static_cast<double>(direction.x) * direction.x);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        direction.x = static_cast<float>(direction.x * inverse);
        direction.y = static_cast<float>(0.0 * inverse);
        direction.z = static_cast<float>(direction.z * inverse);
    }
    const double boost = tuning_float_lookup(kCarPlantForwardBoost, object);
    auto y = static_cast<float>(direction.y * boost);
    auto z = static_cast<float>(direction.z * boost);
    auto w = static_cast<float>(direction.w * boost);
    maths::Vec4& v = entity()->vel();
    v.x = static_cast<float>(v.x + direction.x * boost);
    v.y += y;
    v.z += z;
    v.w += w;
    return 1;
}

// 0x004FD040
int SkaterPhysicsModule::set_rail_sound(script::Struct* params, script::Script*)
{
    uint32_t sound = 0;
    params->read_hash_value(0u, &sound, 1);
    const bool slide = sound == kSlide;
    set_timed_flag(this, PoseFlag::kSlide, slide ? 1 : 0);
    sound_->set_sliding(slide ? 1 : 0);
    return 1;
}

// 0x004FD0D0
int SkaterPhysicsModule::change_wheel_drag(script::Struct* params, script::Script*)
{
    objects::GameEntity* object = object_of(this);
    float* friction = &rolling_friction_;
    params->fetch_float(0u, friction, 0);
    if (params->has_bare_flag(kDefault) && special_friction_time_ == 0.0f)
        *friction = tuning_float_lookup(kCoastDrag, object);
    return 1;
}

// 0x004FD140
int SkaterPhysicsModule::last_spin_was(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kBackside)) {
        const bool flipped = state_->flag(PoseFlag::kFlipped).on != 0;
        const bool backside = last_spin_backside_ != 0;
        return (flipped ? !backside : backside) ? 1 : 0;
    }
    if (params->has_bare_flag(kFrontside)) {
        const bool flipped = state_->flag(PoseFlag::kFlipped).on != 0;
        const bool backside = last_spin_backside_ != 0;
        return (flipped ? backside : !backside) ? 1 : 0;
    }
    return 1;
}

// 0x004FD1E0
int SkaterPhysicsModule::turn_to_face_velocity(script::Struct*, script::Script*)
{
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& velocity = object->vel();
    const double x = velocity.x;
    const double y = velocity.y;
    const double z = velocity.z;
    const double length_squared = (x * x + y * y) + z * z;
    if (length_squared < kStillSquared)
        return 1;
    maths::Vec4& right = core::at(object->basis_frame().row, 0);
    maths::Vec4& up = core::at(object->basis_frame().row, 1);
    maths::Vec4& facing = core::at(object->basis_frame().row, 2);
    facing = object->vel();
    facing.make_unit();
    // right = up x facing
    const double rx = static_cast<double>(facing.z) * up.y - static_cast<double>(facing.y) * up.z;
    const double ry = static_cast<double>(facing.x) * up.z - static_cast<double>(facing.z) * up.x;
    right.w = 0.0f;
    auto rz = static_cast<float>(static_cast<double>(facing.y) * up.x - static_cast<double>(facing.x) * up.y);
    right.z = rz;
    right.x = static_cast<float>(rx);
    right.y = static_cast<float>(ry);
    right.make_unit();
    // up = facing x right
    const double ux = static_cast<double>(right.z) * facing.y - static_cast<double>(right.y) * facing.z;
    const double uy = static_cast<double>(right.x) * facing.z - static_cast<double>(right.z) * facing.x;
    up.w = 0.0f;
    auto uz = static_cast<float>(static_cast<double>(right.y) * facing.x - static_cast<double>(right.x) * facing.y);
    up.z = uz;
    up.x = static_cast<float>(ux);
    up.y = static_cast<float>(uy);
    up.make_unit();
    display_matrix_ = entity()->basis_frame();
    return 1;
}

// 0x004FD360
int SkaterPhysicsModule::override_limits(script::Struct* params, script::Script*)
{
    objects::GameEntity* object = object_of(this);
    float& time = limits_time_;
    if (params->has_bare_flag(kCancelLimits)) {
        time = 0.0f;
        return 1;
    }
    if (limits_time_ == -kTimerForLevel) // for the level
        return 1;
    if (params->has_bare_flag(kEnd)) {
        time = 0.0f;
        return 1;
    }
    float* max = &limits_max_;
    params->fetch_float(kMax, max, 1);
    float* max_max = &limits_max_max_;
    limits_max_max_ = *max;
    params->fetch_float(kMaxMax, max_max, 0);
    limits_time_ = std::bit_cast<float>(kLimitsTimeDefault);
    params->fetch_float(kTime, &time, 0);
    float* friction = &limits_friction_;
    limits_friction_ = std::bit_cast<float>(kLimitsFrictionDefault);
    params->fetch_float(kFriction, friction, 0);
    float* gravity = &limits_gravity_;
    *gravity = tuning_float_lookup(kGroundGravity, object);
    params->fetch_float(kGravity, gravity, 0);
    if (params->has_bare_flag(kUntilCancelled))
        time = -1.0f;
    if (params->has_bare_flag(kCurrentLevel))
        time = -kTimerForLevel;
    return 1;
}

// 0x004FD4A0
int SkaterPhysicsModule::set_special_friction(script::Struct* params, script::Script*)
{
    script::Array* frictions = nullptr;
    params->fetch_list(0u, &frictions, 0);
    int& index = special_friction_index_;
    const int count = static_cast<int>(frictions->extent()); // (no array: the original reads through null too)
    if (index >= count)
        index = count - 1;
    rolling_friction_ = frictions->fetch_float(static_cast<uint32_t>(index));
    if (index == 0)
        special_friction_start_ = get_now();
    ++index;
    params->fetch_float(kDuration, &special_friction_time_, 0);
    return 1;
}

// --- results for the script ---

// 0x004FD580
int SkaterPhysicsModule::get_matrix_normal(script::Struct*, script::Script* script)
{
    script->args().get()->push_float(checksums::Name{kX}, core::at(entity()->basis_frame().row, 1).x);
    script->args().get()->push_float(checksums::Name{kY}, core::at(entity()->basis_frame().row, 1).y);
    script->args().get()->push_float(checksums::Name{kZ}, core::at(entity()->basis_frame().row, 1).z);
    return 1;
}
// 0x004FD5E0
int SkaterPhysicsModule::get_last_ground_pos(script::Struct*, script::Script* script)
{
    const maths::Vec4& position = last_ground_pos_;
    script->args().get()->put_vec3(checksums::Name{kPos}, {.x = position.x, .y = position.y, .z = position.z});
    return 1;
}
// 0x004FD610
int SkaterPhysicsModule::get_last_ground_heading(script::Struct*, script::Script* script) const
{
    script->args().get()->push_float(checksums::Name{kLastHeading}, last_heading_);
    return 1;
}
// 0x004FDC30
int SkaterPhysicsModule::get_ammo(script::Struct*, script::Script* script) const
{
    script->args().get()->put_int(checksums::Name{kAmmo}, ammo_);
    return 1;
}
// 0x004FDC50
int SkaterPhysicsModule::get_ammo_type(script::Struct*, script::Script* script) const
{
    script->args().get()->put_name_key(checksums::Name{kAmmoType}, ammo_type_);
    return 1;
}
// 0x004FDC70
int SkaterPhysicsModule::get_pitch_bail_dir(script::Struct*, script::Script* script) const
{
    script->args().get()->put_name_key(checksums::Name{kPitchBailDir}, nearest_wall_side_);
    return 1;
}

// --- feelers ahead ---

// 0x004FD660
int SkaterPhysicsModule::is_facing_wall(script::Struct* params, script::Script*)
{
    render::ProbeRay feeler;
    if (feel_ahead(params, feeler, false) && hit_wall(feeler))
        return 1;
    return 0;
}

// 0x004FD840
int SkaterPhysicsModule::turn_from_wall(script::Struct* params, script::Script*)
{
    render::ProbeRay feeler;
    if (!feel_ahead(params, feeler, false) || !hit_wall(feeler))
        return 0;
    entity()->vel().change_heading(feeler.surface_dir());
    core::at(entity()->basis_frame().row, 2).change_heading(feeler.surface_dir());
    core::down_cast<objects::RiderSpinAndFlip>(
        static_cast<objects::GameEntity*>(object_of(this))->fetch_module(kSkaterFlipAndRotate))
        ->get_flip();
    display_matrix_ = entity()->basis_frame();
    return 1;
}

// 0x004FDA80
int SkaterPhysicsModule::is_facing_vert(script::Struct* params, script::Script*)
{
    render::ProbeRay feeler;
    if (feel_ahead(params, feeler, true) && (core::any(feeler.hit().face_flags_ & render::FaceFlag::kVert)))
        return 1;
    return 0;
}

// --- focus ---

// 0x004FDC90
int SkaterPhysicsModule::focus_off(script::Struct*, script::Script*)
{
    focus_on_ = 0;
    auto* camera =
        core::down_cast<objects::GameEntity>(core::down_cast<objects::Rider>(object_of(this))->get_skater_camera());
    core::down_cast<objects::FreeLookModule>(camera->fetch_module(kCameraLookAround))->set_locked(false);
    return 1;
}
// 0x004FDCC0
int SkaterPhysicsModule::get_focus_time_left(script::Struct*, script::Script* script) const
{
    auto* score = core::down_cast<objects::Rider>(object_of(this))->get_points_total();
    if (!score)
        return 0;
    script->args().get()->push_float(checksums::Name{kFocusTime}, score->focus_time_limit());
    return 1;
}
// 0x004FDCF0
int SkaterPhysicsModule::get_focus_times_allowed(script::Struct*, script::Script* script) const
{
    auto* score = core::down_cast<objects::Rider>(object_of(this))->get_points_total();
    if (!score)
        return 0;
    script->args().get()->put_int(checksums::Name{kFocusAllowed}, score->focus_max_times());
    return 1;
}
// 0x004FDD20
int SkaterPhysicsModule::set_focus_times_allowed(script::Struct* params, script::Script*) const
{
    auto* score = core::down_cast<objects::Rider>(object_of(this))->get_points_total();
    if (!score)
        return 0;
    params->fetch_int_value(0u, &score->focus_max_times(), 0);
    return 1;
}

} // namespace skater

