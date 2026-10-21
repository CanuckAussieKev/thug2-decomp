#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's sound component (SkaterSound, type 0x187574FA, vtable 0x0064C0C4, 0x28 bytes): the one-shot sounds of
// the skater's terrain - jump, land, bonk, cess (and on a rail grind / slide jump and land) - played at the skater
// with a volume by the speed. The finalize physics component keeps its terrain and slide flag up to date. From the
// disassembly of 0x00518C80-0x00519129 (0x00518EA0-0x00518FA0 aren't Ghidra functions).
//
// Vtable: 0 Destroy, 1 the shared empty 0x0051B970, 2 InitFromStructure, 12 CallMemberFunction.
namespace Obj {

class CSkaterSoundComponent : public CBaseComponent {
public:
    // 0x00518E20 (registered by RegisterComponentTypes): a new one (0 if out of memory): full speed 1, volume_mult 1,
    // not sliding.
    static CBaseComponent* __cdecl Create();
    // 0x00518C80: the destructor body (the base's).
    void DestroyBody();
    // 0x00518C90 (vtable slot 0)
    CSkaterSoundComponent* Destroy(unsigned int flags);
    // 0x00518CC0 (vtable +0x08): volume_mult.
    void InitFromStructure(Script::CStruct* params);
    // 0x00518FD0 (vtable +0x30): PlayJumpSound / PlayLandSound (on the ground or in the air, or on a rail: the speed
    // - |vy| for a landing on the ground -, else nothing), PlayBonkSound, PlayCessSound (the speed) - 1; 2 for any
    // other name.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // `speed` (a fraction of full speed) as a percentage: the volume times volume_mult; the sound of `terrain` in
    // the state's table played at the skater:
    // 0x00518CF0: a landing (TABLE_LAND; on a rail TABLE_GRINDLAND / TABLE_SLIDELAND).
    void PlayLand(float speed, int terrain);
    // 0x00518D60: a jump (TABLE_JUMP; in `state` 4, a rail, TABLE_GRINDJUMP / TABLE_SLIDEJUMP).
    void PlayJump(float speed, int terrain, int state);
    // 0x00518DD0: a bonk (TABLE_BONK).
    void PlayBonk(float speed, int terrain);
    // 0x00518EA0 / 0x00518FA0: a landing / jump on the terrain now (on the ground or in the air m_lastGround).
    void PlayLandHere(float speed);
    void PlayJumpHere(float speed);
    // 0x00518EE0: a jump in this state.
    void PlayJumpInState(float speed, int terrain);
    // 0x00518F00 / 0x00518F50: a bonk / a cess (TABLE_CESS) on the terrain now.
    void PlayBonkHere(float speed);
    void PlayCessHere(float speed);

    int m_state;           // +0x18 the skater's state (0 ground, 1 air, 4 rail)
    uint8_t m_terrain;     // +0x1C
    uint8_t m_lastGround;  // +0x1D the terrain the ground and air sounds use
    uint8_t m_sliding;     // +0x1E on a rail: a slide, not a grind
    uint8_t pad1F;
    float m_fullSpeed;     // +0x20 the speed that counts as 1
    float m_volumeMult;    // +0x24 volume_mult
};
static_assert(offsetof(CSkaterSoundComponent, m_state) == 0x18 && offsetof(CSkaterSoundComponent, m_terrain) == 0x1C &&
                  offsetof(CSkaterSoundComponent, m_sliding) == 0x1E &&
                  offsetof(CSkaterSoundComponent, m_fullSpeed) == 0x20 &&
                  offsetof(CSkaterSoundComponent, m_volumeMult) == 0x24 && sizeof(CSkaterSoundComponent) == 0x28,
              "layout must match the original (0x00518E20 allocates 0x28 bytes)");

} // namespace Obj
