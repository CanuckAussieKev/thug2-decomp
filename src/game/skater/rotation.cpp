// The skater's turning in the air (spins, breaking out of vert), with PartyMod's PS2-style controls built in.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles,
// rounded to float only where the original stores them.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/skater/rotation.h"

#include <bit>
#include <cmath>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/control_pad.h"
#include "game/skater/controls.h"
#include "game/skater/skater.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/game_clock.h"
#include "game/skate/score.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The component's fields.
constexpr uint32_t kObject = 0x0C;          // the skater object: matrix at +0x6C, velocity at +0xAC
constexpr uint32_t kDisplayMatrix = 0x18;
constexpr uint32_t kWallNormalX = 0x58;     // (x and z of the vert wall's normal, it seems)
constexpr uint32_t kWallNormalZ = 0x60;
constexpr uint32_t kFrameLength = 0x8C;     // the turn per frame is speed * this
constexpr uint32_t kRampDirection = 0x288;  // a vector the face-the-ramp turn is measured against
constexpr uint32_t kBreakVertFlag = 0x2D0;
constexpr uint32_t kTapSpin = 0x2DA;
constexpr uint32_t kQueuedSpin = 0x30C;     // tap mode: the turn still to do (radians)
constexpr uint32_t kNoSpin = 0x370;
constexpr uint32_t kSpinningLeft = 0x3A5;   // (true for a positive turn)
constexpr uint32_t kInput = 0x150C;         // the pad: buttons of 0x20 bytes from +0x20
constexpr uint32_t kTrick = 0x1518;         // the trick: its rotation total (degrees) at +0x13A4
constexpr uint32_t kRotationDisplay = 0x1520; // [+0x18]: what shows the rotation total
constexpr uint32_t kFlags = 0x1528;         // the skater's state flags: {bool, int time} each

// The skater object's fields.
constexpr uint32_t kMatrix = 0x6C;          // rows: +0x6C, up +0x7C, forward +0x8C, position +0x9C
constexpr uint32_t kForward = 0x8C;
constexpr uint32_t kUp = 0x7C;
constexpr uint32_t kVelocity = 0xAC;

// The state flags used here (offsets into the flags block).
constexpr uint32_t kFlag30 = 0x30;
constexpr uint32_t kFlag38 = 0x38;
constexpr uint32_t kFlag48 = 0x48;
constexpr uint32_t kCanFaceRamp = 0x68;     // (cleared once a spin has gone on long enough)
constexpr uint32_t kFlag78 = 0x78;
constexpr uint32_t kFlag80 = 0x80;
constexpr uint32_t kFlagB0 = 0xB0;
constexpr uint32_t kNoSecondSpin = 0xC0;

// A button of the pad: {bool down, int time it changed, ..., +0x10 bool pressed this frame}.
constexpr uint32_t kJump = 0x00;           // (offsets from the pad's +0x20)
constexpr uint32_t kSpinLeft2 = 0x40;
constexpr uint32_t kSpinRight2 = 0x60;
constexpr uint32_t kSpinLeft = 0x80;       // L1
constexpr uint32_t kTriggerA = 0xA0;       // PartyMod's "R2/L2" pair
constexpr uint32_t kSpinRight = 0xE0;      // R1
constexpr uint32_t kTriggerB = 0x100;
constexpr uint32_t kButton140 = 0x140;
constexpr uint32_t kButton160 = 0x160;

// Script values (checksums).
constexpr uint32_t kSpinDelay = 0xA5A02449;
constexpr uint32_t kSpinFaceRampTime = 0xABDD3395;
constexpr uint32_t kSpinRampStart = 0xA092B2BE;
constexpr uint32_t kSpinRampEnd = 0x5D756349;
constexpr uint32_t kSpinSpeed = 0x4957DB43;
constexpr uint32_t kTapSpinSpeed = 0xF7A2ACC1;
constexpr uint32_t kFaceRampMaxTilt = 0x61224F2E;
constexpr uint32_t kFaceRampSpeed = 0x9DB33213;
constexpr uint32_t kRotationSnap = 0x50C5CC2F;
constexpr uint32_t kBreakVertJumpTime = 0x9D2F8CC8;
constexpr uint32_t kBreakVertSlow = 0x13F33B41;
constexpr uint32_t kBreakVertUpScale = 0x848C5CD6;
constexpr uint32_t kBreakVertPitch = 0xD757F4BB;

constexpr float kPi = 3.14159274f;                    // 0x0064BB7C
constexpr float kDegreesToRadians = 0.0174532924f;    // 0x00645464
constexpr float kRadiansToDegrees = 57.2957764f;      // 0x00645468
constexpr float kBreakVertPush = 24.0f;               // 0x00645EE8
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490FDB && std::bit_cast<uint32_t>(kDegreesToRadians) == 0x3C8EFA35 &&
                  std::bit_cast<uint32_t>(kRadiansToDegrees) == 0x42652EE0,
              "the original's constants");

// The game clock (ms) plus the offset at 0x00696D60: the time a flag or button changed.
int Now() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }

// Clear a state flag (noting when).
void ClearFlag(void* component, uint32_t flag)
{
    uint8_t* f = At<uint8_t*>(component, kFlags) + flag;
    if (*f) {
        *f = 0;
        At<int>(f, 4) = Now();
    }
}

// A physics value from script: the skater's physics structure's `name`, else the
// global (asserted when `required`).
double PhysicsFloat(uint32_t name, int required, void* object)
{
    return Obj::GetPhysicsFloat(name, required, static_cast<Obj::CCompositeObject*>(object));
}
int PhysicsInt(uint32_t name, int required, void* object)
{
    return Obj::GetPhysicsInt(name, required, static_cast<Obj::CCompositeObject*>(object));
}
// A value set by the skater's stats (0x00580790: script range scaled by the stat level, unrounded).
double StatValue(void* object, uint32_t name)
{
    return static_cast<Obj::CSkater*>(object)->GetScaledPhysics(name); // (rounded to a float by ours)
}

// How long a button has been down - read as unsigned by the first pair of spin buttons, signed by
// the second.
int HeldTime(uint8_t* button) { return reinterpret_cast<Obj::CControlButton*>(button)->GetPressedTime(); }
float HeldSeconds(uint8_t* button) { return static_cast<float>(static_cast<double>(static_cast<uint32_t>(HeldTime(button)))); }
// Forget a button's press this frame.
void TakePress(uint8_t* button) { reinterpret_cast<Obj::CControlButton*>(button)->ClearTrigger(); }

Mth::Matrix& ObjectMatrix(void* object) { return At<Mth::Matrix>(object, kMatrix); }

// The skater's matrix turned so that it faces along its velocity (x and z), kept upright about that.
void FaceAlongVelocity(void* object)
{
    Mth::Vector direction = At<Mth::Vector>(object, kVelocity);
    direction.Normalize();
    Mth::Vector& forward = At<Mth::Vector>(object, kForward);
    forward.x = direction.x;
    forward.z = direction.z;
    forward.Normalize();
    ObjectMatrix(object).OrthoNormalizeAbout(2);
}

} // namespace

// 0x00504060
void CorePhysicsComponent::Spin()
{
    uint8_t* pad = At<uint8_t*>(this, kInput) + 0x20;
    void* object = At<void*>(this, kObject);
    if (At<bool>(this, kNoSpin))
        return;
    uint8_t* flags = At<uint8_t*>(this, kFlags);
    if (flags[kFlag78] && flags[kCanFaceRamp])
        ClearFlag(this, kCanFaceRamp);

    bool spinning = false;
    float speed = 0.0f;
    float& queued = At<float>(this, kQueuedSpin);
    auto frameLength = [this] { return static_cast<double>(At<float>(this, kFrameLength)); };
    if (At<bool>(this, kTapSpin)) {
        // Each tap queues half a turn (unless the other button is down).
        if (pad[kSpinLeft + 0x10]) {
            TakePress(pad + kSpinLeft);
            if (!pad[kSpinRight])
                queued = static_cast<float>(static_cast<double>(queued) + kPi);
        }
        if (pad[kSpinRight + 0x10]) {
            TakePress(pad + kSpinRight);
            if (!pad[kSpinLeft])
                queued = static_cast<float>(static_cast<double>(queued) - kPi);
        }
        if (queued != 0.0f) {
            spinning = true;
            speed = queued < 0.0f ? -1.0f : 1.0f;
            double turn = StatValue(object, kTapSpinSpeed) * speed;
            double step = turn * frameLength();
            if (std::fabs(queued) < std::fabs(step))
                queued = 0.0f;
            else
                queued = static_cast<float>(queued - step);
            speed = static_cast<float>(turn);
        }
    } else {
        // Holding one button (not both) turns once it's been held longer than the spin delay - always, with
        // PartyMod's PS2 controls (it NOPs the delay checks' jumps; the delay is still read). Held long enough,
        // the skater can no longer be turned to face the ramp.
        uint8_t* left = pad + kSpinLeft;
        uint8_t* right = pad + kSpinRight;
        if (left[0] && !right[0]) {
            float held = HeldSeconds(left);
            if (PhysicsFloat(kSpinDelay, 0, nullptr) < held || Ps2Controls()) {
                speed = static_cast<float>(StatValue(object, kSpinSpeed));
                spinning = true;
                held = HeldSeconds(left);
                if (PhysicsFloat(kSpinFaceRampTime, 1, object) < held)
                    ClearFlag(this, kCanFaceRamp);
            }
        }
        if (right[0] && !left[0]) {
            float held = HeldSeconds(right);
            if (PhysicsFloat(kSpinDelay, 0, nullptr) < held || Ps2Controls()) {
                speed = static_cast<float>(-StatValue(object, kSpinSpeed));
                spinning = true;
                held = HeldSeconds(right);
                if (PhysicsFloat(kSpinFaceRampTime, 1, object) < held)
                    ClearFlag(this, kCanFaceRamp);
            }
        }
    }

    // The second pair of spin buttons: the speed ramps up from 0 at one held time to full at another.
    if (!At<uint8_t*>(this, kFlags)[kNoSecondSpin] && !spinning) {
        int held = 0;
        if (pad[kSpinLeft2]) {
            speed = static_cast<float>(StatValue(object, kSpinSpeed));
            held = HeldTime(pad + kSpinLeft2);
            if (PhysicsFloat(kSpinFaceRampTime, 1, object) < static_cast<float>(held))
                ClearFlag(this, kCanFaceRamp);
        }
        if (pad[kSpinRight2]) {
            speed = static_cast<float>(-StatValue(object, kSpinSpeed));
            held = HeldTime(pad + kSpinRight2);
            if (PhysicsFloat(kSpinFaceRampTime, 1, object) < static_cast<float>(held))
                ClearFlag(this, kCanFaceRamp);
        }
        float start = static_cast<float>(PhysicsFloat(kSpinRampStart, 1, object));
        double end = PhysicsFloat(kSpinRampEnd, 1, object);
        double t = held;
        if (t <= start)
            speed = 0.0f;
        if (t < end)
            speed = static_cast<float>((t - start) * speed / (end - start));
    }

    if (speed == 0.0f) {
        // Not spinning: turn the skater to face along the ramp while it's allowed and they're tilted far enough.
        flags = At<uint8_t*>(this, kFlags);
        if (flags[kFlag30] && flags[kCanFaceRamp]) {
            float tilt = Mth::Clamp(At<float>(object, kForward + 4), -1.0f, 1.0f);
            float angle = static_cast<float>(std::acos(static_cast<double>(tilt))); // (the C runtime's, 0x00623630)
            if (PhysicsFloat(kFaceRampMaxTilt, 1, object) * kDegreesToRadians > angle) {
                ClearFlag(this, kCanFaceRamp);
            } else {
                float off = static_cast<float>(Mth::GetAngleAbout(At<Mth::Vector>(object, kForward),
                                                                  At<Mth::Vector>(this, kRampDirection),
                                                                  At<Mth::Vector>(object, kUp)));
                float rate = static_cast<float>(PhysicsFloat(kFaceRampSpeed, 1, object));
                double turn = static_cast<double>(Mth::Sign(off)) * rate;
                speed = static_cast<float>(turn);
                if (std::fabs(off) < std::fabs(turn * frameLength())) {
                    // (the last bit: exactly what's left)
                    speed = static_cast<float>(off / frameLength());
                    ClearFlag(this, kCanFaceRamp);
                }
            }
        }
        if (speed == 0.0f)
            return;
    }

    At<bool>(this, kSpinningLeft) = speed > 0.0f;
    float step = static_cast<float>(static_cast<double>(speed) * frameLength());
    ObjectMatrix(object).RotateYLocal(step);
    At<Mth::Matrix>(this, kDisplayMatrix).RotateYLocal(step);
    float& total = At<float>(At<uint8_t*>(this, kTrick), 0x13A4);
    total = static_cast<float>(static_cast<double>(step) * kRadiansToDegrees + total);
    // Shown as an integer (the C runtime's _ftol2: truncated) - except when the flags say not to and the total
    // is short of a whole turn by more than the snap (360 - snap + 0.1).
    flags = At<uint8_t*>(this, kFlags);
    if (flags[kFlag30] && !flags[kFlag78]) {
        float size = static_cast<float>(std::fabs(static_cast<double>(total)));
        double snap = 360.0f - PhysicsFloat(kRotationSnap, 1, object) + 0.1f;
        if (!(snap <= size))
            return;
    }
    void* display = At<void*>(At<void*>(this, kRotationDisplay), 0x18);
    static_cast<Mdl::Score*>(display)->UpdateSpin(static_cast<int>(static_cast<int64_t>(total)));
}

// 0x00507120
void CorePhysicsComponent::BreakVert(bool force)
{
    void* object = At<void*>(this, kObject);
    uint8_t* pad = At<uint8_t*>(this, kInput) + 0x20;
    // The two buttons: both (PartyMod's PS2 controls: either - its JNZs at 0x00507184 / 0x005071AE).
    auto triggers = [&] {
        return Ps2Controls() ? pad[kTriggerA] || pad[kTriggerB] : pad[kTriggerA] && pad[kTriggerB];
    };
    bool jumpHeld = false;
    // Not original: THAW 0x005DA210 (the on-bike flag read first, 0x005DA218) - on the bike the movement stick's up
    // held (THAW's LEFTUP, not left / right) instead of the d-pad's, and R1 with the stick up or down (the tire tap's
    // buttons), or a tire tap looked for in vert, takes the second path (a spine transfer, else pushed off the wall)
    // instead of falling away from it.
    bool bike = ThawBmx::Active(this);
    if (bike) {
        jumpHeld = ThawBmx::VertPushHeld(this, PhysicsInt(kBreakVertJumpTime, 1, object));
    } else if (pad[kJump]) {
        int limit = PhysicsInt(kBreakVertJumpTime, 1, object);
        jumpHeld = HeldTime(pad + kJump) > limit && !pad[kSpinLeft2] && !pad[kSpinRight2] && !pad[kButton160] &&
                   !pad[kButton140];
    }
    if (!jumpHeld && !triggers() && !At<bool>(this, kBreakVertFlag) && !force)
        return;

    float* velocity = &At<float>(object, kVelocity);
    if (triggers() || At<bool>(this, kBreakVertFlag) || (bike && ThawBmx::VertTapHeld(this))) {
        if (TrySpineTransfer()) {
            At<Mth::Matrix>(this, kDisplayMatrix) = ObjectMatrix(object);
            return;
        }
        velocity[0] = static_cast<float>(velocity[0] - static_cast<double>(At<float>(this, kWallNormalX)) * kBreakVertPush);
        velocity[2] = static_cast<float>(velocity[2] - static_cast<double>(At<float>(this, kWallNormalZ)) * kBreakVertPush);
        ObjectMatrix(object).RotateXLocal(
            static_cast<float>(PhysicsFloat(kBreakVertPitch, 1, object) * kDegreesToRadians));
        ClearFlag(this, kFlag30);
        ClearFlag(this, kFlag38);
        ClearFlag(this, kFlag48);
        uint8_t* flag80 = At<uint8_t*>(this, kFlags) + kFlag80;
        if (*flag80 != 1) {
            *flag80 = 1;
            At<int>(flag80, 4) = Now();
        }
        FaceAlongVelocity(object);
        return; // (the display matrix isn't updated on this path)
    }

    // Slowed away from the wall by the speed times a script value; the upward speed scaled.
    double lengthSqr = static_cast<double>(velocity[0]) * velocity[0] + static_cast<double>(velocity[1]) * velocity[1];
    lengthSqr += static_cast<double>(velocity[2]) * velocity[2];
    float lengthSqrStored = static_cast<float>(lengthSqr);
    double slow = PhysicsFloat(kBreakVertSlow, 1, object) * std::sqrt(static_cast<double>(lengthSqrStored));
    velocity[0] = static_cast<float>(velocity[0] - slow * At<float>(this, kWallNormalX));
    velocity[2] = static_cast<float>(velocity[2] - slow * At<float>(this, kWallNormalZ));
    velocity[1] = static_cast<float>(PhysicsFloat(kBreakVertUpScale, 1, object) * velocity[1]);
    ObjectMatrix(object).RotateXLocal(static_cast<float>(PhysicsFloat(kBreakVertPitch, 1, object) * kDegreesToRadians));
    ClearFlag(this, kFlag30);
    ClearFlag(this, kFlag38);
    ClearFlag(this, kFlag48);
    ClearFlag(this, kFlagB0);
    FaceAlongVelocity(object);
    At<Mth::Matrix>(this, kDisplayMatrix) = ObjectMatrix(object);
}

} // namespace Skater

REPLACE(0x00504060, &Skater::CorePhysicsComponent::Spin);
REPLACE(0x00507120, &Skater::CorePhysicsComponent::BreakVert);
