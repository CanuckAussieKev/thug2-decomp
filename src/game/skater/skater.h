#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

// Pieces of the skater object (Obj::CSkater, a composite object; 0x0057FE30-0x00580BD8): its stats (overrides at
// +0x290, the cheats and online rules on top), the physics structure for what it rides (skater_physics, or a
// vehicle's), values scaled by a stat (pair ranges, the switch / difficulty pairs, limits), sparks, its camera and
// number, matching a profile's description, the CAT and chapter info a save carries, pausing for its physics control,
// and the light direction its shadow is cast by. The rest of the skater's code is elsewhere (and not decompiled
// yet): only the fields these use are named.
namespace Script {
class CStruct;
}

namespace Obj {

class CSkater : public CCompositeObject {
public:
    // 0x0057FE30: stat `index`'s override (+0x290), kept within 0-10 when 0x005A1180 says so.
    void SetStatOverride(int index, float value);
    // 0x00580490: stat `index` (the override, the default stats if none, the skate module's forced value; online
    // limits; CHEAT_STATS_13: 15; +3 with the score's bonus flag).
    float GetStat(int index);
    // 0x005805B0: a value from `params`: its unnamed pair scaled by the stat named by its unnamed integer (10 if
    // none), times its switch pair (by the switch stat, 0-1) when riding switch, times its diff pair's first or
    // second for game difficulty 0 or 2, kept within its limit (the pair's direction says which side).
    float GetScaledValue(Script::CStruct* params);
    // 0x00580790: the physics structure's `name` structure (or the global one) as GetScaledValue's. 0x005807F0:
    // `params`' `name` structure's.
    float GetScaledPhysics(uint32_t name);
    float GetScaledParam(uint32_t name, Script::CStruct* params);

    // 0x0057FEB0: its viewport's camera. 0x0057FEC0: its skater number. 0x00580880: its skater camera (skatercam0
    // or skatercam1, by viewport).
    void* GetCamera();
    int GetSkaterNumber();
    CObject* GetSkaterCamera();
    // 0x00580040: its +0x238 block. 0x00580050: the flag at +0x2C4. 0x00580480: its score.
    void* GetBlock238();
    bool GetFlag2C4();
    void* GetScore();
    // 0x0057FED0: whether it matches `params`' description (is_pro, the custom flag, is_male / is_female, is_named,
    // stance, style).
    bool MatchesParams(Script::CStruct* params);
    // 0x0057FFE0 / 0x00580000: sparks on (SkaterParticles_SparksOn) / off (SkaterParticles_SparksOff, if on).
    void SparksOn();
    void SparksOff();
    // 0x00580060: the CAT info (its tricks: each one's other params, animations and rotations), then
    // kill_load_premade_cats. 0x00580150: the 70 entries of its +0x28C list from `params`.
    void LoadCATInfo(Script::CStruct* params);
    void LoadList44B66A9F(Script::CStruct* params);
    // 0x005801B0: the chapter arrays (CHAPTER_STATUS, CHAPTER_INTROS_VIEWED, CHAPTER_OUTTROS_VIEWED) added to `save`.
    // 0x00580210: copied back from `save`.
    void WriteChapterInfo(Script::CStruct* save);
    void ReadChapterInfo(Script::CStruct* save);
    // 0x005802C0: 0x0042BD60 with this and `value`.
    void Set42BD60(float value);
    // 0x00580830: a new random board (0-7, through the skate module; up to 20 tries for one other than the
    // current), set through its vtable (+0x3C).
    void PickRandomBoard();
    // 0x005808A0: its pause state (0 running, 1 frozen, 2 physics paused): the game paused or the physics control
    // told.
    void SetPauseState(int state);
    // 0x005809C0: its shadow's light direction (the model's light, clamped below the horizon), faded in or out
    // over 16 frames as it is in the air or not.
    void UpdateShadowLight(int unused1, int unused2);

    uint8_t m_unknown108[0x10C - sizeof(CCompositeObject)];
    uint32_t m_style;            // +0x10C
    bool m_goofy;                // +0x110
    bool m_pro;                  // +0x111
    uint8_t pad112[2];
    int m_male;                  // +0x114
    uint8_t m_unknown118[0x158 - 0x118];
    uint32_t m_name;             // +0x158
    uint8_t m_unknown15C[0x16C - 0x15C];
    uint32_t m_board;            // +0x16C
    bool m_sparks;               // +0x170
    uint8_t pad171[3];
    int m_shadowFade;            // +0x174
    uint8_t m_unknown178[4];
    uint8_t* m_trickInfo[11];    // +0x17C (each: +4 a structure, +8 / +0xC arrays)
    uint8_t m_unknown1A8[0x238 - 0x1A8];
    uint8_t m_block238[0x254 - 0x238];
    void* m_shadow;              // +0x254
    uint8_t m_unknown258[0x278 - 0x258];
    uint8_t* m_scoreHolder;      // +0x278 (+0x18 the score)
    uint8_t m_unknown27C[4];
    uint8_t* m_corePhysics;      // +0x280
    uint8_t* m_state284;         // +0x284
    uint8_t* m_state288;         // +0x288
    void* m_list28C;             // +0x28C
    float m_statOverrides[10];   // +0x290
    int m_skaterNumber;          // +0x2B8
    int m_viewport;              // +0x2BC
    int m_pauseState;            // +0x2C0
    bool m_flag2C4;              // +0x2C4
};
static_assert(offsetof(CSkater, m_style) == 0x10C && offsetof(CSkater, m_name) == 0x158 &&
                  offsetof(CSkater, m_trickInfo) == 0x17C && offsetof(CSkater, m_shadow) == 0x254 &&
                  offsetof(CSkater, m_statOverrides) == 0x290 && offsetof(CSkater, m_skaterNumber) == 0x2B8 &&
                  offsetof(CSkater, m_flag2C4) == 0x2C4,
              "layout must match the original");

// 0x005802E0: the physics structure for what `skater` rides (null or not a skater: skater_physics).
Script::CStruct* __cdecl GetPhysicsStructure(CCompositeObject* skater);
// 0x00580400 / 0x00580440: a physics value from `skater`'s structure, else the global script value.
float __cdecl GetPhysicsFloat(uint32_t name, int assert, CCompositeObject* skater);
int __cdecl GetPhysicsInt(uint32_t name, int assert, CCompositeObject* skater);

} // namespace Obj
