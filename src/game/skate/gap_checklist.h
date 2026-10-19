#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/core/string.h"
#include "game/mth/vector.h"

// The gap checklist (0x00578680-0x00579820): each level's list of the gaps it has (every EndGap the level's node
// scripts can reach), sorted by score then name, with how often each was landed. The career keeps one per level
// (0x00584110); the skate module's (0x005656C0) is the current level's. The script commands AddGapsToMenu,
// CreateGapList, GetLevelGapTotals and GiveAllGaps read it.
namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Game {

// One gap (0x50 bytes, vtable 0x0064EC7C; its node type Lst::Node<CGapCheck>'s 0x0064EC78).
struct CGapCheck {
    Mem::ListNode m_node;          // data = the gap itself
    Mth::Vector m_skaterStartPos;  // +0x14 where the gap menu puts the skater (SetGapPositions)
    Mth::Vector m_skaterStartDir;  // +0x24
    int m_pending;                 // +0x34 landed since the last award (GotGap)
    int m_count;                   // +0x38 times landed
    int m_score;                   // +0x3C
    bool m_inLevel;                // +0x40 found by the last scan of the level's scripts
    uint8_t pad41[3];
    Str::String m_name;            // +0x44

    // 0x005786E0: an unlinked node, an empty name, never landed, in the level. Returns this.
    CGapCheck* Construct();
    // 0x00579510: the name freed, the node unlinked (its node type's vtable). 0x005794E0: the scalar deleting
    // destructor.
    void DestroyBody();
    CGapCheck* Destroy(unsigned int flags);
};
static_assert(offsetof(CGapCheck, m_skaterStartPos) == 0x14 && offsetof(CGapCheck, m_pending) == 0x34 &&
                  offsetof(CGapCheck, m_score) == 0x3C && offsetof(CGapCheck, m_name) == 0x44 &&
                  sizeof(CGapCheck) == 0x50,
              "layout must match the original");

// Lst::Node<CGapCheck>'s destructors: 0x00578680 (unlinked, its vtable 0x0064EC78), 0x005786A0 and 0x005794B0 (two
// scalar deleting destructors of it).
struct CGapCheckNode {
    Mem::ListNode m_node;

    void DestroyBody();
    CGapCheckNode* Destroy(unsigned int flags);
    CGapCheckNode* Destroy2(unsigned int flags);
};

class CGapChecklist {
public:
    // 0x00579690: no gaps, not built. Returns this.
    CGapChecklist* Construct();

    // 0x00578840: every gap deleted, not built.
    void Clear();
    // 0x00578AC0: every EndGap the nodes' TriggerScripts reach added (AddGapFromNode, through the instance
    // 0x007CE4B0), in the script heap; PrintGaps prints a header.
    void Build();
    // 0x00578A00: an EndGap call's parameters: with a score= and a non-empty text=, the gap is added (or found
    // again); otherwise, with PrintGaps, a note that it isn't in the checklist. (`call` isn't used.)
    void AddGapFromNode(Script::CStruct* params, const uint8_t* call);
    // 0x00578880: the gap `name` of `score`: one already there (same name - case counts - and score) is marked
    // in the level; else a new one (`count` times landed, in the script heap) goes after the last gap of a lower
    // score, or of the same score and a name sorting first (ignoring case) - or first.
    void AddGap(const char* name, int count, int score);

    // 0x00578B30: the index of the gap `name` (-1: none). 0x005793D0: the gap at `index` (null: none).
    int FindGap(const char* name);
    CGapCheck* GetGap(int index);
    // 0x00579420 / 0x00579440 / 0x00579450: the gap at `index`'s name, times landed, score (no check: the index
    // must be valid). 0x00579460: its skater start position and direction.
    const char* GetGapName(int index);
    int GetGapCount(int index);
    int GetGapScore(int index);
    void GetGapPositions(int index, Mth::Vector* pos, Mth::Vector* dir);
    // 0x00578BC0: the gap `name`'s skater start position and direction (no such gap: nothing).
    void SetGapPositions(const char* name, Mth::Vector pos, Mth::Vector dir);
    // 0x00578C70: the gap `name` landed (pending until AwardPendingGaps).
    void GotGap(const char* name);
    // 0x00578CF0: the pending landings dropped.
    void ClearPending();
    // 0x005796C0: the pending landings counted: the goal manager told of each gap landed (0x005586E0, while
    // 0x0055DEA0 says so), and for gaps landed for the first time got_gap_for_first_time runs (gap_text, and from
    // the second such gap on multiple_new_gaps = how many so far).
    void AwardPendingGaps();
    // 0x00578D90: every gap never landed and in the level.
    void Reset();
    // 0x00578D20: how many gaps. 0x00578D50: whether every gap was landed (none: true).
    int Count();
    bool AllGot();

    // The script commands' work (`script`'s parameters get the results; true unless there's nothing):
    // 0x00578DD0 (GetLevelGapTotals): num_gaps, num_collected_gaps. True.
    bool GetGapTotals(Script::CStruct* params, Script::CScript* script);
    // 0x00578E40 (AddGapsToMenu): gap_menu_add_item for each gap (first_item / last_item flags, gap_score,
    // gap_name, times, gap_number, skater_start_pos, skater_start_dir, choose_script= - NullScript by default).
    // False with no gaps.
    bool AddGapsToMenu(Script::CStruct* params, Script::CScript* script);
    // 0x00579080 (CreateGapList): array_size; GapList (gap_score, gap_name, times, gap_number, skater_start_pos,
    // skater_start_dir for each) and num_gaps_got. False with no gaps (num_gaps_got 0).
    bool CreateGapList(Script::CStruct* params, Script::CScript* script);

    bool m_built;           // +0x00 (also set by AddGap)
    uint8_t pad01[3];
    Mem::ListNode m_gaps;   // +0x04 the list's head (Lst::Head<CGapCheck>, vtable 0x0064ED20)
};
static_assert(offsetof(CGapChecklist, m_gaps) == 4 && sizeof(CGapChecklist) == 0x18, "layout must match the original");

// 0x00578AA0 (Build's callback): AddGapFromNode on the checklist being built.
void __cdecl AddGapFromNodeCallback(Script::CStruct* params, const uint8_t* call);

// The script commands (cdecl, the CFunction table's):
// 0x005792E0 AddGapsToMenu / 0x00579300 CreateGapList: the skate module's checklist's.
bool __cdecl CFunc_AddGapsToMenu(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_CreateGapList(Script::CStruct* params, Script::CScript* script);
// 0x00579320 GetLevelGapTotals: level= (required) - the career's checklist of that level's.
bool __cdecl CFunc_GetLevelGapTotals(Script::CStruct* params, Script::CScript* script);
// 0x00579360 GiveAllGaps: every gap of level= (default: the current level - 0x00584130) landed once more.
bool __cdecl CFunc_GiveAllGaps(Script::CStruct* params, Script::CScript* script);

} // namespace Game
