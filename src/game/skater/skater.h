#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/string.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
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

struct CSkaterProfile;
class CCompositeObjectManager;

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
    // 0x00580030: the core physics' display matrix (its +0x18).
    Mth::Matrix* GetDisplayMatrix();
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
    // 0x00580830: moved to a random restart (0-7, through the skate module; up to 20 tries for one other than the
    // last), through its vtable (+0x3C: SkipToRestart(node, walking)).
    void MoveToRandomRestart();
    // 0x005808A0: its pause state (0 running, 1 frozen, 2 physics paused): the game paused or the physics control
    // told.
    void SetPauseState(int state);
    // 0x005809C0: its shadow's light direction (the model's light, clamped below the horizon), faded in or out
    // over 16 frames as it is in the air or not.
    void UpdateShadowLight(int unused1, int unused2);
    // 0x00580BE0: the ten stats from `profile` (or, in classic mode, the skate module's classic stats).
    void SetStatsFromProfile(CSkaterProfile* profile);
    // 0x00580EB0: its looks and identity from `profile` (in its skater heap): push style, stance, pro, display name,
    // male, skate style, first name, name; its stance flag set again and the flip-and-rotate component told.
    void UpdateFromProfile(CSkaterProfile* profile);
    // 0x00580FF0: its score, a component (0x589E5FB3), its vtable +0x48 reset; the 24 input flags pressed then
    // released; its +0x264's +0x20 reset; the script 0xAE5438AF.
    void ResetInput();
    // 0x005810E0: its state reset (physics control, flip and rotate, its +0x258's resets, the balance displays off,
    // the shadow placed, 0x21FC3301's +0x18 -1).
    void ResetState();
    // 0x005811E0: its net client's id told to its flip-and-rotate component, its +0x250 told; then +0x24C's
    // 0x00419A20.
    void SetUpNetwork();
    // 0x00581260: the board element for `type` (four kinds; their structures say the texture, sizes, model swaps...).
    void ShowBoardElement(uint32_t type);
    // 0x005814D0 / 0x00581510 / 0x00581550: a component's reset (not local); hidden; shown.
    void Reset27C();
    void HideSkater();
    void ShowSkater();
    // 0x00581580: its display name (online: the player's).
    const char* GetDisplayName();
    // 0x005815C0: moved to the node named `node` (no node: only the orientation reset).
    void MoveToNode(uint32_t node);
    // 0x00581760 / 0x005817F0: paused (the displays off, a script, the pad's vibration, the sounds) / unpaused.
    void Pause();
    void Unpause();
    // 0x00581850: set up from `profile`: a local skater's camera (found or made from skatercam_composite_structure),
    // its script, its components (motion, an anim set's, skeleton, model - its geometry loaded in the skater's
    // geometry heap -, shadow, a local skater's input / vibration / two more), Finalize, a local skater's pad
    // settings and SkaterInit, its profile and stats, ... its trick infos and projectiles, its controls' textures.
    void Init(CSkaterProfile* profile);
    // 0x005822A0: Init, then a local skater's network message handlers (when 0x005A11B0 says so), and never suspended.
    void InitFromProfile(CSkaterProfile* profile);
    // 0x005823A0: its state script 0xE740F458 started (with Walk / 0x7B738580 flags), run once if local.
    void RunStateScript(bool walk, bool other);
    // 0x00582510: its script switched to `name` (flip-and-rotate told first) and run once.
    void SwitchToScript(uint32_t name, Script::CStruct* params);
    // 0x005825E0: the CAT info written to `save` (tricks). 0x00582740: the +0x28C list's 70 entries written.
    void WriteCATInfo(Script::CStruct* save);
    void WriteList44B66A9F(Script::CStruct* save);
    // 0x00582800: a projectile launched (LaunchProjectile) from a wrist, at the nearest target ahead (projectile
    // proximity components within Physics_Projectile_Targeting_Dist / _Angle; peds not yet shot, at a bone they
    // choose) when there's one beyond 100.
    void LaunchProjectile();
    // 0x00582D90: the constructor (vtable 0x0064EF48): its skater-list node, local or not, base components, type
    // 0xBAF9B341, number and id (skaters 0 and 1 get tracker aliases), registered with `manager`, its viewport, and
    // its components (a local skater's full set, a remote one's few). Returns this.
    CSkater* Construct(int viewport, CCompositeObjectManager* manager, bool local, uint32_t id, int skaterNumber, int unused);
    // 0x00583400: a local skater's +0x270 and 0x589E5FB3 reset, 0xF27A294D's, the anim blender's; its script deleted.
    void ResetComponents();
    // 0x00583470: restarted (a walking skater back to skating, the script's exit scripts, its physics, its camera,
    // vtable +0x44, ResetComponents, +0x48, +0x4C with `a` and `b`, 0xB1D5A3E1 told).
    void Restart(uint32_t a, uint32_t b);
    // 0x005835D0: its script member functions (KillSkater, AddSkaterToWorld, SkaterIsNamed, ShouldMongo, the
    // sparks, SetCustomRestart / SkipToCustomRestart, IsLocalSkater, GetCameraId, GetStat, GetScriptedStat,
    // MoveToRandomRestart, PlaceBeforeCamera, GetSkaterNumber...); the rest the composite object's.
    bool CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // (the code SkipToCustomRestart and SetCustomRestart share)
    void SkipToCustomRestart();
    // 0x00583DA0 (vtable +0x3C): skipped to the restart node `node` (none: where it is), walking or not: a local
    // skater's input and pad reset, vtable +0x40 with the node, Restart, the camera updated, the node's TriggerScript
    // run; teleported; in three game modes the server told (message 0x57).
    void SkipToRestart(uint32_t node, uint32_t walking);
    // 0x00583FB0: the destructor body. 0x005840E0: the scalar deleting destructor.
    void DestroyBody();
    CSkater* Destroy(unsigned int flags);

    uint32_t m_pushStyle;        // +0x108 (the profile's pushstyle)
    uint32_t m_style;            // +0x10C (skatestyle)
    bool m_goofy;                // +0x110
    bool m_pro;                  // +0x111
    uint8_t pad112[2];
    int m_male;                  // +0x114
    char m_firstName[0x40];      // +0x118
    uint32_t m_name;             // +0x158
    Str::String m_displayName;   // +0x15C
    uint8_t m_unknown168[0x16C - 0x168];
    uint32_t m_lastRestart;      // +0x16C
    bool m_sparks;               // +0x170
    uint8_t pad171[3];
    int m_shadowFade;            // +0x174
    uint8_t m_unknown178[4];
    uint8_t* m_trickInfo[11];    // +0x17C (each: +4 a structure, +8 / +0xC arrays)
    uint8_t m_unknown1A8[0x1C0 - 0x1A8];
    struct Projectile {
        uint32_t name;           // projectile_0 ... projectile_4
        bool active;
        uint8_t m_unknown5[0x13];
    } m_projectiles[5];          // +0x1C0
    uint8_t m_block238[0x24C - 0x238];
    void* m_unknown24C;          // +0x24C
    uint8_t* m_unknown250;       // +0x250
    void* m_shadow;              // +0x254
    void* m_unknown258;          // +0x258
    void* m_unknown25C;          // +0x25C
    uint8_t m_unknown260[4];
    uint8_t* m_unknown264;       // +0x264 (+0x20: reset; +0x490 -> +0x1C its input device)
    void* m_unknown268;          // +0x268 (component 0x187574FA)
    void* m_unknown26C;          // +0x26C (component 0x1859F772)
    uint8_t* m_unknown270;       // +0x270
    uint8_t m_unknown274[4];
    uint8_t* m_scoreHolder;      // +0x278 (+0x18 the score)
    uint8_t* m_unknown27C;       // +0x27C
    uint8_t* m_corePhysics;      // +0x280
    uint8_t* m_state284;         // +0x284
    uint8_t* m_state288;         // +0x288
    void* m_list28C;             // +0x28C
    float m_statOverrides[10];   // +0x290
    int m_skaterNumber;          // +0x2B8
    int m_viewport;              // +0x2BC
    int m_pauseState;            // +0x2C0
    bool m_flag2C4;              // +0x2C4
    uint8_t pad2C5[3];
    Mth::Vector m_customRestartPos;    // +0x2C8 (SetCustomRestart)
    Mth::Matrix m_customRestartMatrix; // +0x2D8
    bool m_customRestartWalking; // +0x318
    bool m_customRestartSet;     // +0x319
    uint8_t pad31A[2];
    uint32_t m_unknown31C;       // +0x31C
    bool m_paused;               // +0x320
    uint8_t pad321[3];
};
static_assert(offsetof(CSkater, m_style) == 0x10C && offsetof(CSkater, m_name) == 0x158 &&
                  offsetof(CSkater, m_trickInfo) == 0x17C && offsetof(CSkater, m_shadow) == 0x254 &&
                  offsetof(CSkater, m_statOverrides) == 0x290 && offsetof(CSkater, m_skaterNumber) == 0x2B8 &&
                  offsetof(CSkater, m_flag2C4) == 0x2C4 && offsetof(CSkater, m_displayName) == 0x15C &&
                  offsetof(CSkater, m_unknown264) == 0x264 && offsetof(CSkater, m_customRestartPos) == 0x2C8 &&
                  offsetof(CSkater, m_customRestartWalking) == 0x318 && offsetof(CSkater, m_projectiles) == 0x1C0 &&
                  offsetof(CSkater, m_block238) == 0x238 && offsetof(CSkater, m_unknown270) == 0x270 && offsetof(CSkater, m_paused) == 0x320,
              "layout must match the original");

// 0x005802E0: the physics structure for what `skater` rides (null or not a skater: skater_physics).
Script::CStruct* __cdecl GetPhysicsStructure(CCompositeObject* skater);
// 0x00580400 / 0x00580440: a physics value from `skater`'s structure, else the global script value.
float __cdecl GetPhysicsFloat(uint32_t name, int assert, CCompositeObject* skater);
int __cdecl GetPhysicsInt(uint32_t name, int assert, CCompositeObject* skater);

} // namespace Obj
