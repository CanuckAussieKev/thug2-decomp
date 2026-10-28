#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The engine's screen effects (Nx::CScreenEffects, 8 bytes; vtable 0x00649A0C - the PC's CPcScreenEffects,
// pc_scene.h, fills in its slots): on / off, and the bloom and depth-of-field settings from script structures, each
// handed to the platform through a virtual slot.
namespace Nx {

class CScreenEffects {
public:
    // 0x0049E2F0: construct - on. Returns this.
    CScreenEffects* Construct();
    // 0x0049E300: the destructor's body.
    void DestroyBody();
    // 0x0049E330 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CScreenEffects* Destroy(unsigned int freeFlags);

    // 0x0049E320 / 0x0049E310: the SetScreenEffectsOn / Off script commands' (the structure and script unused).
    // True.
    bool SetOn(Script::CStruct* params, Script::CScript* script);
    bool SetOff(Script::CStruct* params, Script::CScript* script);
    // 0x0049E360: the bloom from a structure (the time of day's): 0xF649D637 on (slot 2), 0xF68014D9 / 0x6F894563
    // the passes' alphas (slot 4, when either is given; -1 for the other), 0x29461E27 a colour (slot 5) and
    // 0x99A9B716 the cut-off colour (slot 3) - colours as [r g b] arrays, alpha 0. The second argument is unused.
    // True.
    bool SetBloomParams(Script::CStruct* params, int unused);
    // 0x0049E4B0: the depth of field (SetScreenEffectsDOFParams): 0xF649D637 on (slot 6), floats 0xE568F442 (slot
    // 10), 0x3B909E50 (slot 9 - given the first float's value, as the original), 0x29461E27 (slot 11), 0xB7462B1D
    // (slot 12), colours 0x0F3BF443 (slot 8) and 0xFC5C7D61 (slot 7). True.
    bool SetDOFParams(Script::CStruct* params, Script::CScript* script);

    void** vtable;
    bool m_on; // +0x04
    uint8_t pad5[3];
};
static_assert(sizeof(CScreenEffects) == 8, "layout must match the original");

} // namespace Nx
