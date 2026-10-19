// A controller's buttons and sticks (0x0046B7E0-0x0046C470). See control_pad.h.

#include "game/obj/control_pad.h"

#include <cmath>
#include <cstring>

#include "game/core/crc.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr int kPressedAbove = 0x40;

// The game clock with its offset (0x00696D60).
int Clock() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }

// Tmr::GetTime as the original compares it: unsigned, as a float would hold it.
double Now() { return static_cast<double>(Tmr::GetTime()); }

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

// The FPU's own cosine and arctangent (as the original's FCOS / FPATAN), at double precision.
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
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
void CControlButton::ClearTrigger()
{
    m_triggered = 0;
}

// 0x0046B7F0
void CControlButton::ClearRelease()
{
    m_released = 0;
}

// 0x0046B800
void CControlButton::SetName(const char* name)
{
    m_name = Crc::GenerateCRCFromString(name);
}

// 0x0046B820
void CControlButton::Update(int pressure)
{
    if (m_lockedUntil != 0.0f) {
        // (compared as the original: a NaN lock keeps holding presses off)
        if (pressure > kPressedAbove && !(Now() > static_cast<double>(m_lockedUntil)))
            return;
        m_lockedUntil = 0.0f;
    }
    m_pressure = pressure;
    if (pressure > kPressedAbove) {
        if (!m_pressed) {
            m_pressTime = Clock();
            m_triggered = 1;
        }
        m_pressed = 1;
    } else {
        if (m_pressed) {
            m_releaseTime = Clock();
            m_released = 1;
        }
        m_pressed = 0;
    }
}

// 0x0046B8B0
void CControlButton::SetPressed(uint8_t down)
{
    if (down) {
        if (!m_pressed) {
            m_pressTime = Clock();
            m_triggered = 1;
        }
    } else if (m_pressed) {
        m_released = 1;
    }
    m_pressed = down;
}

// 0x0046B900
int CControlButton::GetReleasedTime()
{
    int released = m_releaseTime;
    return Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - released);
}

// 0x0046B920
int CControlButton::GetPressedTime()
{
    int pressed = m_pressTime;
    return Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - pressed);
}

// 0x0046B940
CControlPad* CControlPad::Init()
{
    m_up.SetName("UP");
    m_down.SetName("DOWN");
    m_left.SetName("LEFT");
    m_right.SetName("RIGHT");
    m_square.SetName("SQUARE");
    m_circle.SetName("CIRCLE");
    m_triangle.SetName("TRIANGLE");
    m_x.SetName("X");
    m_L1.SetName("L1");
    m_L2.SetName("L2");
    m_L3.SetName("L3");
    m_R1.SetName("R1");
    m_R2.SetName("R2");
    m_R3.SetName("R3");
    m_start.SetName("START");
    m_select.SetName("SELECT");
    m_lock[0] = 0;
    m_lock[1] = 0;
    return this;
}

// 0x0046BB50
void CControlPad::Reset()
{
    m_up.Update(0);
    m_down.Update(0);
    m_left.Update(0);
    m_right.Update(0);
    m_L1.Update(0);
    m_L2.Update(0);
    m_L3.Update(0);
    m_R1.Update(0);
    m_R2.Update(0);
    m_R3.Update(0);
    m_square.Update(0);
    m_circle.Update(0);
    m_x.Update(0);
    m_triangle.Update(0);
    m_start.Update(0);
    m_select.Update(0);
    for (float& f : m_stick)
        f = 0.0f;
    for (float& f : m_scaledStick)
        f = 0.0f;
    m_polar[0] = {0.0f, 0.0f};
    m_polar[1] = {0.0f, 0.0f};
}

// 0x0046BC60
void CControlPad::Clear()
{
    Reset();
    CControlButton* both[] = {&m_up, &m_down, &m_left, &m_right, &m_L1, &m_L2, &m_L3, &m_R1};
    for (CControlButton* button : both) {
        button->ClearTrigger();
        button->ClearRelease();
    }
    // (the original's slip: R2's press is taken twice and its release kept, R3's release twice and its press kept)
    m_R2.ClearTrigger();
    m_R2.ClearTrigger();
    m_R3.ClearRelease();
    m_R3.ClearRelease();
    CControlButton* rest[] = {&m_square, &m_circle, &m_x, &m_triangle, &m_start, &m_select};
    for (CControlButton* button : rest) {
        button->ClearTrigger();
        button->ClearRelease();
    }
}

// 0x0046BDA0
void CControlPad::Update(const uint8_t* data)
{
    m_up.Update(Full(data[6] != 0));
    m_down.Update(Full(data[7] != 0));
    m_left.Update(Full(data[5] != 0));
    m_right.Update(Full(data[4] != 0));
    m_square.Update(Full(data[0xB] != 0));
    m_circle.Update(Full(data[9] != 0));
    m_x.Update(Full(data[0xA] != 0));
    m_triangle.Update(Full(data[8] != 0));
    m_L1.Update(Full(data[0xC] != 0));
    m_R1.Update(Full(data[0xD] != 0));
    m_L2.Update(Full(data[0xE] != 0));
    m_R2.Update(Full(data[0xF] != 0));
    uint32_t bits = *reinterpret_cast<const uint32_t*>(data + 0x1C);
    m_L3.Update(Full((bits & 0x200) != 0));
    m_R3.Update(Full((bits & 0x400) != 0));
    m_select.Update(Full((bits & 0x100) != 0));
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
}

// 0x0046C270
CControlButton* CControlPad::GetButton(uint32_t name)
{
    // (the original's order)
    CControlButton* buttons[] = {&m_up, &m_down, &m_left, &m_right, &m_square, &m_circle, &m_triangle, &m_x,
                                 &m_L1, &m_L2, &m_L3, &m_R1, &m_R2, &m_R3, &m_start, &m_select};
    for (CControlButton* button : buttons) {
        if (button->m_name == name)
            return button;
    }
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
float __stdcall StickLength(float x, float y, float angle)
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
    double gate = std::fabs(X87Cos(toGate));
    double length = std::sqrt(ny * ny + nx * nx) * gate * static_cast<double>(FloatFromBits(0x3F969696));
    return length > 1.0 ? 1.0f : static_cast<float>(length);
}

} // namespace Obj

REPLACE(0x0046B7E0, &Obj::CControlButton::ClearTrigger);
REPLACE(0x0046B7F0, &Obj::CControlButton::ClearRelease);
REPLACE(0x0046B800, &Obj::CControlButton::SetName);
REPLACE(0x0046B820, &Obj::CControlButton::Update);
REPLACE(0x0046B8B0, &Obj::CControlButton::SetPressed);
REPLACE(0x0046B900, &Obj::CControlButton::GetReleasedTime);
REPLACE(0x0046B920, &Obj::CControlButton::GetPressedTime);
REPLACE(0x0046B940, &Obj::CControlPad::Init);
REPLACE(0x0046BA50, Obj::DPadDirection);
REPLACE(0x0046BA80, Obj::DPadAngle);
REPLACE(0x0046BB50, &Obj::CControlPad::Reset);
REPLACE(0x0046BC60, &Obj::CControlPad::Clear);
REPLACE(0x0046BDA0, &Obj::CControlPad::Update);
REPLACE(0x0046C270, &Obj::CControlPad::GetButton);
REPLACE(0x0046C370, Obj::StickLength);
