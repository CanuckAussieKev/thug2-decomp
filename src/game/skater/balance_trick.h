#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/entity.h"
#include "game/skater/manual_balance.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

// The skater's balance trick component (SkaterBalanceTrick, type 0x589E5FB3, vtable 0x0064BA30, 0x188 bytes; made by
// 0x004F9170): the balance of the trick being balanced - a manual (or nose manual), a grind (or slide), a lip trick or
// a skitch, one CBalance each (manual_balance.h) - started by DoBalanceTrick, tweaked by AdjustBalance, its meter switched on
// and off; the physics values of its type (GrindParams / ManualParams / SkitchParams / LipParams, or the structure
// DoBalanceTrick was given). From the disassembly of 0x004F8CD0-0x004F9960.
//
// Vtable: 0 Destroy, 2 ConfigureFromData, 3 Finalize, 12 InvokeMethodByName; the rest the base's / shared (1 the empty
// 0x0051B970, 4 / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 0x005BAB90, 11 0x0042BB20).
namespace GameObjects {

class CAnimBlenderComponent;

class CRiderTeeterMoves : public CObjectModule {
public:
    static constexpr uint32_t kType = 0x589E5FB3;
    // The balance trick types (m_trick).
    static constexpr uint32_t kManual = 0xEF24413B, kNoseManual = 0x0AC90769, kGrind = 0x255ED86F, kSlide = 0x8D10119D,
                              kSkitch = 0x3506CE64, kLip = 0xA549B57B;

    // 0x004F9170 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x004F8CD0 (registered with the type): the member function table 0x00683238 (InBalanceMove).
    static void __cdecl PublishMethodTable();
    // 0x004F8F80: the constructor (returns this): the four balances made, no physics structure, no anim component.
    CRiderTeeterMoves* MakeNew();
    // 0x004F8CF0: the destructor body (the balances', last first, then the base's). 0x004F9010 (vtable slot 0).
    void DestroyBody();
    CRiderTeeterMoves* Teardown(unsigned int flags);
    // 0x004F9040 (vtable slot 2): each balance given the skater. `params` isn't read.
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x004F9080 (vtable slot 3): the anim blender component (0x7149B753).
    void Seal();
    // 0x004F9420 (vtable slot 12): SwitchOffBalanceMeter, SwitchOnBalanceMeter, StartBalanceTrick, AdjustBalance,
    // SetWobbleDetails, StopBalanceTrick, DoBalanceTrick; 2 for other names.
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);
    // 0x004F8D70 (the member function InBalanceMove, from the table; not a Ghidra function): whether one was
    // started (StartBalanceTrick). Its parameters aren't read.
    int InBalanceMove(Scripts::CStruct* params, Scripts::CScript* script);

    // 0x004F8D80 / 0x004F8DB0: every balance's best time cleared / kept.
    void ClearBestTimes();
    void KeepBestTimes();
    // 0x004F8DE0: every balance reset (manual, grind, skitch, lip); no trick, not started.
    void Reset();
    // 0x004F8EE0: every balance stopped (manual, grind, lip, skitch); no trick, not started.
    void Halt();
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
    void ChangeArgs(Scripts::CStruct* params);
    // 0x004F9200 (not a Ghidra function): the wobble from `wobble` (WobbleAmpA / B, WobbleK1 / 2, SpazFactor - each a
    // structure scaled for the skater) given to the anim blender; the skater's script made first if it has none.
    void SetWobble(Scripts::CStruct* wobble);

    // The balance of the current type (null for none).
    CBalance* AtHand();

    uint32_t m_trick;                   // +0x18 the balance trick being balanced (0: none)
    bool m_started;                     // +0x1C StartBalanceTrick
    uint8_t pad1D[3];
    Scripts::CStruct* m_callArgs;         // +0x20 DoBalanceTrick's BalanceParams (a copy)
    CBalance m_manual;                  // +0x24 manual / nose manual
    CBalance m_grind;                   // +0x7C grind / slide
    CBalance m_lip;                     // +0xD4
    CBalance m_skitch;                  // +0x12C
    CAnimBlenderComponent* m_animBlenderPtr; // +0x184
};
static_assert(offsetof(CRiderTeeterMoves, m_trick) == 0x18 &&
                  offsetof(CRiderTeeterMoves, m_started) == 0x1C &&
                  offsetof(CRiderTeeterMoves, m_callArgs) == 0x20 &&
                  offsetof(CRiderTeeterMoves, m_manual) == 0x24 &&
                  offsetof(CRiderTeeterMoves, m_grind) == 0x7C &&
                  offsetof(CRiderTeeterMoves, m_lip) == 0xD4 &&
                  offsetof(CRiderTeeterMoves, m_skitch) == 0x12C &&
                  offsetof(CRiderTeeterMoves, m_animBlenderPtr) == 0x184 &&
                  sizeof(CRiderTeeterMoves) == 0x188,
              "layout must match the original (0x004F9170 allocates 0x188 bytes)");

} // namespace Obj
