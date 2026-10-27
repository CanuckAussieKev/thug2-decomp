// NOT ORIGINAL - THAW's BMX ridden in THUG2, phase 3: THAW's bike branches in the skater's core physics
// (game/skater/thaw_bmx.h). Our addition at the user's request - not THUG2's code and not PartyMod's.
//
// Each function here is THAW.exe's bike branch (read from THAW's own code - the first .text, 0x401000-0x68E000;
// the obfuscated sites inside them inferred from what's around them and from THUG2's counterpart, as noted)
// re-expressed on THUG2's component layout: THAW's fields are at other offsets (its +0x9C frame length is THUG2's
// +0x8C, its pad's first sixteen buttons are THUG2's, its timed flags are THUG2's shifted by 8 from +0x68 and by
// 0x10 from +0xA8 ...). Our THUG2 functions call these only while Active(core) - the feature on and the skater on the
// bike; otherwise nothing here runs.
//
// THAW's pad has buttons THUG2's hasn't: the movement stick's four directions as buttons (LEFTUP / LEFTDOWN / LEFTLEFT
// / LEFTRIGHT, its pad +0x280 / +0x2A0 / +0x2C0 / +0x2E0, set by its PC layer). Here they're the d-pad's own buttons or
// the stick past half way (BeginFrame keeps their press times); where THAW reads the stick's analog value, a centred
// stick gives the d-pad's direction at full deflection (keyboard play).

#include <cmath>
#include <cstdint>

#include "game/core/crc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/config.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Skater::ThawBmx {

namespace {

template <typename T>
T& At(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
Mth::Vector& VectorAt(const void* object, uint32_t offset) { return At<Mth::Vector>(object, offset); }

// The core physics component's fields (THUG2's; THAW's offset in the comment).
constexpr uint32_t kObject = 0x0C;
constexpr uint32_t kDisplayMatrix = 0x18;  // (rows: +0x18 right, +0x28 up, +0x38 forward)
constexpr uint32_t kDisplayUp = 0x28;
constexpr uint32_t kDisplayForward = 0x38;
constexpr uint32_t kDisplayNormal = 0x58;  // THAW +0x68
constexpr uint32_t kGroundNormal = 0x68;   // THAW +0x78
constexpr uint32_t kOldNormal = 0x78;      // THAW +0x88 (the display normal when the ground normal changed)
constexpr uint32_t kNormalLerp = 0x88;     // THAW +0x98 (1 -> 0 over the lerp)
constexpr uint32_t kFrameLength = 0x8C;    // THAW +0x9C
constexpr uint32_t kFeeler = 0xA8;         // THAW +0xB8
constexpr uint32_t kHitFlags = 0xF8;       // the feeler's hit face's flags
constexpr uint32_t kFeelerDistance = 0x11C; // THAW +0x12C (the feeler's m_distance)
constexpr uint32_t kHitPoint = 0x120;      // THAW +0x130
constexpr uint32_t kHitNormal = 0x130;     // THAW +0x140
constexpr uint32_t kFeelerStart = 0x208;   // THAW +0x388
constexpr uint32_t kFeelerEnd = 0x218;
constexpr uint32_t kHitSkatable = 0x228;
constexpr uint32_t kHitVert = 0x229;
constexpr uint32_t kCrouchedKick = 0x303;  // THAW +0x567
constexpr uint32_t kNoKick = 0x310;        // THAW +0x674
constexpr uint32_t kMayBrake = 0x311;      // THAW +0x675
constexpr uint32_t kBraking = 0x324;       // THAW +0x688
constexpr uint32_t kTurnVelocityToo = 0x388; // THAW +0x73C (set: the velocity isn't turned with the skater)
constexpr uint32_t kSpinningLeft = 0x3A5;  // THAW +0x759
constexpr uint32_t kInput = 0x150C;        // THAW +0x1910 (the pad at its +0x20)
constexpr uint32_t kSkaterComponent = 0x1518; // THAW +0x191C (+0x13A4 the trick's rotation; THAW +0x13F8)
constexpr uint32_t kRotationDisplay = 0x1520;
constexpr uint32_t kBalance = 0x1524;      // THAW +0x1928 (+0x18 the balance trick's kind; +0x24 the manual's
                                           // balance, its lean at +0x54)
constexpr uint32_t kFlags = 0x1528;        // THAW +0x192C (+0x18 the state)
// The timed flags (THUG2's; THAW's in the comment).
constexpr uint32_t kFlagCrouched = 0x20;   // THAW 0x20
constexpr uint32_t kFlagVert = 0x30;       // THAW 0x30
constexpr uint32_t kFlagVertGround = 0x40; // THAW 0x40
constexpr uint32_t kFlagFaceRamp = 0x68;   // THAW 0x70 (the air's turn to face the ramp allowed)
constexpr uint32_t kFlag70 = 0x70;         // THAW 0x78 (ApplyGroundFriction's flag)
constexpr uint32_t kFlagAcidDrop = 0x78;   // THAW 0x80
constexpr uint32_t kFlagNoSecondSpin = 0xC0; // THAW 0xD8

// The skater object's.
constexpr uint32_t kPosition = 0x4C;       // THAW +0x68
constexpr uint32_t kMatrix = 0x6C;         // THAW +0x88
constexpr uint32_t kRight = 0x6C;
constexpr uint32_t kUp = 0x7C;
constexpr uint32_t kFacing = 0x8C;         // THAW +0xA8
constexpr uint32_t kVelocity = 0xAC;       // THAW +0xC8

// The states.
constexpr int kStateAir = 1;
constexpr int kStateRail = 4;
constexpr int kStateBikeStall = 6; // (THAW's stall state - CoreFields::kBikeStall)

// The pad's buttons (offsets in THUG2's pad = THAW's).
constexpr uint32_t kUpButton = 0x00, kDownButton = 0x20, kLeftButton = 0x40, kRightButton = 0x60;
constexpr uint32_t kL1 = 0x80, kR1 = 0xE0, kCircle = 0x140, kSquare = 0x160;

// Checksums.
constexpr uint32_t kManualKind = 0xEF24413B;          // Manual (the balance trick's kind)
constexpr uint32_t kBrakeAcceleration = 0xE5D73F6F;   // Physics_Brake_Acceleration
constexpr uint32_t kFrontBrakeAcceleration = 0x6235801C; // Physics_Front_Brake_Acceleration
constexpr uint32_t kSkidAcceleration = 0xECFA301C;    // Physics_Brake_Skid_Acceleration
constexpr uint32_t kStandingKickSpeed = 0x4610C2E3;   // Skater_Max_Standing_Kick_Speed_Stat
constexpr uint32_t kCrouchedKickSpeed = 0x92E0247C;   // Skater_Max_Crouched_Kick_Speed_Stat
constexpr uint32_t kGroundRotation = 0x374E056B;      // Physics_Ground_Rotation
constexpr uint32_t kGroundSharpRotation = 0x7933A8EF; // Physics_Ground_Sharp_Rotation
constexpr uint32_t kMaxSpeedStat = 0x2EACDDB3;        // Skater_Max_Speed_Stat
constexpr uint32_t kGroundNoLeanTime = 0xE978B078;    // Physics_Ground_No_Lean_Time
constexpr uint32_t kNoBalanceFrames = 0x291AC9B9;     // Physics_manual_no_balance_frames
constexpr uint32_t kGroundLeanStat = 0xA2133ED7;      // Physics_Ground_Lean_stat
constexpr uint32_t kAirLeanStat = 0xCBFDD841;         // Physics_Air_Lean_stat
constexpr uint32_t kAirLeanFastStat = 0x8A93C338;     // Physics_Air_Lean_fast_stat
constexpr uint32_t kAirNoLeanTime = 0x5F70EF46;       // Physics_Air_No_Lean_Time
constexpr uint32_t kAirRampLeanTime = 0x12CD2D14;     // Physics_Air_Ramp_Lean_Time
constexpr uint32_t kAirRotationStat = 0x4957DB43;     // Physics_Air_Rotation_stat
constexpr uint32_t kAirRotationFastStat = 0x3FBC1DC9; // Physics_Air_Rotation_fast_stat
constexpr uint32_t kAirNoRotateTime = 0xA092B2BE;     // Physics_Air_No_Rotate_Time
constexpr uint32_t kAirRampRotateTime = 0x5D756349;   // Physics_Air_Ramp_Rotate_Time
constexpr uint32_t kAutoturnCancelTime = 0xABDD3395;  // skater_autoturn_cancel_time
constexpr uint32_t kSpinCountSlop = 0x50C5CC2F;       // spin_count_slop
constexpr uint32_t kHeadHeight = 0x542CF0C7;          // Skater_default_head_height
constexpr uint32_t kGroundSnapUp = 0xE4D79235;        // Physics_Ground_Snap_Up
constexpr uint32_t kGroundSnapDown = 0x899BA3D0;      // Physics_Ground_Snap_Down
constexpr uint32_t kGroundStickAngle = 0xEF161C2A;    // Ground_stick_angle
constexpr uint32_t kGroundStickAngleForward = 0x4138283E; // Ground_stick_angle_forward
constexpr uint32_t kForwardCollisionHeight = 0xD4205C9B; // Skater_First_Forward_Collision_Height
constexpr uint32_t kVertMaxSpeedTime = 0x0F230065;    // Skater_Vert_Max_Speed_Time
constexpr uint32_t kVertMaxMaxSpeedStat = 0x9CCDD908; // Skater_Vert_Max_Max_Speed_Stat
constexpr uint32_t kLanded = 0x532B16EF;              // Landed (the exception)
constexpr uint32_t kEndo = 0x4C961392;                // endo
constexpr uint32_t kNose = 0x7F03932C;                // nose

// THAW's scripts these spawn (bike_scripts.qb; phase 4 ported THAW's own: tools/pipeline/thaw_bmx/bike_scripts.ts).
constexpr const char* kEnterManualScript = "bike_enter_manual";
constexpr const char* kEnterNoseManualScript = "bike_enter_nosemanual";
constexpr const char* kBailManualScript = "bail_bike_manual";
constexpr const char* kBailNoseManualScript = "bail_bike_nosemanual";
constexpr const char* kExitEndoScript = "bike_exit_endo";
constexpr const char* kForceBailScript = "force_bike_bail";
constexpr const char* kReleaseXupScript = "bike_release_partial_xup";
// THAW's globals (physics.qb), copied into the overlay by ride.ts; THAW's values when they're missing.
constexpr const char* kNormalLerpSpeedBike = "Normal_Lerp_Speed_Bike";
constexpr float kNormalLerpSpeedBikeThaw = 0.5f;
constexpr const char* kForwardJumpBoost = "bike_forward_jump_boost";
constexpr float kForwardJumpBoostThaw = 100.0f;
constexpr const char* kGrindTrickList = "BikeGrindTrickList";
constexpr const char* kStallSlipTime = "stall_slip_time";
constexpr uint32_t kRailTolerance = 0xD0688D4E;       // Rail_Tolerance

// THAW's constants.
constexpr float kDegreesToRadians = 0.0174532924f; // 0x0068E474
constexpr float kRadiansToDegrees = 57.2957764f;   // 0x0068E478
constexpr float kLerpRate = 60.0f;                 // 0x0068FB7C (THUG2's 0x00645EA0)
constexpr float kWheelBase = 2.0f * kWheelOffset;  // 0x0068E3A4 x 0x0069014C
constexpr float kWheelFeelDown = -200.0f;          // 0x00694640
constexpr float kCrease = 0.2f;                    // 0x00690574
constexpr float kSlow = 10.0f;                     // 0x0068FFC4
constexpr float kEndoSpeed = 300.0f;               // 0x00690664
constexpr float kManualBalance = 50.0f;            // 0x006906C8 (ResetBikeManualAngle's)
constexpr float kManualTop = 100.0f;               // 0x0068FBC0
constexpr float kEndoExitSpeed = 100.0f;           // 0x0068FBC0
constexpr float kEndoBack = 200.0f;                // 0x006A0D34
constexpr float kJumpManualPitch = 0.7f;           // 0x006A0DF0 / 0x006A0DE8 (doubles)
constexpr double kStickScale = 0.0078125;          // 0x006A0DC8 (1 / 128)
constexpr float kSlowTurnRamp = 1.0f / 600.0f;     // 0x006A0DD0
constexpr int kSlowTurnRampMs = 600;               // 0x258
constexpr float kBackwardsLean = 0.0005f;          // 0x006A0DC0
constexpr float kStickHalf = 64.0f;                // (ours: the stick past half way is THAW's LEFT... button)
constexpr double kAcidDropBack = 0.1;              // 0x006A0DB0
constexpr double kStallSteepUp = 0.95;             // 2nd build 0x006EDDC0 (the stall's rail too steep under this up y)
constexpr int kStallSteepFrames = 10;              // (the frames of it before the stall turns into a grind)
constexpr float kStallSlipTimeThaw = 130.0f;       // (THAW's stall_slip_time, physics.qb - while the overlay lacks it)
constexpr uint32_t kStateChangeTime = 0x90;        // THAW +0xA0 (SetState 0x004FDED0 notes the time of a change)

// --- helpers -----------------------------------------------------------------------------------------------------

void* ObjectOf(const void* core) { return At<void*>(core, kObject); }
uint8_t* FlagsOf(const void* core) { return At<uint8_t*>(core, kFlags); }
int StateOf(const void* core) { return At<int>(FlagsOf(core), 0x18); }
uint8_t* PadOf(const void* core) { return At<uint8_t*>(core, kInput) + 0x20; }
Obj::CControlPad* ControlPad(const void* core) { return reinterpret_cast<Obj::CControlPad*>(PadOf(core)); }
float FrameLength(const void* core) { return At<float>(core, kFrameLength); }

int Now() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }
uint32_t Since(int stamp)
{
    return static_cast<uint32_t>(Tmr::GameClockElapsed()) +
           (static_cast<uint32_t>(GameVariable<int>(0x00696D60)) - static_cast<uint32_t>(stamp));
}
// A button's time since it was pressed (0x0046B920), as THAW's float compares take it (unsigned).
float HeldMs(uint8_t* button)
{
    return static_cast<float>(static_cast<uint32_t>(reinterpret_cast<Obj::CControlButton*>(button)->GetPressedTime()));
}
float HeldMs(const StickButton& button) { return static_cast<float>(Since(button.pressTime)); }

// 0x004FDED0 (not rewritten): the physics state changed.
void SetState(void* core, int state) { GameFunction<void(__thiscall*)(void*, int)>(0x004FDED0)(core, state); }

void ClearTimedFlag(const void* core, uint32_t flag)
{
    uint8_t* f = FlagsOf(core) + flag;
    if (!*f)
        return;
    *f = 0;
    At<int>(f, 4) = Now();
}

float PhysicsFloat(uint32_t name, const void* core)
{
    return Obj::GetPhysicsFloat(name, 1, static_cast<Obj::CCompositeObject*>(ObjectOf(core)));
}
int PhysicsInteger(uint32_t name, const void* core)
{
    return Obj::GetPhysicsInt(name, 1, static_cast<Obj::CCompositeObject*>(ObjectOf(core)));
}
float Stat(uint32_t name, const void* core) { return static_cast<Obj::CSkater*>(ObjectOf(core))->GetScaledPhysics(name); }
// A global of THAW's physics.qb that THUG2 hasn't: THAW's value while the overlay lacks it.
float ThawGlobal(const char* name, float thaw)
{
    float value = Script::GetFloat(name);
    return value != 0.0f ? value : thaw;
}

double Dot3(const Mth::Vector& a, const Mth::Vector& b)
{
    return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
}
double Length3(const Mth::Vector& v) { return std::sqrt(Dot3(v, v)); }
bool MovingBackwards(const void* core)
{
    void* object = ObjectOf(core);
    return Dot3(VectorAt(object, kVelocity), VectorAt(object, kFacing)) < 0.0;
}
float Speed(const void* core) { return static_cast<float>(Length3(VectorAt(ObjectOf(core), kVelocity))); }

// The movement stick's raw axis (THAW's pad +0x348 x / +0x34C y: THUG2's m_stick[2] / [3], -128..127), the d-pad's
// direction at full deflection while it's centred.
float StickRaw(const void* core, int axis)
{
    Obj::CControlPad* pad = ControlPad(core);
    float raw = pad->m_stick[2 + axis];
    if (std::fabs(raw) >= 1.0f)
        return raw;
    if (axis == 0)
        return pad->m_right.m_pressed ? 127.0f : (pad->m_left.m_pressed ? -127.0f : 0.0f);
    return pad->m_down.m_pressed ? 127.0f : (pad->m_up.m_pressed ? -127.0f : 0.0f);
}
// Its dead-banded value (THAW's pad +0x358 / +0x35C: THUG2's m_scaledStick[2] / [3], -1..1), likewise.
float StickScaled(const void* core, int axis)
{
    Obj::CControlPad* pad = ControlPad(core);
    float scaled = pad->m_scaledStick[2 + axis];
    if (scaled != 0.0f)
        return scaled;
    if (axis == 0)
        return pad->m_right.m_pressed ? 1.0f : (pad->m_left.m_pressed ? -1.0f : 0.0f);
    return pad->m_down.m_pressed ? 1.0f : (pad->m_up.m_pressed ? -1.0f : 0.0f);
}

// THAW 0x005C8CA0 (its first instruction obfuscated: the pad's pointer loaded - the rest is plain): on the
// bike a brake button held - L1 the front brake (+0x528 set), R1 the rear; the skater's is the d-pad's down.
bool BrakeHeld(const void* core, BikeState& state)
{
    uint8_t* pad = PadOf(core);
    state.frontBrake = false;
    if (pad[kL1]) {
        state.frontBrake = true;
        return true;
    }
    return pad[kR1] != 0;
}

void RunBikeScript(const void* core, const char* name, Script::CStruct* params = nullptr)
{
    uint32_t checksum = Crc::GenerateCRCFromString(name);
    if (!Script::ScriptExists(checksum)) {
        static uint32_t s_warned = 0;
        if (s_warned != checksum) {
            Log("thaw_bmx: the script %s isn't loaded (make the overlay again: tools/pipeline/thaw_bmx/convert.ts, "
                "make_menu_level.ts)",
                name);
            s_warned = checksum;
        }
        return;
    }
    Script::RunScript(checksum, params, static_cast<Obj::CObject*>(ObjectOf(core)), false, name);
}

bool WheelsOn()
{
    static bool on = ConfigInt("features", "thaw_bmx_wheels", 1) != 0;
    return on;
}

void TurnVelocity(Mth::Vector& velocity, float angle)
{
    // (as THUG2's TurnOnGround: x87 sine and cosine)
    double s = std::sin(static_cast<double>(angle)), c = std::cos(static_cast<double>(angle));
    double x = velocity.x, z = velocity.z;
    velocity.x = static_cast<float>(c * x + s * z);
    velocity.z = static_cast<float>(c * z + -(x * s));
}

void UpdateStickButton(StickButton& button, bool down)
{
    if (down && !button.down)
        button.pressTime = Now();
    button.down = down;
}

// A wheel's feeler's result (FeelWheel, below).
struct WheelHit {
    bool hit = false;
    Mth::Vector point{}, normal{};
    uint16_t flags = 0;
    int why = -1; // ([debug] thaw_bmx_wheels's log: -1 no hit, else Bike::WheelFeelResult's)
    float distance = 0.0f, reach = 0.0f, turn = 0.0f, limit = 0.0f;
};
WheelHit FeelWheel(void* core, Mth::Vector& wheel, const Mth::Vector& last, const Mth::Vector& up, float above);

} // namespace

// --- the maths -----------------------------------------------------------------------------------------------------

namespace Bike {

int BrakeChoice(bool manual, bool nose, bool frontBrake, bool endo, bool balanceIsManual)
{
    // THAW 0x005CAE10: in a manual the brake on the wheel in the air does nothing (the rear one in a nose manual, the
    // front one in a manual).
    if (manual && ((nose && !frontBrake) || (!nose && frontBrake)))
        return 0;
    int choice = 1;
    if (frontBrake && endo)
        choice = 2;
    if (!manual && !balanceIsManual)
        choice = 3;
    return choice;
}

float GroundTurnRate(float speed, float maxSpeed, float rotation, float sharpRotation, bool braking, bool manual,
                     float stickX, int heldMs, bool backwards, float balanceLean)
{
    float rate = 0.0f;
    if (stickX != 0.0f) {
        if (!braking) {
            // Sharper the slower: from the sharp rotation at a standstill to the rotation at the max speed.
            double fraction = static_cast<double>(speed) / maxSpeed;
            if (fraction > 1.0)
                fraction = 1.0f;
            rate = static_cast<float>((1.0 - static_cast<float>(fraction)) * (sharpRotation - rotation) + rotation);
        } else {
            rate = sharpRotation;
        }
        if (speed < kSlow && !manual) {
            // Nearly stopped: ramped in over the first 600 ms the d-pad's button is held, by direction only.
            if (heldMs < kSlowTurnRampMs)
                rate = static_cast<float>(heldMs) * rate * kSlowTurnRamp;
            if (stickX > 0.0f)
                rate = rate * -1.0f;
        } else {
            rate = static_cast<float>(-static_cast<double>(stickX) * kStickScale * rate);
        }
    }
    if (backwards)
        rate = manual ? 0.0f - rate : balanceLean * kBackwardsLean;
    return rate;
}

ManualOutput ManualStep(const ManualInput& in)
{
    ManualOutput out = {in.manual, in.nose, in.endo, in.angle, in.noBalanceFrames, ManualEvent::None};
    if (!out.manual) {
        // Steering: no manual. The stick held back (forward) longer than Physics_Ground_No_Lean_Time: a manual (a nose
        // manual); the front brake at speed going forwards: an endo (a nose manual on the front brake).
        if (in.left || in.right)
            return out;
        auto start = [&](bool nose, bool endo, ManualEvent event) {
            out.nose = nose;
            out.manual = true;
            out.angle = 1.0f;
            out.endo = endo;
            out.noBalanceFrames = in.noBalanceStart;
            out.event = event;
        };
        if (in.up && static_cast<float>(in.upHeld) > in.noLeanTime)
            start(true, false, ManualEvent::EnterNoseManual);
        else if (in.down && static_cast<float>(in.downHeld) > in.noLeanTime)
            start(false, false, ManualEvent::EnterManual);
        else if (in.frontBrake && in.speed > kEndoSpeed && in.forward)
            start(true, true, ManualEvent::EnterEndo);
        else
            return out;
    }
    // The first frames of a manual: no balance yet.
    if (out.noBalanceFrames > 0) {
        --out.noBalanceFrames;
        return out;
    }
    // The angle's rate: the stick (the other way round in a nose manual); in an endo up while the front brake is
    // held, else down; else falling away from 50 (down below it, over above it).
    float rate = 0.0f;
    if ((in.up || in.down) && !out.endo) {
        rate = out.nose ? 0.0f - in.stickY * in.lean : in.stickY * in.lean;
    } else if (out.endo) {
        rate = in.frontBrake ? in.lean : static_cast<float>(in.lean * -1.0);
    } else if (out.angle > kManualBalance) {
        rate = in.lean;
    } else if (!(out.angle < 0.0f)) {
        rate = static_cast<float>(in.lean * -1.0);
    }
    if (out.angle > kManualTop && rate > 0.0f) {
        // Over the top: the bail.
        out.event = ManualEvent::Bail;
        out.angle = kManualBalance;
        out.manual = false;
        out.endo = false;
        return out;
    }
    if (rate == 0.0f)
        return out;
    out.angle = in.frameLength * rate + out.angle;
    if (out.angle > 0.0f)
        return out;
    // Back on both wheels: an endo slow enough rolls back (THAW's bike_exit_endo), else Landed.
    out.event = out.endo && in.speed < kEndoExitSpeed ? ManualEvent::ExitEndo : ManualEvent::Landed;
    out.manual = false;
    out.angle = kManualBalance;
    out.endo = false;
    return out;
}

bool JumpBoosts(const Mth::Vector& velocity, float stickY)
{
    double horizontal = std::sqrt(static_cast<double>(velocity.z) * velocity.z + static_cast<double>(velocity.x) * velocity.x);
    return horizontal == 0.0 && !(stickY > 0.0f);
}

float SettleAirPitch(float pitch)
{
    // THAW 0x005D7E20: the pitch's whole degrees round a turn (truncated, the sign kept), a tenth of the way back to
    // the nearest upright each frame.
    float angle = static_cast<float>(static_cast<int>(pitch) % 360);
    if (angle == 0.0f)
        return 0.0f;
    if (angle > 0.0f)
        return angle > 180.0f ? (360.0f - angle) * 0.1f : angle * -0.1f;
    return -180.0f > angle ? (-360.0f - angle) * 0.1f : angle * -0.1f;
}

int GrindRow(float stickY)
{
    if (-1.0f <= stickY) {
        if (1.0f < stickY)
            return 125.0f <= stickY ? 4 : 3;
        return 0;
    }
    return stickY <= -125.0f ? 2 : 1;
}

int GrindColumn(bool leftSide, bool sideways, bool backwards, bool* overGrind)
{
    // THAW 0x005CF460 (with THAW's caller 0x005E2CA9: its third and fourth arguments are both the sideways flag).
    bool side = backwards ? !leftSide : leftSide;
    if (overGrind)
        *overGrind = side != sideways;
    return (side ? 1 : 0) | (sideways ? 2 : 0);
}

bool VertWindow(float since, float window, bool* raise)
{
    // THAW 0x005CAA60: the vert max-max speed while the time since is under the window; the heavy friction only once
    // it's past it (THAW asks the clock again for the friction - the same frame's time).
    *raise = since < window;
    return window < since;
}

Mth::Vector WheelBaseNormal(const Mth::Vector& normal, const Mth::Vector& front, const Mth::Vector& rear)
{
    Mth::Vector base = {front.x - rear.x, front.y - rear.y, front.z - rear.z, front.w - rear.w};
    base.Normalize();
    float along = static_cast<float>(Dot3(base, normal));
    Mth::Vector result = {normal.x - base.x * along, normal.y - base.y * along, normal.z - base.z * along,
                          normal.w - base.w * along};
    result.Normalize();
    return result;
}

Mth::Vector WheelBaseUp(const Mth::Vector& right, const Mth::Vector& front, const Mth::Vector& rear,
                        const Mth::Vector& hit)
{
    Mth::Vector d = {front.x - rear.x, front.y - rear.y, front.z - rear.z, 0.0f};
    if (Length3(d) > 0.0)
        d.Normalize();
    Mth::Vector r = {right.x, right.y, right.z, 0.0f};
    if (Length3(r) > 0.0)
        r.Normalize();
    Mth::Vector up = {r.y * d.z - r.z * d.y, r.z * d.x - d.z * r.x, d.y * r.x - r.y * d.x, 0.0f};
    if (Length3(up) > 0.0)
        up.Normalize();
    // The way that's nearer the ground's own normal.
    auto flip = [](float u, float n) { return std::fabs(n - u) > std::fabs(u + n); };
    if (flip(up.x, hit.x) || flip(up.y, hit.y) || flip(up.z, hit.z))
        up = {-up.x, -up.y, -up.z, 0.0f};
    return up;
}

WheelFeelResult FeelWheelOn(const WheelFeel& in)
{
    // THAW 0x005D5060 (2nd build 0x005D4EF0).
    WheelFeelResult out = {false, false, in.wheel, 1, 0.0f, 0.0f, 0.0f, 0.0f};
    // (THAW: the differences and `along` in SSE floats, the distance on the x87)
    float dx = in.wheel.x - in.point.x, dy = in.wheel.y - in.point.y, dz = in.wheel.z - in.point.z;
    float distance = static_cast<float>(std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy +
                                                  static_cast<double>(dx) * dx));
    float along = in.up.z * dz + in.up.y * dy + in.up.x * dx; // (how far above the hit, along up)
    const Mth::Vector onHit = {in.point.x + in.normal.x, in.point.y + in.normal.y, in.point.z + in.normal.z,
                               in.point.w + in.normal.w};
    if (!in.skatable) {
        if (along < 0.001f) { // 0x006DBD98
            out.moveWheel = true;
            out.wheel = onHit;
        }
        return out;
    }
    Mth::Vector direction = in.facing;
    if (in.backwards)
        direction = {-direction.x, -direction.y, -direction.z, -direction.w};
    float into = static_cast<float>(static_cast<double>(direction.z) * in.normal.z +
                                    static_cast<double>(direction.y) * in.normal.y +
                                    static_cast<double>(direction.x) * in.normal.x);
    Mth::Vector inHit = direction, inGround = direction;
    inHit.RotateToPlane(in.normal);
    inGround.RotateToPlane(in.groundNormal);
    float turn = static_cast<float>(static_cast<double>(inGround.x) * inHit.x + static_cast<double>(inGround.z) * inHit.z +
                                    static_cast<double>(inGround.y) * inHit.y);
    float limit = static_cast<float>(std::cos(static_cast<double>(in.stickAngle) * kDegreesToRadians));
    out.distance = distance;
    out.turn = turn;
    out.limit = limit;
    out.why = 2;
    if (into > 0.0f && turn > 0.0f && limit > turn)
        return out;
    out.why = 3;
    if (along > 0.0f) {
        float cosine = turn < -1.0f ? -1.0f : (turn > 1.0f ? 1.0f : turn);
        float mx = in.wheel.x - in.last.x, my = in.wheel.y - in.last.y, mz = in.wheel.z - in.last.z;
        double moved = std::sqrt(static_cast<double>(mx) * mx + static_cast<double>(mz) * mz + static_cast<double>(my) * my);
        float reach = static_cast<float>(std::tan(std::acos(static_cast<double>(cosine))) * moved);
        if (in.snapDown > reach)
            reach = in.snapDown;
        out.reach = reach;
        if (distance > reach)
            return out;
    }
    out.why = 0;
    out.ground = true;
    out.moveWheel = true;
    out.wheel = onHit;
    return out;
}

bool StallAcross(const Mth::Vector& startVelocity, const Mth::Vector& direction, float tolerance)
{
    // 2nd build 0x005E2972: the velocity flattened (y 0) and normalised, its dot with the rail's direction (single
    // precision, z first) - across the rail when Rail_Tolerance is more than its size.
    Mth::Vector flat = startVelocity;
    flat.y = 0.0f;
    flat.Normalize();
    float dot = direction.z * flat.z + direction.y * flat.y + flat.x * direction.x;
    return tolerance > std::fabs(dot);
}

StallSlipStep StallSlip(uint32_t elapsed, float slipTime, const Mth::Vector& velocity, const Mth::Vector& kept)
{
    // 2nd build 0x005E7092: the time since the state changed (unsigned) against stall_slip_time; under it, the kept
    // velocity x (1 / stall_slip_time) taken off - every component, w too.
    StallSlipStep out = {false, velocity};
    if (static_cast<double>(elapsed) > static_cast<double>(slipTime)) {
        out.stall = true;
        return out;
    }
    float inverse = 1.0f / slipTime;
    out.velocity.x = velocity.x - kept.x * inverse;
    out.velocity.y = velocity.y - kept.y * inverse;
    out.velocity.z = velocity.z - kept.z * inverse;
    out.velocity.w = velocity.w - kept.w * inverse;
    return out;
}

bool StallSteepStep(float upY, int* frames)
{
    // 2nd build 0x005E3821: the up's y (the ground normal, just set to it) under 0.95 (a double): the count goes up to
    // 10; at 10 back to grinding, the count to 0. Never reset otherwise.
    if (!(static_cast<double>(upY) < kStallSteepUp))
        return false;
    if (*frames < kStallSteepFrames) {
        ++*frames;
        return false;
    }
    *frames = 0;
    return true;
}

Mth::Vector StallFacing(const Mth::Vector& facing, const Mth::Vector& direction)
{
    // 2nd build 0x005E354E: the facing's dot with the rail's direction (single precision, z first); not at least 0:
    // the direction the other way (w too).
    float dot = facing.z * direction.z + facing.y * direction.y + facing.x * direction.x;
    float sign = dot >= 0.0f ? 1.0f : -1.0f;
    return {direction.x * sign, direction.y * sign, direction.z * sign, direction.w * sign};
}

} // namespace Bike

// --- the frame -----------------------------------------------------------------------------------------------------

void BeginFrame(void* core)
{
    BikeState& state = *StateOf(core, true);
    // THAW 0x005E87F4: the wheel points as the frame begins.
    state.lastFrontWheel = state.frontWheel;
    state.lastRearWheel = state.rearWheel;
    // THAW's movement stick buttons: the d-pad's, or the stick past half way.
    Obj::CControlPad* pad = ControlPad(core);
    float x = pad->m_stick[2], y = pad->m_stick[3];
    UpdateStickButton(state.stickUp, pad->m_up.m_pressed || y < -kStickHalf);
    UpdateStickButton(state.stickDown, pad->m_down.m_pressed || y > kStickHalf);
    UpdateStickButton(state.stickLeft, pad->m_left.m_pressed || x < -kStickHalf);
    UpdateStickButton(state.stickRight, pad->m_right.m_pressed || x > kStickHalf);
}

void EndFrame(void* core)
{
    {
        // (ours, a diagnostic: the bike's position or velocity no longer finite - logged once)
        static bool s_logged = false;
        void* object = ObjectOf(core);
        const Mth::Vector& p = VectorAt(object, kPosition);
        const Mth::Vector& v = VectorAt(object, kVelocity);
        if (!s_logged && !(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(v.x) &&
                           std::isfinite(v.y) && std::isfinite(v.z) && std::fabs(v.x) + std::fabs(v.y) + std::fabs(v.z) < 1e6f)) {
            s_logged = true;
            const BikeState& s = *StateOf(core, true);
            Log("thaw_bmx: not finite - state %d pos %g %g %g vel %g %g %g front %g %g %g rear %g %g %g manual %d",
                StateOf(core), p.x, p.y, p.z, v.x, v.y, v.z, s.frontWheel.x, s.frontWheel.y, s.frontWheel.z,
                s.rearWheel.x, s.rearWheel.y, s.rearWheel.z, s.manual ? 1 : 0);
        }
    }
    // (phase 4, thaw_bmx_tricks.cpp: THAW 0x00620940 - the manual's pose from its angle)
    ManualPose(core);
    // (phase 7, ours: THUG2's manual meter for the bike's manual)
    ManualMeter(core);
    // THAW 0x005C9380: the x-up held with circle - in the air, or in a manual (not a nose manual); let go: out of it.
    BikeState& state = *StateOf(core, true);
    if (!state.xup)
        return;
    if (!(StateOf(core) == kStateAir || (state.manual && !state.noseManual)))
        return;
    if (PadOf(core)[kCircle])
        return;
    state.xup = false;
    // (THAW spawns it through an obfuscated thunk with the script and four zeros; the skater runs it here.)
    RunBikeScript(core, kReleaseXupScript);
}

// --- on the ground -------------------------------------------------------------------------------------------------

void OnGround(void* core)
{
    BikeState& state = *StateOf(core, true);
    state.airPitch = 0.0f; // (THAW 0x005E4370: +0x66C)
    state.tireTap = false; // (THAW 0x005E48C2)
}

bool GroundGravityApplies(void* core, const Mth::Vector& gravity)
{
    BikeState& state = *StateOf(core, true);
    if (!(Speed(core) < kSlow))
        return true;
    Mth::Vector direction = gravity;
    if (Length3(direction) > 0.0)
        direction.Normalize();
    if (!(Dot3(VectorAt(ObjectOf(core), kFacing), direction) < 0.0))
        return true;
    return state.manual && !BrakeHeld(core, state);
}

bool ShouldBrake(void* core)
{
    BikeState& state = *StateOf(core, true);
    uint8_t* pad = PadOf(core);
    state.frontBrake = false;
    if (pad[kL1])
        state.frontBrake = true;
    else if (!pad[kR1])
        return false;
    return At<uint8_t>(core, kMayBrake) != 0;
}

void Brake(void* core)
{
    BikeState& state = *StateOf(core, true);
    bool balanceIsManual = At<uint32_t>(At<void*>(core, kBalance), 0x18) == kManualKind;
    int choice = Bike::BrakeChoice(state.manual, state.noseManual, state.frontBrake, state.endo, balanceIsManual);
    if (choice == 0)
        return;
    At<uint8_t>(core, kBraking) = 1;
    if (GameFunction<bool(__thiscall*)(void*)>(0x004FE420)(core)) { // (turning on the slope instead)
        At<uint8_t>(core, kBraking) = 0;
        return;
    }
    static const uint32_t kNames[] = {0, kBrakeAcceleration, kFrontBrakeAcceleration, kSkidAcceleration};
    float slowing = FrameLength(core) * PhysicsFloat(kNames[choice], core);
    Mth::Vector& velocity = VectorAt(ObjectOf(core), kVelocity);
    if (Length3(velocity) < static_cast<double>(slowing) * 2.0) {
        velocity = {0.0f, 0.0f, 0.0f, 1.0f};
        return;
    }
    Mth::Vector slow = velocity;
    slow.SetLength(0.0f - slowing);
    velocity.x += slow.x;
    velocity.y += slow.y;
    velocity.z += slow.z;
    velocity.w += slow.w;
}

bool CanKick(void* core)
{
    BikeState& state = *StateOf(core, true);
    if (At<uint8_t>(core, kNoKick) || BrakeHeld(core, state) || state.manual || MovingBackwards(core))
        return false;
    bool crouched = FlagsOf(core)[kFlagCrouched] || At<uint8_t>(core, kCrouchedKick);
    float most = Stat(crouched ? kCrouchedKickSpeed : kStandingKickSpeed, core);
    return !(most < Speed(core));
}

bool MayAccelerate(void* core)
{
    BikeState& state = *StateOf(core, true);
    return !MovingBackwards(core) && !state.manual;
}

void TurnOnGround(void* core)
{
    BikeState& state = *StateOf(core, true);
    Obj::CControlPad* pad = ControlPad(core);
    float stickX = StickRaw(core, 0);
    int held = 0;
    if (stickX != 0.0f)
        held = static_cast<int>(pad->m_right.m_pressed ? pad->m_right.GetPressedTime() : pad->m_left.GetPressedTime());
    float rate = Bike::GroundTurnRate(Speed(core), Stat(kMaxSpeedStat, core), PhysicsFloat(kGroundRotation, core),
                                      PhysicsFloat(kGroundSharpRotation, core), stickX != 0.0f && BrakeHeld(core, state),
                                      state.manual, stickX, held, MovingBackwards(core),
                                      At<float>(At<void*>(core, kBalance), 0x54));
    // The skid's wait (BikeExitSkid's 60 frames) counted down on the ground.
    if (state.skidTimer > 0 && --state.skidTimer == 0)
        state.canSkid = true;
    if (rate == 0.0f)
        return;
    float step = FrameLength(core) * rate;
    At<uint8_t>(core, kSpinningLeft) = step > 0.0f ? 1 : 0;
    void* object = ObjectOf(core);
    if (!At<uint8_t>(core, kTurnVelocityToo))
        TurnVelocity(VectorAt(object, kVelocity), step);
    At<Mth::Matrix>(object, kMatrix).RotateYLocal(step);
    At<Mth::Matrix>(core, kDisplayMatrix).RotateYLocal(step);
    // THAW 0x005C9880: the wheel points along the new facing.
    SetWheelPoints(state, VectorAt(object, kPosition), VectorAt(object, kFacing));
    // THAW 0x005D66E9 (2nd build 0x005D6579: the manual flag +0x525): in a manual the turn
    // is the manual's spin - added to the trick's rotation and shown (THAW's 0x0056D060 with the air pitch: THUG2's
    // rotation display, the spin only). Otherwise both wheels are felt for again at their new places (THAW 0x005D5060
    // from the snap's feeler start to 200 below): either off the ground, the turn is undone (the skater's and the
    // display's matrices turned back - not the velocity - and the wheel points put back along the old facing).
    if (state.manual) {
        float& total = At<float>(At<void*>(core, kSkaterComponent), 0x13A4);
        total = step * kRadiansToDegrees + total;
        void* display = At<void*>(At<void*>(core, kRotationDisplay), 0x18);
        GameFunction<void(__thiscall*)(void*, int)>(0x00564BE0)(display, static_cast<int>(static_cast<int64_t>(total)));
        return;
    }
    const Mth::Vector up = VectorAt(object, kUp);
    float above = PhysicsFloat(kGroundSnapUp, core);
    bool frontDown = FeelWheel(core, state.frontWheel, state.lastFrontWheel, up, above).hit;
    bool rearDown = FeelWheel(core, state.rearWheel, state.lastRearWheel, up, above).hit;
    if (frontDown && rearDown)
        return;
    At<Mth::Matrix>(object, kMatrix).RotateYLocal(-step);
    At<Mth::Matrix>(core, kDisplayMatrix).RotateYLocal(-step);
    SetWheelPoints(state, VectorAt(object, kPosition), VectorAt(object, kFacing));
}

void ManualPitch(void* core)
{
    // (THAW returns while its balance trick component's +0x1C is set; THUG2's layout differs there: here, while one
    // of THUG2's balance tricks runs.)
    if (At<uint32_t>(At<void*>(core, kBalance), 0x18) != 0)
        return;
    BikeState& state = *StateOf(core, true);
    void* object = ObjectOf(core);
    Bike::ManualInput in = {};
    in.manual = state.manual;
    in.nose = state.noseManual;
    in.endo = state.endo;
    in.angle = state.manualAngle;
    in.noBalanceFrames = state.noBalanceFrames;
    in.up = state.stickUp.down;
    in.down = state.stickDown.down;
    in.left = state.stickLeft.down;
    in.right = state.stickRight.down;
    in.upHeld = static_cast<int>(HeldMs(state.stickUp));
    in.downHeld = static_cast<int>(HeldMs(state.stickDown));
    in.frontBrake = PadOf(core)[kL1] != 0;
    in.stickY = StickScaled(core, 1);
    in.speed = Speed(core);
    in.forward = Dot3(VectorAt(object, kVelocity), VectorAt(object, kFacing)) > 0.0;
    in.noLeanTime = PhysicsFloat(kGroundNoLeanTime, core);
    in.noBalanceStart = PhysicsInteger(kNoBalanceFrames, core);
    in.lean = Stat(kGroundLeanStat, core);
    in.frameLength = FrameLength(core);
    Bike::ManualOutput out = Bike::ManualStep(in);
    state.manual = out.manual;
    state.noseManual = out.nose;
    state.endo = out.endo;
    state.manualAngle = out.angle;
    state.noBalanceFrames = out.noBalanceFrames;
    switch (out.event) {
    case Bike::ManualEvent::EnterNoseManual:
        RunBikeScript(core, kEnterNoseManualScript);
        break;
    case Bike::ManualEvent::EnterManual:
        RunBikeScript(core, kEnterManualScript);
        break;
    case Bike::ManualEvent::EnterEndo: {
        state.endoVelocity = VectorAt(object, kVelocity);
        Script::CStruct* params = Script::NewStruct();
        params->AddInteger(kEndo, 1);
        RunBikeScript(core, kEnterNoseManualScript, params);
        Script::DeleteStruct(params);
        break;
    }
    case Bike::ManualEvent::Bail:
        RunBikeScript(core, in.nose ? kBailNoseManualScript : kBailManualScript);
        break;
    case Bike::ManualEvent::Landed:
        static_cast<Obj::CObject*>(object)->SelfEvent(kLanded, nullptr);
        break;
    case Bike::ManualEvent::ExitEndo: {
        // Rolled back the way it came at 200 (the endo's velocity flattened, turned round).
        Mth::Vector back = state.endoVelocity;
        back.y = 0.0f;
        back.Normalize();
        Mth::Vector& velocity = VectorAt(object, kVelocity);
        velocity = {-back.x * kEndoBack, -back.y * kEndoBack, -back.z * kEndoBack, back.w};
        RunBikeScript(core, kExitEndoScript);
        break;
    }
    default:
        break;
    }
}

// --- jumping -------------------------------------------------------------------------------------------------------

void JumpBoost(void* core)
{
    // THAW 0x005DDF30: standing still (no horizontal speed at all) with the stick not pulled back: pushed along the
    // facing.
    void* object = ObjectOf(core);
    Mth::Vector& velocity = VectorAt(object, kVelocity);
    if (!Bike::JumpBoosts(velocity, StickRaw(core, 1)))
        return;
    float boost = ThawGlobal(kForwardJumpBoost, kForwardJumpBoostThaw);
    const Mth::Vector& facing = VectorAt(object, kFacing);
    velocity.x += facing.x * boost;
    velocity.y += facing.y * boost;
    velocity.z += facing.z * boost;
    velocity.w += facing.w * boost;
}

void JumpPitch(void* core)
{
    // THAW 0x005DDF30: out of a manual, pitched by the manual's angle (0.7 of it; a nose manual the other way).
    BikeState& state = *StateOf(core, true);
    if (!state.manual)
        return;
    float angle = static_cast<float>(state.manualAngle * (state.noseManual ? kJumpManualPitch : -kJumpManualPitch) *
                                     kDegreesToRadians);
    At<Mth::Matrix>(ObjectOf(core), kMatrix).RotateXLocal(angle);
    At<Mth::Matrix>(core, kDisplayMatrix).RotateXLocal(angle);
}

// --- in the air ----------------------------------------------------------------------------------------------------

void OnAir(void* core)
{
    // THAW 0x005DF9F0: no manual in the air.
    StateOf(core, true)->manual = false;
}

void AirWheelPoints(void* core)
{
    // THAW 0x005E05E6 (the on-bike flag; 2nd build 0x005E0476), after the display matrix
    // is the skater's again: the wheel points from the skater's position and facing.
    void* object = ObjectOf(core);
    SetWheelPoints(*StateOf(core, true), VectorAt(object, kPosition), VectorAt(object, kFacing));
}

void AirRotation(void* core)
{
    // THAW 0x005D7E20.
    uint8_t* flags = FlagsOf(core);
    if (flags[kFlag70])
        return;
    BikeState& state = *StateOf(core, true);
    void* object = ObjectOf(core);
    uint8_t* pad = PadOf(core);
    // The stick (a diagonal normalised).
    float sx = StickScaled(core, 0), sy = StickScaled(core, 1);
    if (sx != 0.0f && sy != 0.0f) {
        double length = std::sqrt(static_cast<double>(sx) * sx + static_cast<double>(sy) * sy);
        sx = static_cast<float>(sx / length);
        sy = static_cast<float>(sy / length);
    }
    bool fast = pad[kL1] != 0;
    // The pitch: up / down, after the no-lean time, ramped in.
    float pitch = 0.0f;
    float lean = Stat(fast ? kAirLeanFastStat : kAirLeanStat, core);
    float noLean = PhysicsFloat(kAirNoLeanTime, core);
    float rampLean = PhysicsFloat(kAirRampLeanTime, core);
    int held = 0;
    if (sy != 0.0f) {
        pitch = 0.0f - lean * sy;
        held = static_cast<int>(HeldMs(pad[kUpButton] ? pad + kUpButton : pad + kDownButton));
    }
    float heldF = static_cast<float>(held);
    if (!(noLean < heldF))
        pitch = 0.0f;
    if (rampLean > heldF)
        pitch = (heldF - noLean) * pitch / (rampLean - noLean);
    // (On the bike the air's turn to face the ramp is off.)
    ClearTimedFlag(core, kFlagFaceRamp);
    // The spin: left / right, likewise.
    float spin = 0.0f;
    if (!flags[kFlagNoSecondSpin]) {
        float rotation = Stat(fast ? kAirRotationFastStat : kAirRotationStat, core);
        int held2 = 0;
        if (sx != 0.0f) {
            spin = 0.0f - rotation * sx;
            held2 = static_cast<int>(HeldMs(pad[kLeftButton] ? pad + kLeftButton : pad + kRightButton));
        }
        float held2F = static_cast<float>(held2);
        if (PhysicsFloat(kAutoturnCancelTime, core) < held2F)
            ClearTimedFlag(core, kFlagFaceRamp);
        float noRotate = PhysicsFloat(kAirNoRotateTime, core);
        float rampRotate = PhysicsFloat(kAirRampRotateTime, core);
        if (!(noRotate < held2F))
            spin = 0.0f;
        if (rampRotate > held2F)
            spin = (held2F - noRotate) * spin / (rampRotate - noRotate);
    }
    // The pitch so far (degrees), or let go: back towards upright.
    bool pitched = false;
    float pitchStep = 0.0f;
    if (pitch != 0.0f) {
        pitchStep = FrameLength(core) * pitch;
        state.airPitch = pitchStep * kRadiansToDegrees + state.airPitch;
        pitched = true;
    } else {
        float settle = Bike::SettleAirPitch(state.airPitch);
        if (settle != 0.0f) {
            state.airPitch += settle;
            pitchStep = settle * kDegreesToRadians;
            pitched = true;
        }
    }
    if (pitched && !flags[kFlagVert]) {
        // (In vert THAW pitches the model only - through its model component, which THUG2's hasn't: not here.)
        At<Mth::Matrix>(object, kMatrix).RotateXLocal(pitchStep);
        At<Mth::Matrix>(core, kDisplayMatrix).RotateXLocal(pitchStep);
    }
    float spinStep = 0.0f;
    if (spin != 0.0f) {
        At<uint8_t>(core, kSpinningLeft) = spin > 0.0f ? 1 : 0;
        spinStep = FrameLength(core) * spin;
        At<Mth::Matrix>(object, kMatrix).RotateYLocal(spinStep);
        At<Mth::Matrix>(core, kDisplayMatrix).RotateYLocal(spinStep);
    }
    if (spinStep != 0.0f || pitchStep != 0.0f) {
        // The trick's rotation total, shown as THUG2's Spin shows it (short of a whole turn by more than the slop
        // in vert, not; THAW shows the flips too - THUG2's display has no place for them).
        float& total = At<float>(At<void*>(core, kSkaterComponent), 0x13A4);
        total = spinStep * kRadiansToDegrees + total;
        bool show = true;
        if (flags[kFlagVert] && !flags[kFlagAcidDrop] && !pitched) {
            double snap = 360.0f - PhysicsFloat(kSpinCountSlop, core) + 0.1f;
            show = snap <= std::fabs(static_cast<double>(total));
        }
        if (show) {
            void* display = At<void*>(At<void*>(core, kRotationDisplay), 0x18);
            GameFunction<void(__thiscall*)(void*, int)>(0x00564BE0)(display, static_cast<int>(static_cast<int64_t>(total)));
        }
    }
    // The rider's head into something (not on vert): the bail.
    if (!flags[kFlagVert]) {
        const Mth::Vector& position = VectorAt(object, kPosition);
        const Mth::Vector& up = VectorAt(object, kUp);
        float head = PhysicsFloat(kHeadHeight, core);
        VectorAt(core, kFeelerStart) = position;
        VectorAt(core, kFeelerEnd) = {position.x + up.x * head, position.y + up.y * head, position.z + up.z * head,
                                      position.w + up.w * head};
        if (static_cast<CorePhysicsComponent*>(core)->CheckFeeler(0x10, 0))
            RunBikeScript(core, kForceBailScript);
    }
}

// --- the ground under the wheels -------------------------------------------------------------------------------------

namespace {

// One wheel's feeler (THAW.exe 0x005D5060 - read whole in the second THAW build's 0x005D4EF0): THUG2's own test of the ground under the skater (CheckForGround's, 0x00509BD0)
// for one wheel. The wheel's feeler from above the wheel to 200 below it: no hit, no ground. A hit that isn't ground
// (not skatable): the wheel moved out of it (the hit point plus its normal) when it's (nearly) at or below it - no
// ground. Ground: not kept to when it turns away too steeply from the ground the bike is on (the travel direction -
// the facing, turned round rolling backwards - laid into both planes further apart than Ground_stick_angle, or
// Ground_stick_angle_forward with up held, while it runs into the hit's plane), nor when it's further away than the
// wheel could have dropped this frame at that angle (its move since `last`, the frame's start, times the angle's
// tangent - at least Physics_Ground_Snap_Down). Kept: the wheel at the hit point plus its normal. (Phase 3 took any
// skatable hit up to a wheel base below: the bike stuck to the ground over crests and off ledges THAW leaves.)
WheelHit FeelWheel(void* core, Mth::Vector& wheel, const Mth::Vector& last, const Mth::Vector& up, float above)
{
    WheelHit result;
    VectorAt(core, kFeelerStart) = {wheel.x + up.x * above, wheel.y + up.y * above, wheel.z + up.z * above,
                                    wheel.w + up.w * above};
    VectorAt(core, kFeelerEnd) = {wheel.x + up.x * kWheelFeelDown, wheel.y + up.y * kWheelFeelDown,
                                  wheel.z + up.z * kWheelFeelDown, wheel.w + up.w * kWheelFeelDown};
    if (!static_cast<CorePhysicsComponent*>(core)->CheckFeeler(0x10, 0))
        return result;
    const Mth::Vector point = VectorAt(core, kHitPoint);
    const Mth::Vector normal = VectorAt(core, kHitNormal);
    Bike::WheelFeel feel = {};
    feel.wheel = wheel;
    feel.last = last;
    feel.up = up;
    feel.point = point;
    feel.normal = normal;
    feel.groundNormal = VectorAt(core, kGroundNormal);
    feel.facing = VectorAt(ObjectOf(core), kFacing);
    feel.backwards = MovingBackwards(core);
    feel.skatable = At<uint8_t>(core, kHitSkatable) != 0;
    feel.stickAngle = PhysicsFloat(PadOf(core)[kUpButton] ? kGroundStickAngleForward : kGroundStickAngle, core);
    feel.snapDown = PhysicsFloat(kGroundSnapDown, core);
    Bike::WheelFeelResult out = Bike::FeelWheelOn(feel);
    result.why = out.why;
    result.distance = out.distance;
    result.reach = out.reach;
    result.turn = out.turn;
    result.limit = out.limit;
    if (out.moveWheel)
        wheel = out.wheel;
    if (!out.ground)
        return result;
    result.hit = true;
    result.point = point;
    result.normal = normal;
    result.flags = At<uint16_t>(core, kHitFlags);
    return result;
}

// THAW 0x005D97C0: the wheels pinned to the ground under them.
void SnapWheels(void* core, BikeState& state, WheelHit& front, WheelHit& rear, bool& mainHit)
{
    void* object = ObjectOf(core);
    const Mth::Vector up = VectorAt(object, kUp);
    const Mth::Vector facing = VectorAt(object, kFacing);
    float above = PhysicsFloat(kGroundSnapUp, core);
    front = FeelWheel(core, state.frontWheel, state.lastFrontWheel, up, above);
    rear = FeelWheel(core, state.rearWheel, state.lastRearWheel, up, above);
    // A crease between the two (the normals more than ~78 degrees apart): the leading wheel's hit dropped.
    if (front.hit && rear.hit && Dot3(front.normal, rear.normal) < kCrease) {
        if (!MovingBackwards(core))
            front.hit = false;
        else
            rear.hit = false;
    }
    if (front.hit)
        state.frontWheel = front.point;
    if (rear.hit)
        state.rearWheel = rear.point;
    // Under the skater (the component's own feeler, kept for the ground check after).
    const Mth::Vector position = VectorAt(object, kPosition);
    VectorAt(core, kFeelerStart) = {position.x + up.x * above, position.y + up.y * above, position.z + up.z * above,
                                    position.w + up.w * above};
    VectorAt(core, kFeelerEnd) = {position.x + up.x * kWheelFeelDown, position.y + up.y * kWheelFeelDown,
                                  position.z + up.z * kWheelFeelDown, position.w + up.w * kWheelFeelDown};
    mainHit = static_cast<CorePhysicsComponent*>(core)->CheckFeeler(0x10, 0);
    if (mainHit && PhysicsFloat(kGroundSnapDown, core) < At<float>(core, kFeelerDistance))
        mainHit = false;
    const Mth::Vector mainPoint = VectorAt(core, kHitPoint);
    // One wheel down: the other a wheel base from it - towards the ground under the skater, else along the facing.
    auto towards = [&](const Mth::Vector& from) {
        Mth::Vector d = mainHit ? Mth::Vector{mainPoint.x - from.x, mainPoint.y - from.y, mainPoint.z - from.z,
                                              mainPoint.w - from.w}
                                : facing;
        if (mainHit)
            d.Normalize();
        return d;
    };
    if (rear.hit && !front.hit) {
        Mth::Vector d = towards(state.rearWheel);
        state.frontWheel = {state.rearWheel.x + d.x * kWheelBase, state.rearWheel.y + d.y * kWheelBase,
                            state.rearWheel.z + d.z * kWheelBase, state.rearWheel.w + d.w * kWheelBase};
    } else if (front.hit && !rear.hit) {
        // (THAW subtracts here too - the rear a wheel base on from the front, past it, when the ground under the
        // skater was found: its slip; the rear goes back from the front, as the other case.)
        Mth::Vector d = mainHit ? towards(state.frontWheel) : Mth::Vector{-facing.x, -facing.y, -facing.z, -facing.w};
        state.rearWheel = {state.frontWheel.x + d.x * kWheelBase, state.frontWheel.y + d.y * kWheelBase,
                           state.frontWheel.z + d.z * kWheelBase, state.frontWheel.w + d.w * kWheelBase};
    }
    // Something between the rider and a wheel: the wheel there (out by its normal); nearly stopped, the bike pushed
    // back.
    for (Mth::Vector* wheel : {&state.frontWheel, &state.rearWheel}) {
        Nx::CFeeler feeler;
        feeler.Construct();
        feeler.m_line.start = {position.x + up.x * kWheelOffset, position.y + up.y * kWheelOffset,
                               position.z + up.z * kWheelOffset, position.w + up.w * kWheelOffset};
        feeler.m_line.end = {wheel->x + up.x, wheel->y + up.y, wheel->z + up.z, wheel->w + up.w};
        if (feeler.GetCollision(true, false)) {
            *wheel = {feeler.m_point.x + feeler.m_normal.x, feeler.m_point.y + feeler.m_normal.y,
                      feeler.m_point.z + feeler.m_normal.z, feeler.m_point.w + feeler.m_normal.w};
            Mth::Vector& velocity = VectorAt(object, kVelocity);
            if (Length3(velocity) < kSlow)
                velocity = {-velocity.x, -velocity.y, -velocity.z, -velocity.w};
        }
        feeler.Destruct();
    }
}

} // namespace

bool RollingBackwards(void* core) { return MovingBackwards(core); }

bool VertPushHeld(void* core, int pushTime)
{
    // THAW 0x005DA380 on the bike: LEFTUP held longer than Skater_vert_push_time, neither LEFTLEFT nor LEFTRIGHT nor
    // square nor circle.
    const BikeState& state = *StateOf(core, true);
    uint8_t* pad = PadOf(core);
    return state.stickUp.down && pushTime < static_cast<int>(Since(state.stickUp.pressTime)) && !state.stickLeft.down &&
           !state.stickRight.down && !pad[kSquare] && !pad[kCircle];
}

bool VertTapHeld(void* core)
{
    // THAW 0x005DA380 on the bike: R1 with LEFTUP or LEFTDOWN, or the tire tap looked for in vert (+0x54C).
    const BikeState& state = *StateOf(core, true);
    return (PadOf(core)[kR1] && (state.stickUp.down || state.stickDown.down)) || state.tapArmed;
}

// --- steps, crests and vert (phase 6) --------------------------------------------------------------------------------

float StuckCheckRaise(void* core)
{
    // THAW 0x005C2980: on the bike (+0x524) the start's y plus Skater_First_Forward_Collision_Height.
    return PhysicsFloat(kForwardCollisionHeight, core);
}

void NoteVertGround(void* core)
{
    // THAW 0x005E46E9: the ground feeler's face vert (THAW's +0x3E9 - read as its feeler check's vert byte, THUG2's
    // +0x229) - the time noted.
    if (!At<uint8_t>(core, kHitVert))
        return;
    BikeState& state = *StateOf(core, true);
    state.vertGroundNoted = true;
    state.vertGroundTime = Now();
}

bool VertSpeedAllowance(void* core, float* maxMax)
{
    // THAW 0x005CAA60: within Skater_Vert_Max_Speed_Time of the last vert ground the max-max speed is
    // Skater_Vert_Max_Max_Speed_Stat's, and no heavy air friction over the max speed.
    BikeState& state = *StateOf(core, true);
    if (!state.vertGroundNoted)
        return true;
    bool raise = false;
    bool friction = Bike::VertWindow(static_cast<float>(Since(state.vertGroundTime)),
                                     PhysicsFloat(kVertMaxSpeedTime, core), &raise);
    if (raise)
        *maxMax = Stat(kVertMaxMaxSpeedStat, core);
    return friction;
}

WheelGround WheelGroundCheck(void* core)
{
    BikeState& state = *StateOf(core, true);
    void* object = ObjectOf(core);
    // (THAW 0x005E534A: the wheels only out of a manual, a tire tap and flag +0x78 (THUG2's +0x70); otherwise the
    // skater's ground check with the wheel points at the skater's sides.)
    if (!WheelsOn() || state.manual || state.tireTap || FlagsOf(core)[kFlag70]) {
        SetWheelPoints(state, VectorAt(object, kPosition), VectorAt(object, kFacing));
        return kWheelsNotUsed;
    }
    SetWheelPoints(state, VectorAt(object, kPosition), VectorAt(object, kFacing));
    WheelHit front, rear;
    bool mainHit = false;
    SnapWheels(core, state, front, rear, mainHit);
    // (THAW's flag +0x40, vert under a wheel; THAW also sets its +0x58 from face flag 0x100 - THUG2's equivalent isn't
    // known: not here.)
    uint8_t vert = (front.hit && (front.flags & 8)) || (rear.hit && (rear.flags & 8));
    uint8_t* flag = FlagsOf(core) + kFlagVertGround;
    if (*flag != vert) {
        *flag = vert;
        At<int>(flag, 4) = Now();
    }
    // Off the ground: no wheel down, or only the trailing one (the leading wheel over an edge).
    bool backwards = MovingBackwards(core);
    if ((!front.hit && !rear.hit) || (front.hit && !rear.hit && backwards) || (!front.hit && rear.hit && !backwards)) {
        static const bool s_log = ConfigInt("debug", "thaw_bmx_wheels", 0) != 0;
        if (s_log) {
            const Mth::Vector& p = VectorAt(object, kPosition);
            Log("thaw_bmx wheels off: pos %.1f %.1f %.1f speed %.1f%s | front %d why %d dist %.2f reach %.2f turn %.3f "
                "limit %.3f | rear %d why %d dist %.2f reach %.2f turn %.3f limit %.3f",
                p.x, p.y, p.z, Speed(core), backwards ? " backwards" : "", front.hit ? 1 : 0, front.why, front.distance,
                front.reach, front.turn, front.limit, rear.hit ? 1 : 0, rear.why, rear.distance, rear.reach, rear.turn,
                rear.limit);
        }
        return kWheelsOff;
    }
    // On it: midway between the wheels, the ground normal across the wheel base.
    Mth::Vector& position = VectorAt(object, kPosition);
    position = {(state.frontWheel.x + state.rearWheel.x) * 0.5f, (state.frontWheel.y + state.rearWheel.y) * 0.5f,
                (state.frontWheel.z + state.rearWheel.z) * 0.5f, (state.frontWheel.w + state.rearWheel.w) * 0.5f};
    Mth::Vector up = Bike::WheelBaseUp(VectorAt(object, kRight), state.frontWheel, state.rearWheel,
                                       VectorAt(core, kHitNormal));
    GameFunction<void(__thiscall*)(void*, Mth::Vector)>(0x004FE860)(core, up); // (THAW's 0x005D0D40: an obfuscated thunk)
    if (StateOf(core) != kStateAir)
        static_cast<CorePhysicsComponent*>(core)->KeepUpright(); // (THAW 0x005CE980, on the ground)
    return mainHit ? kWheelsOnGround : kWheelsOnGroundNoHit;
}

bool LerpDisplayNormal(void* core)
{
    if (StateOf(core) == kStateAir)
        return false;
    BikeState& state = *StateOf(core, true);
    // THUG2's 0x004FEA50 with THAW's bike target and speed.
    Mth::Vector target = Bike::WheelBaseNormal(VectorAt(core, kGroundNormal), state.frontWheel, state.rearWheel);
    float& t = At<float>(core, kNormalLerp);
    Mth::Vector& display = VectorAt(core, kDisplayNormal);
    if (t != 0.0f) {
        const Mth::Vector& old = VectorAt(core, kOldNormal);
        if (old == target) {
            t = 0.0f;
        } else {
            t = t - FrameLength(core) * ThawGlobal(kNormalLerpSpeedBike, kNormalLerpSpeedBikeThaw) * kLerpRate;
            if (!(t > 0.0f)) {
                t = 0.0f;
                display = target;
            } else {
                display = {(old.x - target.x) * t + target.x, (old.y - target.y) * t + target.y,
                           (old.z - target.z) * t + target.z, (old.w - target.w) * t + target.w};
                display.Normalize();
            }
        }
    }
    Mth::Vector& displayUp = VectorAt(core, kDisplayUp);
    if (displayUp != display) {
        displayUp = display;
        At<Mth::Matrix>(core, kDisplayMatrix).OrthoNormalizeAbout(1);
        void* object = ObjectOf(core);
        VectorAt(core, kDisplayMatrix) = VectorAt(object, kRight);
        VectorAt(core, kDisplayForward) = VectorAt(object, kFacing);
    }
    return true;
}

// --- grinding, acid drops ------------------------------------------------------------------------------------------

Script::CArray* GrindTrickRow(void* core, bool leftSide, bool sideways, bool backwards, uint32_t* column)
{
    BikeState& state = *StateOf(core, true);
    *column = static_cast<uint32_t>(Bike::GrindColumn(leftSide, sideways, backwards, &state.overGrind));
    Script::CArray* list = Script::GetArray(kGrindTrickList);
    if (!list || list->m_size == 0)
        return nullptr;
    uint32_t row = static_cast<uint32_t>(Bike::GrindRow(StickRaw(core, 1)));
    if (row >= list->m_size)
        row = 0;
    return reinterpret_cast<Script::CArray*>(list->GetElement(row));
}

bool GrindSide(void*, bool leftSide, bool badLedgeOnThatSide) { return badLedgeOnThatSide ? !leftSide : leftSide; }

bool AcidDropDirection(void* core, Mth::Vector* direction)
{
    const Mth::Vector& facing = VectorAt(ObjectOf(core), kFacing);
    Mth::Vector d = {facing.x, 0.0f, facing.z, facing.w};
    double length = std::sqrt(static_cast<double>(d.x) * d.x + static_cast<double>(d.z) * d.z);
    if (length > 0.0) {
        float inverse = static_cast<float>(1.0 / length);
        d = {d.x * inverse, 0.0f, d.z * inverse, d.w * inverse};
    }
    if (StickRaw(core, 1) > kAcidDropBack)
        d = {-d.x, -d.y, -d.z, d.w};
    *direction = d;
    return true;
}

// --- the stall ---------------------------------------------------------------------------------------------------

void GrindStall(void* core, const Mth::Vector& startVelocity, const Mth::Vector& direction)
{
    // THAW 0x005E2AA5 (2nd build 0x005E2935). (THAW: R1 or the skater's ability 0x16 - THAW's skater stalls too; only
    // the bike's is ported. THAW runs the rail's TriggerScript here, with its stall event 0x20000 when R1 is held - our
    // grind start ran THUG2's before getting onto the rail, and THUG2's events have no stall.)
    BikeState& state = *StateOf(core, true);
    if (!PadOf(core)[kR1]) {
        state.stallScript = false;
        return;
    }
    if (Bike::StallAcross(startVelocity, direction, Script::GetFloat(kRailTolerance)))
        SetState(core, kStateBikeStall);
    state.stallVelocity = VectorAt(ObjectOf(core), kVelocity);
    state.stallScript = true;
}

void RailStallSlip(void* core)
{
    // 2nd build 0x005E7074.
    BikeState* state = StateOf(core, false);
    if (!state || !state->stallScript)
        return;
    if (StateOf(core) != kStateRail) {
        state->stallScript = false;
        return;
    }
    float slipTime = ThawGlobal(kStallSlipTime, kStallSlipTimeThaw);
    Mth::Vector& velocity = VectorAt(ObjectOf(core), kVelocity);
    Bike::StallSlipStep step =
        Bike::StallSlip(Since(At<int>(core, kStateChangeTime)), slipTime, velocity, state->stallVelocity);
    if (step.stall) {
        state->stallScript = false;
        SetState(core, kStateBikeStall);
        return;
    }
    velocity = step.velocity;
}

bool StallSteepRail(void* core)
{
    // 2nd build 0x005E3801. (THAW also sets +0x728, and +0x729 off the bike with the facing down the slope - nothing
    // of THAW's reads either.)
    BikeState& state = *StateOf(core, true);
    if (!Bike::StallSteepStep(VectorAt(ObjectOf(core), kUp).y, &state.stallSteepFrames))
        return false;
    SetState(core, kStateRail);
    return true;
}

int EnterWheelieFromAir(void* core, Script::CStruct* params)
{
    // THAW 0x005CA520.
    BikeState& state = *StateOf(core, true);
    state.manual = true;
    state.manualAngle = kManualBalance;
    state.noBalanceFrames = PhysicsInteger(kNoBalanceFrames, core);
    state.noseManual = params && params->ContainsFlag(kNose);
    uint8_t* pad = PadOf(core);
    if (pad[state.noseManual ? kL1 : kR1])
        VectorAt(ObjectOf(core), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
    state.tireTap = false;
    return 1;
}

} // namespace Skater::ThawBmx
