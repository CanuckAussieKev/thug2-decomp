#pragma once

#include <cstdint>

// The skater's turning in the air: spins (L1/R1, and the other spin buttons) and breaking out of vert - with
// PartyMod's PS2-style controls (partymod.ini UsePS2Controls; Skater::Ps2Controls) built in.
namespace Skater {

// The skater's core physics component (the object 0x0050CA00, the air update, runs on). Its layout is only
// partly known: fields are reached by offset (see rotation.cpp).
class CorePhysicsComponent {
public:
    // 0x00504060: spin in the air. Tap mode (+0x2DA): each tap of the left / right spin button (L1/R1: input
    // +0xA0 / +0x100) queues half a turn (+0x30C), turned off at the skater's spin speed. Otherwise holding one
    // turns once it's been held longer than the spin delay - PartyMod's PS2 controls drop the delay (its NOPs
    // at 0x00504239 / 0x0050430B); then the second pair of spin buttons (input +0x60 / +0x80), ramping up
    // between two held times. With no spin, the
    // skater may be turned back to face along the ramp. The turn goes into the object's matrix, the display
    // matrix and the trick's rotation total (in degrees, shown as an integer).
    void Spin();
    // 0x00507120: break out of vert - both of two buttons held (input +0xC0 / +0x120, PartyMod's "R2/L2"; its
    // PS2 controls take either: its bytes at 0x00507184 / 0x005071AE), or a flag (+0x2D0), or `force`, or the
    // jump button held past a script time with no other button: pushes the skater away from the wall and
    // pitches them forward.
    void BreakVert(bool force);
};

} // namespace Skater
