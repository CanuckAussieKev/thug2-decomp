#pragma once

// A controller's state as the skater (and the editors) read it (0x0046B7E0-0x0046C470): sixteen buttons, each
// knowing when it went down / up and its pressure, both sticks (raw, dead-banded, and as angle + length), and two
// timed holds of the second stick. The names are THPS's (CSkaterPad / CSkaterButton); the
// code here is THUG2's.

#include <cstddef>
#include <cstdint>

namespace GameObjects {

class CControlButton {
public:
    // 0x0046B7E0 / 0x0046B7F0: the press / release events taken.
    void ResetActivation();
    void ForgetLetGo();
    // 0x0046B800: the button's name (the checksum of `name`).
    void ChangeTitle(const char* name);
    // 0x0046B820: the new pressure (0-255; down above 0x40). While the button is locked (+0x1C, a FrameClock::GetTime
    // millisecond time, 0 = not locked), a press isn't taken until that time has passed; any update that isn't a
    // press ends the lock.
    void TickFrame(int pressure);
    // 0x0046B8B0: down or up as `down` (the byte itself is stored), without the pressure; a release here isn't
    // timed.
    void SetHeldDown(uint8_t down);
    // 0x0046B900 / 0x0046B920: game-clock milliseconds since the button was last released / pressed.
    int ReadLetGoTime();
    int GetHeldMillis();

    uint8_t m_heldDown;       // +0x00
    uint8_t m_unknown1[3];
    int m_pressTime;         // +0x04: the game clock (with its offset) at the last press
    int m_releaseTime;       // +0x08
    int m_pressAmount;          // +0x0C
    uint8_t m_fired;     // +0x10: went down (until ResetActivation)
    uint8_t m_unknown11[3];
    int m_letGo;          // +0x14: went up (until ForgetLetGo)
    uint32_t m_label;         // +0x18
    float m_lockedUntil;     // +0x1C
};
static_assert(sizeof(CControlButton) == 0x20, "layout must match the original");

class CPadSnapshot {
public:
    // 0x0046B940: the buttons' names; no locks.
    CPadSnapshot* PrepareAll();
    // 0x0046BB50: every button updated as released (pressure 0), the sticks zeroed.
    void Reset();
    // 0x0046BC60: Reset, and the buttons' events taken - except, as the original, R2's release and R3's press
    // (R2's press and R3's release are taken twice instead).
    void Clear();
    // 0x0046BDA0: the buttons and sticks from the controller's data (the PS2 pad layout: pressures 4-0xF, the
    // sticks 0-3 and 0x12-0x15 around 0x80, more buttons in the bits of +0x1C).
    void TickFrame(const uint8_t* data);
    // 0x0046C270: the button named `name` (null if none).
    CControlButton* ReadButton(uint32_t name);

    CControlButton m_upDir;         // +0x000
    CControlButton m_btnDown;       // +0x020
    CControlButton m_leftSide;       // +0x040
    CControlButton m_rightSide;      // +0x060
    CControlButton m_shoulderLeftA;         // +0x080
    CControlButton m_shoulderLeftB;         // +0x0A0
    CControlButton m_stickClickLeft;         // +0x0C0
    CControlButton m_shoulderRightA;         // +0x0E0
    CControlButton m_shoulderRightB;         // +0x100
    CControlButton m_stickClickRight;         // +0x120
    CControlButton m_buttonRound;     // +0x140
    CControlButton m_buttonBox;     // +0x160
    CControlButton m_buttonWedge;   // +0x180
    CControlButton m_posX;          // +0x1A0
    CControlButton m_outset;      // +0x1C0
    CControlButton m_buttonChoose;     // +0x1E0
    float m_stick[4];            // +0x200: two sticks' x, y: the data's 0x12-0x15 - 0x80
    float m_scaledStick[4];      // +0x210: the data's 0-3 - 0x80, moved 50 towards 0 (past it: no dead band), / 77
    // +0x220: each stick as an angle (atan2(x, -y): 0 = up, clockwise positive) and a length; the second is the
    // d-pad's (length 127) while that stick is centred.
    struct Polar {
        float angle;
        float length;
    } m_polar[2];
    // +0x230: two FrameClock::GetTime times (0 = none; set by other code) for the second stick held up / down: Update
    // clears each once it has passed, or once the stick is centred or leaves that quarter.
    uint32_t m_lock[2];
};
static_assert(offsetof(CPadSnapshot, m_stick) == 0x200 && offsetof(CPadSnapshot, m_polar) == 0x220 &&
                  offsetof(CPadSnapshot, m_lock) == 0x230,
              "layout must match the original");

// 0x0046BA50: the d-pad direction number (0 none, 1 up, 2 down, 3 left, 4 right, 5-8 the diagonals ...) of the
// four directions held.
int DPadDirection(uint8_t up, uint8_t down, uint8_t left, uint8_t right);
// 0x0046BA80: the d-pad's angle (radians, 0 = up, clockwise positive) for the four directions held.
float DPadAngle(uint8_t up, uint8_t down, uint8_t left, uint8_t right);
// 0x0046C370: a stick's length 0-1 (x, y around 0, -128..127), squared off towards the corners of the stick's
// square gate by the angle (x 1/0.85, at most 1), unrounded in ST0. (__stdcall: the original pops its arguments.)
double __stdcall StickLength(float x, float y, float angle);

extern int g_clockOffset;

} // namespace Obj
