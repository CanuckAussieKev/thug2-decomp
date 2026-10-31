// A controller's buttons and sticks (0x0046B7E0-0x0046C470). See control_pad.h.

#include "engine/objects/control_pad.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "game/rider/thaw_bmx.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"

namespace GameObjects {

int g_clockOffset;

namespace {

constexpr int kPressedAbove = 0x40;

// The game clock with its offset (0x00696D60).
int Clock() { return FrameClock::GameClockElapsed() + g_clockOffset; }

// FrameClock::GetTime as the original compares it: unsigned, as a float would hold it.
double Now() { return static_cast<double>(FrameClock::GetTime()); }

float FloatFromBits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

const float kPi = FloatFromBits(0x40490FDB);
const float kHalfPi = FloatFromBits(0x3FC90FDB);
const float kQuarterPi = FloatFromBits(0x3F490FDB);
const float kThreeQuarterPi = FloatFromBits(0x4016CBE4);

// The FPU's own cosine and arctangent (as the original's FCOS / FPATAN).
// `factor` x |cos(angle)|, the cosine kept at the FPU's full precision (only the product is rounded).
double TimesAbsCos(double factor, double angle)
{
    double result;
    __asm {
        fld factor
        fld angle
        fcos
        fabs
        fmulp st(1), st
        fstp result
    }
    return result;
}
double X87Atan2(double y, double x)
{
    double result;
    __asm {
        fld y
        fld x
        fpatan
        fstp result
    }
    return result;
}

// A stick's data byte with 0x80 as the centre, moved 50 towards 0 (crossing it: there's no dead band) and / 77;
// exactly centred is 0. (-0x80 counts as -0x7F.)
void ScaleStick(uint8_t byte, float* out)
{
    int value = byte - 0x80;
    if (value == 0) {
        *out = 0.0f;
        return;
    }
    if (value == -0x80)
        value = -0x7F;
    int moved = value + (value > 0 ? -50 : 50);
    *out = static_cast<float>(moved * static_cast<double>(FloatFromBits(0x3C54C77B)));
}

// The button value of a pressure byte: any pressure counts as full.
int Full(bool down) { return down ? 0xFF : 0; }

// A lock of the second stick stays while it is set, the stick isn't centred, the stick is in the lock's
// quarter and the time hasn't passed.
void UpdateLock(uint32_t& lock, float length, bool inQuarter)
{
    if (lock == 0)
        return;
    if (length != 0.0f && inQuarter && Now() <= static_cast<double>(lock))
        return;
    lock = 0;
}

} // namespace

// 0x0046B7E0
void CControlButton::ResetActivation()
{
    m_fired = 0;
}

// 0x0046B7F0
void CControlButton::ForgetLetGo()
{
    m_letGo = 0;
}

// 0x0046B800
void CControlButton::ChangeTitle(const char* name)
{
    m_label = Checksums::HashFromText(name);
}

// 0x0046B820
void CControlButton::TickFrame(int pressure)
{
    if (m_lockedUntil != 0.0f) {
        // (compared as the original: a NaN lock keeps holding presses off)
        if (pressure > kPressedAbove && !(Now() > static_cast<double>(m_lockedUntil)))
            return;
        m_lockedUntil = 0.0f;
    }
    m_pressAmount = pressure;
    if (pressure > kPressedAbove) {
        if (!m_heldDown) {
            m_pressTime = Clock();
            m_fired = 1;
        }
        m_heldDown = 1;
    } else {
        if (m_heldDown) {
            m_releaseTime = Clock();
            m_letGo = 1;
        }
        m_heldDown = 0;
    }
}

// 0x0046B8B0
void CControlButton::SetHeldDown(uint8_t down)
{
    if (down) {
        if (!m_heldDown) {
            m_pressTime = Clock();
            m_fired = 1;
        }
    } else if (m_heldDown) {
        m_letGo = 1;
    }
    m_heldDown = down;
}

// 0x0046B900
int CControlButton::ReadLetGoTime()
{
    int released = m_releaseTime;
    return FrameClock::GameClockElapsed() + (g_clockOffset - released);
}

// 0x0046B920
int CControlButton::GetHeldMillis()
{
    int pressed = m_pressTime;
    return FrameClock::GameClockElapsed() + (g_clockOffset - pressed);
}

namespace {

// Every button of the pad with the name scripts ask for it by (each name's checksum is what ReadButton matches).
struct PadButtonName {
    CControlButton CPadSnapshot::*button;
    const char* name;
};
constexpr PadButtonName kPadButtons[] = {
    {&CPadSnapshot::m_upDir, "UP"},          {&CPadSnapshot::m_btnDown, "DOWN"},     {&CPadSnapshot::m_leftSide, "LEFT"},
    {&CPadSnapshot::m_rightSide, "RIGHT"},    {&CPadSnapshot::m_buttonBox, "SQUARE"}, {&CPadSnapshot::m_buttonRound, "CIRCLE"},
    {&CPadSnapshot::m_buttonWedge, "TRIANGLE"}, {&CPadSnapshot::m_posX, "X"},       {&CPadSnapshot::m_shoulderLeftA, "L1"},
    {&CPadSnapshot::m_shoulderLeftB, "L2"},          {&CPadSnapshot::m_stickClickLeft, "L3"},         {&CPadSnapshot::m_shoulderRightA, "R1"},
    {&CPadSnapshot::m_shoulderRightB, "R2"},          {&CPadSnapshot::m_stickClickRight, "R3"},         {&CPadSnapshot::m_outset, "START"},
    {&CPadSnapshot::m_buttonChoose, "SELECT"},
};

} // namespace

// 0x0046B940
CPadSnapshot* CPadSnapshot::PrepareAll()
{
    for (const PadButtonName& entry : kPadButtons)
        (this->*entry.button).ChangeTitle(entry.name);
    m_lock[0] = m_lock[1] = 0;
    return this;
}

// 0x0046BB50: every button let go (a held one records its release), the sticks centred.
void CPadSnapshot::Reset()
{
    for (const PadButtonName& entry : kPadButtons)
        (this->*entry.button).TickFrame(0);
    for (float& f : m_stick)
        f = 0.0f;
    for (float& f : m_scaledStick)
        f = 0.0f;
    m_polar[0] = {0.0f, 0.0f};
    m_polar[1] = {0.0f, 0.0f};
    // NOT ORIGINAL - THAW's BMX (the feature on): THAW's stick buttons beside the pad up too.
    if (Skater::ThawBmx::IsSwitchedOn())
        Skater::ThawBmx::UpdatePadSticks(this, nullptr);
}

// 0x0046BC60
void CPadSnapshot::Clear()
{
    Reset();
    CControlButton* both[] = {&m_upDir, &m_btnDown, &m_leftSide, &m_rightSide, &m_shoulderLeftA, &m_shoulderLeftB, &m_stickClickLeft, &m_shoulderRightA};
    for (CControlButton* button : both) {
        button->ResetActivation();
        button->ForgetLetGo();
    }
    // (the original's slip: R2's press is taken twice and its release kept, R3's release twice and its press kept)
    m_shoulderRightB.ResetActivation();
    m_shoulderRightB.ResetActivation();
    m_stickClickRight.ForgetLetGo();
    m_stickClickRight.ForgetLetGo();
    CControlButton* rest[] = {&m_buttonBox, &m_buttonRound, &m_posX, &m_buttonWedge, &m_outset, &m_buttonChoose};
    for (CControlButton* button : rest) {
        button->ResetActivation();
        button->ForgetLetGo();
    }
}

// 0x0046BDA0
void CPadSnapshot::TickFrame(const uint8_t* data)
{
    m_upDir.TickFrame(Full(data[6] != 0));
    m_btnDown.TickFrame(Full(data[7] != 0));
    m_leftSide.TickFrame(Full(data[5] != 0));
    m_rightSide.TickFrame(Full(data[4] != 0));
    m_buttonBox.TickFrame(Full(data[0xB] != 0));
    m_buttonRound.TickFrame(Full(data[9] != 0));
    m_posX.TickFrame(Full(data[0xA] != 0));
    m_buttonWedge.TickFrame(Full(data[8] != 0));
    m_shoulderLeftA.TickFrame(Full(data[0xC] != 0));
    m_shoulderRightA.TickFrame(Full(data[0xD] != 0));
    m_shoulderLeftB.TickFrame(Full(data[0xE] != 0));
    m_shoulderRightB.TickFrame(Full(data[0xF] != 0));
    uint32_t bits = *reinterpret_cast<const uint32_t*>(data + 0x1C);
    m_stickClickLeft.TickFrame(Full((bits & 0x200) != 0));
    m_stickClickRight.TickFrame(Full((bits & 0x400) != 0));
    m_buttonChoose.TickFrame(Full((bits & 0x100) != 0));
    // (START isn't read here)

    for (int i = 0; i < 4; ++i)
        ScaleStick(data[i], &m_scaledStick[i]);
    for (int i = 0; i < 4; ++i)
        m_stick[i] = static_cast<float>(data[0x12 + i] - 0x80);

    double x = m_stick[0], y = m_stick[1];
    m_polar[0].length = static_cast<float>(std::sqrt(x * x + y * y));
    m_polar[0].angle = static_cast<float>(X87Atan2(m_stick[0], -static_cast<double>(m_stick[1])));

    x = m_stick[2];
    y = m_stick[3];
    double length = std::sqrt(y * y + x * x);
    m_polar[1].length = static_cast<float>(length);
    // (compared unrounded, as the original's)
    if (length > FloatFromBits(0x3A83126F)) {
        m_polar[1].angle = static_cast<float>(X87Atan2(m_stick[2], -static_cast<double>(m_stick[3])));
    } else {
        // the d-pad stands in for a centred stick
        uint8_t up = data[6] != 0, down = data[7] != 0, left = data[5] != 0, right = data[4] != 0;
        if (up || down || left || right) {
            m_polar[1].angle = DPadAngle(up, down, left, right);
            m_polar[1].length = 127.0f;
        } else {
            m_polar[1].angle = 0.0f;
            m_polar[1].length = 0.0f;
        }
    }

    double angle = std::fabs(static_cast<double>(m_polar[1].angle));
    UpdateLock(m_lock[0], m_polar[1].length, angle < kQuarterPi);
    UpdateLock(m_lock[1], m_polar[1].length, static_cast<double>(kPi) - angle < kQuarterPi);
    // NOT ORIGINAL - THAW's BMX (the feature on): THAW's pad's eight stick buttons (its 0x596CA0), beside the pad -
    // game/rider/thaw_bmx.h. Original behaviour: none.
    if (Skater::ThawBmx::IsSwitchedOn())
        Skater::ThawBmx::UpdatePadSticks(this, data);
}

// 0x0046C270
CControlButton* CPadSnapshot::ReadButton(uint32_t name)
{
    // (the original's order)
    CControlButton* buttons[] = {&m_upDir, &m_btnDown, &m_leftSide, &m_rightSide, &m_buttonBox, &m_buttonRound, &m_buttonWedge, &m_posX,
                                 &m_shoulderLeftA, &m_shoulderLeftB, &m_stickClickLeft, &m_shoulderRightA, &m_shoulderRightB, &m_stickClickRight, &m_outset, &m_buttonChoose};
    for (CControlButton* button : buttons) {
        if (button->m_label == name)
            return button;
    }
    // NOT ORIGINAL - THAW's BMX (the feature on): THAW's stick buttons by name (leftup ... rightright - its bike
    // scripts' DoBalanceTrick ButtonA=leftright, HeldLongerThan button=leftdown). Original behaviour: null.
    if (Skater::ThawBmx::IsSwitchedOn())
        return static_cast<CControlButton*>(Skater::ThawBmx::PadStickButton(this, name));
    return nullptr;
}

// 0x0046BA50
int DPadDirection(uint8_t up, uint8_t down, uint8_t left, uint8_t right)
{
    // (0x0067D8C8)
    static constexpr int kDirections[16] = {0, 1, 2, 2, 3, 5, 7, 7, 4, 6, 8, 8, 4, 6, 8, 8};
    return kDirections[up + 2 * (down + 2 * (left + 2 * right))];
}

// 0x0046BA80
float DPadAngle(uint8_t up, uint8_t down, uint8_t left, uint8_t right)
{
    // (the original's static table 0x006F2270, filled on the first call; up and down together count as down,
    // left and right together as right)
    static const float kAngles[16] = {
        0.0f, 0.0f, kPi, kPi,
        -kHalfPi, -kQuarterPi, -kThreeQuarterPi, -kThreeQuarterPi,
        kHalfPi, kQuarterPi, kThreeQuarterPi, kThreeQuarterPi,
        kHalfPi, kQuarterPi, kThreeQuarterPi, kThreeQuarterPi,
    };
    return kAngles[up + 2 * (down + 2 * (left + 2 * right))];
}

// 0x0046C370
double __stdcall StickLength(float x, float y, float angle)
{
    double nx = x / static_cast<double>(x > 0.0f ? 127.0f : 128.0f);
    double ny = y / static_cast<double>(y > 0.0f ? 127.0f : 128.0f);
    // the angle brought into the quarter around up / down, then around left / right
    double a = angle;
    double toGate;
    if (a > -static_cast<double>(kQuarterPi) && a < kQuarterPi)
        toGate = a;
    else if (a > kQuarterPi && a < kThreeQuarterPi)
        toGate = a - kHalfPi;
    else if (std::fabs(a) > kThreeQuarterPi)
        toGate = std::fabs(a) - kPi;
    else
        toGate = a + kHalfPi;
    double length = TimesAbsCos(std::sqrt(ny * ny + nx * nx), toGate) * static_cast<double>(FloatFromBits(0x3F969696));
    return length > 1.0 ? 1.0 : length; // (unrounded: the original leaves it in ST0)
}

} // namespace Obj
