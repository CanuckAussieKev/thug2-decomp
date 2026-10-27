#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"
#include "game/skater/manual.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's balance trick component (SkaterBalanceTrick, type 0x589E5FB3, vtable 0x0064BA30, 0x188 bytes; made by
// 0x004F9170): the balance of the trick being balanced - a manual (or nose manual), a grind (or slide), a lip trick or
// a skitch, one CBalance each (manual.h) - started by DoBalanceTrick, tweaked by AdjustBalance, its meter switched on
// and off; the physics values of its type (GrindParams / ManualParams / SkitchParams / LipParams, or the structure
// DoBalanceTrick was given). From the disassembly of 0x004F8CD0-0x004F9960.
//
// Vtable: 0 Destroy, 2 InitFromStructure, 3 Finalize, 12 CallMemberFunction; the rest the base's / shared (1 the empty
// 0x0051B970, 4 / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 0x005BAB90, 11 0x0042BB20).
namespace Obj {

class CAnimBlenderComponent;

class CSkaterBalanceTrickComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x589E5FB3;
    // The balance trick types (m_trick).
    static constexpr uint32_t kManual = 0xEF24413B, kNoseManual = 0x0AC90769, kGrind = 0x255ED86F, kSlide = 0x8D10119D,
                              kSkitch = 0x3506CE64, kLip = 0xA549B57B;

    // 0x004F9170 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004F8CD0 (registered with the type): the member function table 0x00683238 (DoingBalanceTrick).
    static void __cdecl RegisterMemberFunctions();
    // 0x004F8F80: the constructor (returns this): the four balances made, no physics structure, no anim component.
    CSkaterBalanceTrickComponent* Construct();
    // 0x004F8CF0: the destructor body (the balances', last first, then the base's). 0x004F9010 (vtable slot 0).
    void DestroyBody();
    CSkaterBalanceTrickComponent* Destroy(unsigned int flags);
    // 0x004F9040 (vtable slot 2): each balance given the skater. `params` isn't read.
    void InitFromStructure(Script::CStruct* params);
    // 0x004F9080 (vtable slot 3): the anim blender component (0x7149B753).
    void Finalize();
    // 0x004F9420 (vtable slot 12): SwitchOffBalanceMeter, SwitchOnBalanceMeter, StartBalanceTrick, AdjustBalance,
    // SetWobbleDetails, StopBalanceTrick, DoBalanceTrick; 2 for other names.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004F8D70 (the member function DoingBalanceTrick, from the table; not a Ghidra function): whether one was
    // started (StartBalanceTrick). Its parameters aren't read.
    int DoingBalanceTrick(Script::CStruct* params, Script::CScript* script);

    // 0x004F8D80 / 0x004F8DB0: every balance's best time cleared / kept.
    void ClearBestTimes();
    void KeepBestTimes();
    // 0x004F8DE0: every balance reset (manual, grind, skitch, lip); no trick, not started.
    void Reset();
    // 0x004F8EE0: every balance stopped (manual, grind, lip, skitch); no trick, not started.
    void Stop();
    // 0x004F8E20: the current balance's two buttons into `buttons` (`*count` 2), or `*count` 0 without one.
    void GetButtons(int* count, uint32_t* buttons) const;
    // 0x004F8F20: the current balance's lean (0 without one; the original also leaves the balance in EAX).
    float GetLean() const;
    // 0x004F90A0: the physics value `name` scaled for the skater, from DoBalanceTrick's structure if there is one,
    // else the type's structure (0 for other types).
    float GetPhysicsValue(uint32_t name);
    // 0x004F91D0: DoBalanceTrick's structure freed.
    void ClearParams();
    // 0x004F9370: `params` copied into DoBalanceTrick's structure (made when there's none).
    void SetParams(Script::CStruct* params);
    // 0x004F9200 (not a Ghidra function): the wobble from `wobble` (WobbleAmpA / B, WobbleK1 / 2, SpazFactor - each a
    // structure scaled for the skater) given to the anim blender; the skater's script made first if it has none.
    void SetWobble(Script::CStruct* wobble);

    // The balance of the current type (null for none).
    CBalance* Current();

    uint32_t m_trick;                   // +0x18 the balance trick being balanced (0: none)
    bool m_started;                     // +0x1C StartBalanceTrick
    uint8_t pad1D[3];
    Script::CStruct* mp_params;         // +0x20 DoBalanceTrick's BalanceParams (a copy)
    CBalance m_manual;                  // +0x24 manual / nose manual
    CBalance m_grind;                   // +0x7C grind / slide
    CBalance m_lip;                     // +0xD4
    CBalance m_skitch;                  // +0x12C
    CAnimBlenderComponent* mp_animBlender; // +0x184
};
static_assert(offsetof(CSkaterBalanceTrickComponent, m_trick) == 0x18 &&
                  offsetof(CSkaterBalanceTrickComponent, m_started) == 0x1C &&
                  offsetof(CSkaterBalanceTrickComponent, mp_params) == 0x20 &&
                  offsetof(CSkaterBalanceTrickComponent, m_manual) == 0x24 &&
                  offsetof(CSkaterBalanceTrickComponent, m_grind) == 0x7C &&
                  offsetof(CSkaterBalanceTrickComponent, m_lip) == 0xD4 &&
                  offsetof(CSkaterBalanceTrickComponent, m_skitch) == 0x12C &&
                  offsetof(CSkaterBalanceTrickComponent, mp_animBlender) == 0x184 &&
                  sizeof(CSkaterBalanceTrickComponent) == 0x188,
              "layout must match the original (0x004F9170 allocates 0x188 bytes)");

} // namespace Obj
