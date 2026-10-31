// The skater core physics component's script member functions (its table at 0x006826C8): the flags and values
// scripts set and ask about, and a few small actions (0x004FBE80-0x004FDD47).

#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
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

namespace skater {

uint32_t g_core_member_functions[660]{
    0x584CF9E9, 0x5BD63B29, 0, 0, 0, 0, 0x0C7A712C, 0x5BD63B29, 0, 0, 0, 0, 0x8F66B80B, 0x5BD63B29, 0, 0, 0, 0,
    0x4ADC6C2A, 0x5BD63B29, 0, 0, 0, 0, 0x262D42D5, 0x5BD63B29, 0, 0, 0, 0, 0xCA6B3809, 0x5BD63B29, 0, 0, 0, 0,
    0x9C6A7E41, 0x5BD63B29, 0, 0, 0, 0, 0x32DCC9CF, 0x5BD63B29, 0, 0, 0, 0, 0x5E8F9E77, 0x5BD63B29, 0, 0, 0, 0,
    0x0C890A84, 0x5BD63B29, 0, 0, 0, 0, 0xBBF2B570, 0x5BD63B29, 0, 0, 0, 0, 0xDE583E34, 0x5BD63B29, 0, 0, 0, 0,
    0x1996B797, 0x5BD63B29, 0, 0, 0, 0, 0x1F8BBD05, 0x5BD63B29, 0, 0, 0, 0, 0xEBBCF455, 0x5BD63B29, 0, 0, 0, 0,
    0xBC19E291, 0x5BD63B29, 0, 0, 0, 0, 0x02F66333, 0x5BD63B29, 0, 0, 0, 0, 0x68BF6C13, 0x5BD63B29, 0, 0, 0, 0,
    0xE8DEB0D7, 0x5BD63B29, 0, 0, 0, 0, 0x34DCFC97, 0x5BD63B29, 0, 0, 0, 0, 0x0257947E, 0x5BD63B29, 0, 0, 0, 0,
    0x01BAA1D9, 0x5BD63B29, 0, 0, 0, 0, 0x9FCFDFEB, 0x5BD63B29, 0, 0, 0, 0, 0xFEFF11CC, 0x5BD63B29, 0, 0, 0, 0,
    0xFE243FEF, 0x5BD63B29, 0, 0, 0, 0, 0xE168CFE0, 0x5BD63B29, 0, 0, 0, 0, 0x29FBA9DF, 0x5BD63B29, 0, 0, 0, 0,
    0x17846595, 0x5BD63B29, 0, 0, 0, 0, 0x259CB90D, 0x5BD63B29, 0, 0, 0, 0, 0x1D1FD4F0, 0x5BD63B29, 0, 0, 0, 0,
    0x383B939B, 0x5BD63B29, 0, 0, 0, 0, 0x54EF2B79, 0x5BD63B29, 0, 0, 0, 0, 0x0E2998B9, 0x5BD63B29, 0, 0, 0, 0,
    0x6FC5AAE0, 0x5BD63B29, 0, 0, 0, 0, 0xBD4303F4, 0x5BD63B29, 0, 0, 0, 0, 0xA901A50C, 0x5BD63B29, 0, 0, 0, 0,
    0xE0DF1F0A, 0x5BD63B29, 0, 0, 0, 0, 0x8C3D7864, 0x5BD63B29, 0, 0, 0, 0, 0x0E1D5168, 0x5BD63B29, 0, 0, 0, 0,
    0x948EBF96, 0x5BD63B29, 0, 0, 0, 0, 0x947FCCF4, 0x5BD63B29, 0, 0, 0, 0, 0xACB82C02, 0x5BD63B29, 0, 0, 0, 0,
    0x510F983B, 0x5BD63B29, 0, 0, 0, 0, 0x71B993B7, 0x5BD63B29, 0, 0, 0, 0, 0xBE371B7B, 0x5BD63B29, 0, 0, 0, 0,
    0x55934543, 0x5BD63B29, 0, 0, 0, 0, 0xB424244D, 0x5BD63B29, 0, 0, 0, 0, 0x224CDDC9, 0x5BD63B29, 0, 0, 0, 0,
    0x6C8A1316, 0x5BD63B29, 0, 0, 0, 0, 0x448E3630, 0x5BD63B29, 0, 0, 0, 0, 0xF37CE040, 0x5BD63B29, 0, 0, 0, 0,
    0x133E1293, 0x5BD63B29, 0, 0, 0, 0, 0x0F922431, 0x5BD63B29, 0, 0, 0, 0, 0xDFB8F052, 0x5BD63B29, 0, 0, 0, 0,
    0xC5B4B390, 0x5BD63B29, 0, 0, 0, 0, 0xB9EEBFF6, 0x5BD63B29, 0, 0, 0, 0, 0xEC681A59, 0x5BD63B29, 0, 0, 0, 0,
    0x065A559A, 0x5BD63B29, 0, 0, 0, 0, 0x461E5B92, 0x5BD63B29, 0, 0, 0, 0, 0xA14A467F, 0x5BD63B29, 0, 0, 0, 0,
    0x90F1D8D5, 0x5BD63B29, 0, 0, 0, 0, 0x5E8D9B80, 0x5BD63B29, 0, 0, 0, 0, 0xB94BC0C6, 0x5BD63B29, 0, 0, 0, 0,
    0xE47FF4B5, 0x5BD63B29, 0, 0, 0, 0, 0x05144783, 0x5BD63B29, 0, 0, 0, 0, 0xF7E21884, 0x5BD63B29, 0, 0, 0, 0,
    0x0BFF3271, 0x5BD63B29, 0, 0, 0, 0, 0x2A547E37, 0x5BD63B29, 0, 0, 0, 0, 0xAAE7E1D8, 0x5BD63B29, 0, 0, 0, 0,
    0xA038260D, 0x5BD63B29, 0, 0, 0, 0, 0xD27F89E8, 0x5BD63B29, 0, 0, 0, 0, 0x25B393E7, 0x5BD63B29, 0, 0, 0, 0,
    0xB6222B3A, 0x5BD63B29, 0, 0, 0, 0, 0x9D8BDE21, 0x5BD63B29, 0, 0, 0, 0, 0x83A46968, 0x5BD63B29, 0, 0, 0, 0,
    0x6A0834C5, 0x5BD63B29, 0, 0, 0, 0, 0x675CF35E, 0x5BD63B29, 0, 0, 0, 0, 0xD3E1A093, 0x5BD63B29, 0, 0, 0, 0,
    0x3BAAC2C5, 0x5BD63B29, 0, 0, 0, 0, 0xB195C726, 0x5BD63B29, 0, 0, 0, 0, 0xB649958F, 0x5BD63B29, 0, 0, 0, 0,
    0x11B99941, 0x5BD63B29, 0, 0, 0, 0, 0xEFFC54DC, 0x5BD63B29, 0, 0, 0, 0, 0xE3822CCE, 0x5BD63B29, 0, 0, 0, 0,
    0xD5BCAB93, 0x5BD63B29, 0, 0, 0, 0, 0x9AF4A4C2, 0x5BD63B29, 0, 0, 0, 0, 0x8A8EBE72, 0x5BD63B29, 0, 0, 0, 0,
    0xC5623AEE, 0x5BD63B29, 0, 0, 0, 0, 0xD7ECF37C, 0x5BD63B29, 0, 0, 0, 0, 0x43A7516D, 0x5BD63B29, 0, 0, 0, 0,
    0x5F48F4D1, 0x5BD63B29, 0, 0, 0, 0, 0x5F43DDF3, 0x5BD63B29, 0, 0, 0, 0, 0x68F6DB80, 0x5BD63B29, 0, 0, 0, 0,
    0x1B4A5FF9, 0x5BD63B29, 0, 0, 0, 0, 0x007CDADB, 0x5BD63B29, 0, 0, 0, 0, 0x27C92E33, 0x5BD63B29, 0, 0, 0, 0,
    0x8476CA5B, 0x5BD63B29, 0, 0, 0, 0, 0xF900547B, 0x5BD63B29, 0, 0, 0, 0, 0xFC9E2886, 0x5BD63B29, 0, 0, 0, 0,
    0x3981FBF0, 0x5BD63B29, 0, 0, 0, 0, 0x821BF006, 0x5BD63B29, 0, 0, 0, 0, 0x2C30B07A, 0x5BD63B29, 0, 0, 0, 0,
    0x1EB4121A, 0x5BD63B29, 0, 0, 0, 0, 0xA751E3D8, 0x5BD63B29, 0, 0, 0, 0, 0x03B9946F, 0x5BD63B29, 0, 0, 0, 0,
    0xADCA7EE6, 0x5BD63B29, 0, 0, 0, 0, 0x0898E60F, 0x5BD63B29, 0, 0, 0, 0, 0xB17D17CD, 0x5BD63B29, 0, 0, 0, 0,
    0xF22B4480, 0x5BD63B29, 0, 0, 0, 0, 0,          0x5BD63B29, 0, 0, 0, 0};

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
script::Struct* results_of(script::Script* script) { return script->args_; }

// The time in the air: in the air or on a wall (states 1, 2) since it began, else the last one's length.
uint32_t time_aloft(SkaterPhysicsModule* component)
{
    int state = get_at<int>(flags_of(component), kState);
    if (state == 1 || state == 2)
        return get_since(component->air_start_);
    return component->air_end_ - component->air_start_;
}

// The skater's facing, made unit (the x and y products left unrounded as the original keeps them on the x87 stack),
// as the end of a feeler from `start` `distance` long.
void feel_along_facing(render::ProbeRay& feeler, objects::GameEntity* object, float distance)
{
    const maths::Vec4& facing = object->basis_frame_.row[2];
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
    maths::Vec4& start = feeler.line_.start;
    maths::Vec4& end = feeler.line_.end;
    end.x = static_cast<float>(x * distance + start.x);
    end.y = static_cast<float>(y * distance + start.y);
    end.z = static_cast<float>(static_cast<double>(z) * distance + start.z);
}

// The feeler of IsFacingWall and its relatives: the unnamed distance (200), from the skater's position (raised by
// `raise` when set).
bool feel_ahead(SkaterPhysicsModule* component, script::Struct* params, render::ProbeRay& feeler, bool raise)
{
    float distance = kFacingDistance;
    params->fetch_float(0u, &distance, 0);
    feeler.make_new();
    objects::GameEntity* object = object_of(component);
    const maths::Vec4& position = object->at_point_;
    feeler.line_.start = position;
    if (raise)
        feeler.line_.start.y = static_cast<float>(static_cast<double>(position.y) + kVertFeelerRaise);
    feeler.line_.end = feeler.line_.start;
    feel_along_facing(feeler, object, distance);
    return feeler.get_hit_shape(true, false) != 0;
}

// A wall the feeler hit: not vert, its normal's y within 0.1 of 0 (or not a number).
bool hit_wall(const render::ProbeRay& feeler)
{
    if (feeler.hit_.face_flags_ & 8)
        return false;
    return !(std::fabs(static_cast<double>(feeler.surface_dir_.y)) > kWallNormalY);
}

} // namespace

// 0x004FBE80
void __cdecl SkaterPhysicsModule::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kSkaterCorePhysics), g_core_member_functions, 0x18);
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
int SkaterPhysicsModule::auto_kick_is_off(script::Struct*, script::Script*) { return auto_kick_ ? 0 : 1; }
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
int SkaterPhysicsModule::is_skater_on_vehicle(script::Struct*, script::Script*) { return get_is(on_vehicle_); }
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
int SkaterPhysicsModule::bail_is_on(script::Struct*, script::Script*) { return get_is(bail_on_); }
// 0x004FC080
int SkaterPhysicsModule::bad_ledge(script::Struct*, script::Script*) { return get_is(bad_ledge_); }
// 0x004FC090
int SkaterPhysicsModule::came_down_off_spine(script::Struct*, script::Script*) { return get_is(came_down_off_spine_); }
// 0x004FC0A0
int SkaterPhysicsModule::landed_from_vert(script::Struct*, script::Script*) { return get_is(landed_from_vert_); }
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
int SkaterPhysicsModule::is_in_revert(script::Struct*, script::Script*) { return get_is(in_revert_); }
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
int SkaterPhysicsModule::is_skater_focused(script::Struct*, script::Script*) { return get_is(focus_on_); }
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
int SkaterPhysicsModule::is_on_motoskateboard(script::Struct*, script::Script*) { return get_is(motoskateboard_); }
// 0x004FC280
int SkaterPhysicsModule::skater_is_flipping(script::Struct*, script::Script*) { return get_is(model_rotating_); }
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
int SkaterPhysicsModule::is_throwing_projectile(script::Struct*, script::Script*)
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
int SkaterPhysicsModule::in_tantrum(script::Struct*, script::Script*) { return get_is(tantrum_); }
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
int SkaterPhysicsModule::is_tagging(script::Struct*, script::Script*) { return get_is(tagging_); }
// 0x004FC9B0
int SkaterPhysicsModule::get_braking(script::Struct*, script::Script*) { return get_is(braking_); }

// --- the timed flags ---

// 0x004FC780
int SkaterPhysicsModule::get_flipped(script::Struct*, script::Script*) { return get_is(flags_of(this)[kFlagFlipped]); }
// 0x004FC7A0
int SkaterPhysicsModule::get_switched(script::Struct*, script::Script*)
{
    bool flipped = flags_of(this)[kFlagFlipped] != 0;
    if (static_cast<objects::Rider*>(entity_)->goofy_)
        return flipped ? 1 : 0;
    return flipped ? 0 : 1;
}
// 0x004FC7E0
int SkaterPhysicsModule::get_crouched(script::Struct*, script::Script*)
{
    return get_is(flags_of(this)[kFlagCrouched]);
}
// 0x004FC800
int SkaterPhysicsModule::first_time_on_this_rail(script::Struct*, script::Script*)
{
    return get_is(flags_of(this)[kFlagFirstTimeOnRail]);
}
// 0x004FC820
int SkaterPhysicsModule::skitching(script::Struct*, script::Script*) { return get_is(flags_of(this)[kFlagSkitching]); }
// 0x004FC840
int SkaterPhysicsModule::cancel_wallpush(script::Struct*, script::Script*)
{
    set_timed_flag(this, kFlagCancelWallpush, 1);
    return 1;
}
// 0x004FCF90
int SkaterPhysicsModule::no_spin(script::Struct*, script::Script*)
{
    no_spin_ = 1;
    set_timed_flag(this, kFlagSpin, 0);
    return 1;
}
// 0x004FCFC0
int SkaterPhysicsModule::in_bail(script::Struct*, script::Script*)
{
    set_timed_flag(this, kFlagBail, 1);
    return 1;
}
// 0x004FCFF0
int SkaterPhysicsModule::not_in_bail(script::Struct*, script::Script*)
{
    set_timed_flag(this, kFlagBail, 0);
    return 1;
}
// 0x004FD020
int SkaterPhysicsModule::is_in_bail(script::Struct*, script::Script*) { return get_is(flags_of(this)[kFlagBail]); }
// 0x004FD1C0
int SkaterPhysicsModule::in_vert_air(script::Struct*, script::Script*) { return get_is(flags_of(this)[kFlagVertAir]); }
// 0x004FD530
int SkaterPhysicsModule::override_cancel_ground(script::Struct* params, script::Script*)
{
    set_timed_flag(this, kFlagCancelGround, params->has_bare_flag(kOff) ? 0 : 1);
    return 1;
}
// 0x004FD630
int SkaterPhysicsModule::set_vert_air(script::Struct*, script::Script*)
{
    set_timed_flag(this, kFlagVertAir, 1);
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
    *radius = 48.0f;
    *speed = 100.0f;
    *third = 6.0f;
    params->fetch_float(kRadius, radius, 0);
    params->fetch_float(kSpeed, speed, 0);
    params->fetch_float(kExtraPushThird, third, 0);
    return 1;
}
// 0x004FC340
int SkaterPhysicsModule::doing_flip(script::Struct*, script::Script*)
{
    if (model_rotating_ && model_rotation_left_ > kFlipLeftMin)
        return 1;
    return 0;
}

// --- the air time ---

// 0x004FC880
int SkaterPhysicsModule::air_time_less_than(script::Struct* params, script::Script*)
{
    uint32_t time = time_aloft(this);
    return time < static_cast<uint32_t>(air_time_threshold(params)) ? 1 : 0;
}
// 0x004FC8E0
int SkaterPhysicsModule::air_time_greater_than(script::Struct* params, script::Script*)
{
    uint32_t time = time_aloft(this);
    return static_cast<uint32_t>(air_time_threshold(params)) < time ? 1 : 0;
}
// 0x004FC940
int SkaterPhysicsModule::get_airtime(script::Struct*, script::Script* script)
{
    uint32_t time = time_aloft(this);
    float seconds = static_cast<float>(static_cast<double>(time) * kSecondsPerMillisecond);
    results_of(script)->push_float(kAirtime, seconds);
    return 1;
}

// 0x004FC9C0
int SkaterPhysicsModule::restore_auto_kick(script::Struct*, script::Script*)
{
    int profile = static_cast<objects::Rider*>(entity_)->view_slot_;
    uint8_t* profiles = reinterpret_cast<uint8_t*>(modules::g_skate_module);
    auto_kick_ = profiles[profile * 8 + 0x395];
    return 1;
}

// 0x004FC9F0
int SkaterPhysicsModule::do_car_plant_boost(script::Struct*, script::Script*)
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4& velocity = object->vel_;
    float upward = velocity.y;
    if (upward < 0.0f)
        upward = 0.0f;
    velocity.y = upward;
    velocity.y =
        static_cast<float>(static_cast<double>(tuning_float_lookup(kCarPlantUpwardBoost, object)) + velocity.y);
    // Along the horizontal velocity; slower than 10: the facing; that barely anything either: (1, 0, 1).
    maths::Vec4 direction(velocity.x, 0.0f, velocity.z, velocity.w);
    if (static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x < kSlowSquared) {
        const maths::Vec4& facing = object->basis_frame_.row[2];
        direction = maths::Vec4(facing.x, 0.0f, facing.z, facing.w);
        double flat = static_cast<double>(facing.z) * facing.z + static_cast<double>(facing.x) * facing.x;
        if (flat < kStillSquared)
            direction = maths::Vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    double length =
        std::sqrt(static_cast<double>(direction.z) * direction.z + static_cast<double>(direction.x) * direction.x);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        direction.x = static_cast<float>(direction.x * inverse);
        direction.y = static_cast<float>(0.0 * inverse);
        direction.z = static_cast<float>(direction.z * inverse);
    }
    double boost = tuning_float_lookup(kCarPlantForwardBoost, object);
    float y = static_cast<float>(direction.y * boost);
    float z = static_cast<float>(direction.z * boost);
    float w = static_cast<float>(direction.w * boost);
    maths::Vec4& v = entity_->vel_;
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
    bool slide = sound == kSlide;
    set_timed_flag(this, kFlagSlide, slide ? 1 : 0);
    sound_->sliding_ = slide ? 1 : 0;
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
        bool flipped = flags_of(this)[kFlagFlipped] != 0;
        bool backside = last_spin_backside_ != 0;
        return (flipped ? !backside : backside) ? 1 : 0;
    }
    if (params->has_bare_flag(kFrontside)) {
        bool flipped = flags_of(this)[kFlagFlipped] != 0;
        bool backside = last_spin_backside_ != 0;
        return (flipped ? backside : !backside) ? 1 : 0;
    }
    return 1;
}

// 0x004FD1E0
int SkaterPhysicsModule::turn_to_face_velocity(script::Struct*, script::Script*)
{
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& velocity = object->vel_;
    double x = velocity.x, y = velocity.y, z = velocity.z;
    double length_squared = (x * x + y * y) + z * z;
    if (length_squared < kStillSquared)
        return 1;
    maths::Vec4& right = object->basis_frame_.row[0];
    maths::Vec4& up = object->basis_frame_.row[1];
    maths::Vec4& facing = object->basis_frame_.row[2];
    facing = object->vel_;
    facing.make_unit();
    // right = up x facing
    double rx = static_cast<double>(facing.z) * up.y - static_cast<double>(facing.y) * up.z;
    double ry = static_cast<double>(facing.x) * up.z - static_cast<double>(facing.z) * up.x;
    right.w = 0.0f;
    float rz = static_cast<float>(static_cast<double>(facing.y) * up.x - static_cast<double>(facing.x) * up.y);
    right.z = rz;
    right.x = static_cast<float>(rx);
    right.y = static_cast<float>(ry);
    right.make_unit();
    // up = facing x right
    double ux = static_cast<double>(right.z) * facing.y - static_cast<double>(right.y) * facing.z;
    double uy = static_cast<double>(right.x) * facing.z - static_cast<double>(right.z) * facing.x;
    up.w = 0.0f;
    float uz = static_cast<float>(static_cast<double>(right.y) * facing.x - static_cast<double>(right.x) * facing.y);
    up.z = uz;
    up.x = static_cast<float>(ux);
    up.y = static_cast<float>(uy);
    up.make_unit();
    display_matrix_ = entity_->basis_frame_;
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
    if (bits_as<uint32_t>(limits_time_) == 0xC0000000u) // -2: for the level
        return 1;
    if (params->has_bare_flag(kEnd)) {
        time = 0.0f;
        return 1;
    }
    float* max = &limits_max_;
    params->fetch_float(kMax, max, 1);
    float* max_max = &limits_max_max_;
    bits_as<uint32_t>(limits_max_max_) = *reinterpret_cast<uint32_t*>(max);
    params->fetch_float(kMaxMax, max_max, 0);
    bits_as<uint32_t>(limits_time_) = kLimitsTimeDefault;
    params->fetch_float(kTime, &time, 0);
    float* friction = &limits_friction_;
    bits_as<uint32_t>(limits_friction_) = kLimitsFrictionDefault;
    params->fetch_float(kFriction, friction, 0);
    float* gravity = &limits_gravity_;
    *gravity = tuning_float_lookup(kGroundGravity, object);
    params->fetch_float(kGravity, gravity, 0);
    if (params->has_bare_flag(kUntilCancelled))
        time = -1.0f;
    if (params->has_bare_flag(kCurrentLevel))
        time = -2.0f;
    return 1;
}

// 0x004FD4A0
int SkaterPhysicsModule::set_special_friction(script::Struct* params, script::Script*)
{
    script::Array* frictions = nullptr;
    params->fetch_list(0u, &frictions, 0);
    int& index = special_friction_index_;
    int count = static_cast<int>(frictions->extent_); // (no array: the original reads through null too)
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
    results_of(script)->push_float(kX, entity_->basis_frame_.row[1].x);
    results_of(script)->push_float(kY, entity_->basis_frame_.row[1].y);
    results_of(script)->push_float(kZ, entity_->basis_frame_.row[1].z);
    return 1;
}
// 0x004FD5E0
int SkaterPhysicsModule::get_last_ground_pos(script::Struct*, script::Script* script)
{
    const maths::Vec4& position = last_ground_pos_;
    results_of(script)->put_vec3(kPos, position.x, position.y, position.z);
    return 1;
}
// 0x004FD610
int SkaterPhysicsModule::get_last_ground_heading(script::Struct*, script::Script* script)
{
    results_of(script)->push_float(kLastHeading, last_heading_);
    return 1;
}
// 0x004FDC30
int SkaterPhysicsModule::get_ammo(script::Struct*, script::Script* script)
{
    results_of(script)->put_int(kAmmo, ammo_);
    return 1;
}
// 0x004FDC50
int SkaterPhysicsModule::get_ammo_type(script::Struct*, script::Script* script)
{
    results_of(script)->put_name_key(kAmmoType, ammo_type_);
    return 1;
}
// 0x004FDC70
int SkaterPhysicsModule::get_pitch_bail_dir(script::Struct*, script::Script* script)
{
    results_of(script)->put_name_key(kPitchBailDir, nearest_wall_side_);
    return 1;
}

// --- feelers ahead ---

// 0x004FD660
int SkaterPhysicsModule::is_facing_wall(script::Struct* params, script::Script*)
{
    render::ProbeRay feeler;
    if (feel_ahead(this, params, feeler, false) && hit_wall(feeler))
        return 1;
    return 0;
}

// 0x004FD840
int SkaterPhysicsModule::turn_from_wall(script::Struct* params, script::Script*)
{
    render::ProbeRay feeler;
    if (!feel_ahead(this, params, feeler, false) || !hit_wall(feeler))
        return 0;
    entity_->vel_.change_heading(feeler.surface_dir_);
    entity_->basis_frame_.row[2].change_heading(feeler.surface_dir_);
    static_cast<objects::RiderSpinAndFlip*>(
        static_cast<objects::GameEntity*>(object_of(this))->fetch_module(kSkaterFlipAndRotate))
        ->get_flip();
    display_matrix_ = entity_->basis_frame_;
    return 1;
}

// 0x004FDA80
int SkaterPhysicsModule::is_facing_vert(script::Struct* params, script::Script*)
{
    render::ProbeRay feeler;
    if (feel_ahead(this, params, feeler, true) && (feeler.hit_.face_flags_ & 8))
        return 1;
    return 0;
}

// --- focus ---

// 0x004FDC90
int SkaterPhysicsModule::focus_off(script::Struct*, script::Script*)
{
    focus_on_ = 0;
    auto* camera =
        static_cast<objects::GameEntity*>(static_cast<objects::Rider*>(object_of(this))->get_skater_camera());
    static_cast<objects::FreeLookModule*>(camera->fetch_module(kCameraLookAround))->set_locked(false);
    return 1;
}
// 0x004FDCC0
int SkaterPhysicsModule::get_focus_time_left(script::Struct*, script::Script* script)
{
    auto* score = static_cast<uint8_t*>(static_cast<objects::Rider*>(object_of(this))->get_points_total());
    if (!score)
        return 0;
    results_of(script)->push_float(kFocusTime, get_at<float>(score, 0));
    return 1;
}
// 0x004FDCF0
int SkaterPhysicsModule::get_focus_times_allowed(script::Struct*, script::Script* script)
{
    auto* score = static_cast<uint8_t*>(static_cast<objects::Rider*>(object_of(this))->get_points_total());
    if (!score)
        return 0;
    results_of(script)->put_int(kFocusAllowed, get_at<int>(score, 4));
    return 1;
}
// 0x004FDD20
int SkaterPhysicsModule::set_focus_times_allowed(script::Struct* params, script::Script*)
{
    auto* score = static_cast<uint8_t*>(static_cast<objects::Rider*>(object_of(this))->get_points_total());
    if (!score)
        return 0;
    params->fetch_int_value(0u, &get_at<int>(score, 4), 0);
    return 1;
}

} // namespace skater

using skater::SkaterPhysicsModule;
