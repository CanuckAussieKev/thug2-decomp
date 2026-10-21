#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's score component (SkaterScore, type 0xD262802F, vtable 0x0064C090, 0x1C bytes): owns the skater's
// score (Mdl::Score, 0x1550 bytes; not rewritten yet), updates it for a local skater (always special with
// CHEAT_ALWAYS_SPECIAL), and the scripts' questions about it. From the disassembly of 0x00518630-0x00518C6E.
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 12 CallMemberFunction.
namespace Obj {

class CSkaterScoreComponent : public CBaseComponent {
public:
    // 0x00518B60 (registered by RegisterComponentTypes): a new one (0 if out of memory), no score yet.
    static CBaseComponent* __cdecl Create();
    // 0x00518BE0: the score destroyed and freed, then the base's.
    void DestroyBody();
    // 0x00518C50 (vtable slot 0)
    CSkaterScoreComponent* Destroy(unsigned int flags);
    // 0x00518660 (vtable +0x08): the score made if there's none (for the skater's object id). `params` isn't read.
    void InitFromStructure(Script::CStruct* params);
    // 0x005186E0 (vtable +0x04): not a local skater: suspended. Otherwise the score's update (0x00563580); with the
    // skater's +0x168 or CHEAT_ALWAYS_SPECIAL on, the career's special value set from the cheat and the score made
    // special (0x00562DF0), and the flag 0x007CE4FD set.
    void Update();
    // 0x00518760 (vtable +0x30): the score's member functions - IsLatestTrick (a key combo or TrickText with spin and
    // NumTaps), LastScoreLandedLessThan / GreaterThan, TotalScoreLessThan / GreaterThan, CurrentScorePotLessThan /
    // GreaterThan (the base score times the multiplier, truncated), GetSpecialValue / SetSpecialValue,
    // GetTotalScore, GetNumberOfNonGapTricks, GotSpecial, ForceSpecial, IncrementScore, TweakTrick - their answer or
    // 1; 2 for any other name (IsLatestTrick without a trick too).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00518630: the score reset (0x00564A10, then 0x00563370 / 0x00563390 with zeros) - called by the skater.
    void ResetScore();

    uint8_t* m_score; // +0x18 Mdl::Score
};
static_assert(offsetof(CSkaterScoreComponent, m_score) == 0x18 && sizeof(CSkaterScoreComponent) == 0x1C,
              "layout must match the original (0x00518B60 allocates 0x1C bytes)");

} // namespace Obj
