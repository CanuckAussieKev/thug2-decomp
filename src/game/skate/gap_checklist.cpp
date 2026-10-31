// The gap checklist, 0x00578680-0x00579820. See gap_checklist.h.

#include "game/skate/gap_checklist.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/skate/career.h"
#include "game/goals/goal_manager.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"

namespace GoalSystem {

CGapTally* g_buildingGapChecklist;

namespace {

// Names.
constexpr uint32_t kPrintGaps = 0x2E75A8A7;          // PrintGaps (a script global)
constexpr uint32_t kScore = 0xCD66C8AE;              // score
constexpr uint32_t kText = 0xC4745838;               // text
constexpr uint32_t kTriggerScript = 0x2CA8A299;      // TriggerScript
constexpr uint32_t kEndGap = 0xE5399FB2;             // EndGap
constexpr uint32_t kNumGaps = 0xE6121ED0;            // num_gaps
constexpr uint32_t kNumCollectedGaps = 0x31EE0CFC;   // num_collected_gaps
constexpr uint32_t kChooseScript = 0xA7D7C610;       // choose_script
constexpr uint32_t kNullScript = 0x0F7E902C;         // NullScript
constexpr uint32_t kFirstItem = 0x171665D5;          // first_item
constexpr uint32_t kLastItem = 0x76CF1EBD;           // last_item
constexpr uint32_t kGapScore = 0xD8BE0D5C;           // gap_score
constexpr uint32_t kGapName = 0xA3343AA4;            // gap_name
constexpr uint32_t kTimes = 0xE2281173;              // times
constexpr uint32_t kGapNumber = 0x3AC9AB5E;          // gap_number
constexpr uint32_t kSkaterStartPos = 0x26A4E85A;     // skater_start_pos
constexpr uint32_t kSkaterStartDir = 0x1CD674E6;     // skater_start_dir
constexpr uint32_t kGapMenuAddItem = 0xB74B672C;     // gap_menu_add_item (a script)
constexpr uint32_t kArraySize = 0x7F5B3D4D;          // array_size
constexpr uint32_t kNumGapsGot = 0x6A8A0061;         // num_gaps_got
constexpr uint32_t kGapList = 0x737449DE;            // GapList
constexpr uint32_t kLevel = 0x651533EC;              // level
constexpr uint32_t kMultipleNewGaps = 0xD2E7D353;    // multiple_new_gaps
constexpr uint32_t kGapText = 0xC69CE365;            // gap_text
constexpr uint32_t kGotGapForFirstTime = 0x318EAF64; // got_gap_for_first_time (a script)

static VtableSlot kGapVtable_slots[] = {&GoalSystem::CGapEntry::Teardown};
void** const kGapVtable = reinterpret_cast<void**>(kGapVtable_slots);
static VtableSlot kGapNodeVtable_slots[] = {&GoalSystem::CGapCheckNode::Teardown};
void** const kGapNodeVtable = reinterpret_cast<void**>(kGapNodeVtable_slots);  // ListKit::Elem<CGapEntry>
static VtableSlot kGapHeadVtable_slots[] = {&GoalSystem::CGapCheckNode::Destroy2};
void** const kGapHeadVtable = reinterpret_cast<void**>(kGapHeadVtable_slots);  // ListKit::Foremost<CGapEntry>

CGapTally*& Building() { return g_buildingGapChecklist; }

// The list's nodes: the head's data is -1.
CGapEntry* GapOf(MemSys::ListNode* node)
{
    return node->data == reinterpret_cast<void*>(-1) ? nullptr : reinterpret_cast<CGapEntry*>(node);
}
CGapEntry* Next(CGapEntry* gap) { return GapOf(gap->m_listLink.next); }

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// The skate module's checklist (0x005656C0) and the career's of a level (0x00584110; the current level 0x00584130).
CGapTally* SkateChecklist()
{
    return static_cast<CGapTally*>(GameModules::Skate::Sole()->FetchGapList());
}
void* Career() { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x20); }
CGapTally* LevelChecklist(void* career, int level)
{
    return static_cast<GameObjects::CRiderStoryProgress*>(career)->FetchGapList(level);
}
int CurrentLevel(void* career) { return static_cast<GameObjects::CRiderStoryProgress*>(career)->FetchStage(); }

} // namespace

// 0x00578680
void CGapCheckNode::DestroyBody() { m_listLink.UnlinkAs(kGapNodeVtable); }

// 0x005786A0
CGapCheckNode* CGapCheckNode::Teardown(unsigned int flags)
{
    m_listLink.UnlinkAs(kGapNodeVtable);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005794B0
CGapCheckNode* CGapCheckNode::Destroy2(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005786E0
CGapEntry* CGapEntry::MakeNew()
{
    m_listLink.data = this;
    m_listLink.priority = 0;
    m_listLink.next = &m_listLink;
    m_listLink.prev = &m_listLink;
    m_listLink.vtable = kGapVtable;
    new (&m_label) TextUtil::Chars();
    m_num = 0;
    m_pending = 0;
    m_inLevel = true;
    return this;
}

// 0x00579510
void CGapEntry::DestroyBody()
{
    m_label.~Chars();
    m_listLink.UnlinkAs(kGapNodeVtable);
}

// 0x005794E0
CGapEntry* CGapEntry::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00579690
CGapTally* CGapTally::MakeNew()
{
    m_gaps.data = reinterpret_cast<void*>(-1);
    m_gaps.priority = 0;
    m_gaps.next = &m_gaps;
    m_gaps.prev = &m_gaps;
    m_gaps.vtable = kGapHeadVtable;
    m_built = false;
    return this;
}

// 0x00578840
void CGapTally::Clear()
{
    while (CGapEntry* gap = GapOf(m_gaps.next))
        reinterpret_cast<void*(__thiscall*)(CGapEntry*, int)>(gap->m_listLink.vtable[0])(gap, 1);
    m_built = false;
}

// 0x00578880
void CGapTally::RegisterGapEntry(const char* name, int count, int score)
{
    m_built = true;
    CGapEntry* after = nullptr;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (std::strcmp(gap->m_label.c_str(), name) == 0 && gap->m_pointsHeld == score) {
            gap->m_inLevel = true;
            return;
        }
        if (gap->m_pointsHeld < score || (gap->m_pointsHeld == score && TextUtil::CompareCaseless(gap->m_label.c_str(), name) < 0))
            after = gap;
    }
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_scriptHeap);
    void* memory = MemSys::AllocZeroed(sizeof(CGapEntry));
    CGapEntry* gap = memory ? static_cast<CGapEntry*>(memory)->MakeNew() : nullptr;
    gap->m_num = 0;
    gap->m_label = name;
    gap->m_num = count;
    gap->m_pointsHeld = score;
    MemSys::ListNode* before = after ? &after->m_listLink : &m_gaps;
    gap->m_listLink.prev = before;
    gap->m_listLink.next = before->next;
    before->next->prev = &gap->m_listLink;
    before->next = &gap->m_listLink;
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00578A00
void CGapTally::AddGapFromNode(Scripts::CStruct* params, const uint8_t* /*call*/)
{
    int score = 0;
    params->FetchIntValue(kScore, &score, 0);
    const char* text = "";
    params->FetchText(kText, &text, 0);
    if (score && *text) {
        RegisterGapEntry(text, 0, score);
        return;
    }
    if (Scripts::FetchIntValue(kPrintGaps))
        DebugOut::Printf("(Name '%s' Score %d - not in checklist)\n", text, score);
}

// 0x00578AA0
void __cdecl AddGapFromNodeCallback(Scripts::CStruct* params, const uint8_t* call)
{
    Building()->AddGapFromNode(params, call);
}

// 0x00578AC0
void CGapTally::Build()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_scriptHeap);
    Building() = this;
    m_built = true;
    if (Scripts::FetchIntValue("PrintGaps"))
        DebugOut::Printf("\nGAP LIST\n--------------------------------------\n");
    LevelNodes::ForEachNodeScriptCall(kTriggerScript, kEndGap, &AddGapFromNodeCallback);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00578B30
int CGapTally::FindGap(const char* name)
{
    int index = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++index) {
        if (std::strcmp(gap->m_label.c_str(), name) == 0)
            return index;
    }
    return -1;
}

// 0x00578BC0
void CGapTally::SetGapPositions(const char* name, Maths::Vec4 pos, Maths::Vec4 dir)
{
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (std::strcmp(gap->m_label.c_str(), name) == 0) {
            gap->m_skaterStartPos = pos;
            gap->m_skaterStartDir = dir;
            return;
        }
    }
}

// 0x00578C70
void CGapTally::RegisterGapHit(const char* name)
{
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (std::strcmp(gap->m_label.c_str(), name) == 0) {
            ++gap->m_pending;
            return;
        }
    }
}

// 0x00578CF0
void CGapTally::ClearPending()
{
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap))
        gap->m_pending = 0;
}

// 0x00578D20
int CGapTally::TotalOf()
{
    int count = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap))
        ++count;
    return count;
}

// 0x00578D50
bool CGapTally::AllGot()
{
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (!gap->m_num)
            return false;
    }
    return true;
}

// 0x00578D90
void CGapTally::Reset()
{
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        gap->m_num = 0;
        gap->m_pending = 0;
        gap->m_inLevel = true;
    }
}

// 0x00578DD0
bool CGapTally::GetGapTotals(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    int total = 0, collected = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        ++total;
        if (gap->m_num)
            ++collected;
    }
    script->m_args->PutInt(kNumGaps, total);
    script->m_args->PutInt(kNumCollectedGaps, collected);
    return true;
}

// 0x00578E40
bool CGapTally::AddGapsToMenu(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t chooseScript = kNullScript;
    params->ReadHashValue(kChooseScript, &chooseScript, 0);
    int index = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap;) {
        Scripts::CStruct* item = NewStructure();
        CGapEntry* next = Next(gap);
        if (index == 0)
            item->PutNameKey(0u, kFirstItem);
        else if (!next)
            item->PutNameKey(0u, kLastItem);
        item->PutInt(kGapScore, gap->m_pointsHeld);
        item->AppendWord(kGapName, gap->m_label.c_str());
        item->PutInt(kTimes, gap->m_num);
        item->PutInt(kGapNumber, index);
        item->PutVec3(kSkaterStartPos, gap->m_skaterStartPos.x, gap->m_skaterStartPos.y, gap->m_skaterStartPos.z);
        item->PutVec3(kSkaterStartDir, gap->m_skaterStartDir.x, gap->m_skaterStartDir.y, gap->m_skaterStartDir.z);
        item->PutNameKey(kChooseScript, chooseScript);
        Scripts::LaunchScript(kGapMenuAddItem, item, nullptr, false, nullptr);
        DeleteStructure(item);
        ++index;
        gap = next;
    }
    return index != 0;
}

// 0x00579080
bool CGapTally::CreateGapList(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    int count = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap))
        ++count;
    script->m_args->PutInt(kArraySize, count);
    if (count == 0) {
        script->m_args->PutInt(kNumGapsGot, count);
        return false;
    }
    Scripts::CArray* memory = Scripts::NewArray();
    Scripts::CArray* list = memory ? memory->PrepareAll() : nullptr;
    list->ConfigureSizeKind(count, Scripts::QBVAL_STRUCT);
    int got = 0;
    int index = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++index) {
        if (gap->m_num)
            ++got;
        Scripts::CStruct* item = NewStructure();
        item->PutInt("gap_score", gap->m_pointsHeld);
        item->AppendWord("gap_name", gap->m_label.c_str());
        item->PutInt("times", gap->m_num);
        item->PutInt("gap_number", index);
        item->PutVec3(kSkaterStartPos, gap->m_skaterStartPos.x, gap->m_skaterStartPos.y, gap->m_skaterStartPos.z);
        item->PutVec3(kSkaterStartDir, gap->m_skaterStartDir.x, gap->m_skaterStartDir.y, gap->m_skaterStartDir.z);
        list->SetElement(index, reinterpret_cast<uint32_t>(item));
    }
    script->m_args->AppendArrayRef(kGapList, list);
    script->m_args->PutInt(kNumGapsGot, got);
    return true;
}

// 0x005793D0
CGapEntry* CGapTally::GetGap(int index)
{
    int i = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++i) {
        if (i == index)
            return gap;
    }
    return nullptr;
}

// 0x00579420
const char* CGapTally::GetGapTitle(int index) { return GetGap(index)->m_label.c_str(); }
// 0x00579440
int CGapTally::CountGapsFound(int index) { return GetGap(index)->m_num; }
// 0x00579450
int CGapTally::GetGapPoints(int index) { return GetGap(index)->m_pointsHeld; }

// 0x00579460
void CGapTally::GetGapPositions(int index, Maths::Vec4* pos, Maths::Vec4* dir)
{
    CGapEntry* gap = GetGap(index);
    *pos = gap->m_skaterStartPos;
    *dir = gap->m_skaterStartDir;
}

// 0x005796C0
void CGapTally::PayOutQueuedGaps()
{
    int8_t newGaps = 0;
    int index = 0;
    for (CGapEntry* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++index) {
        if (gap->m_pending) {
            CGoalManager* goals = GetObjectiveBoard(); // the goal manager
            if (goals->HasActiveGapGoal())
                goals->RegisterGapHit(index);
            if (gap->m_num == 0) {
                ++newGaps;
                Scripts::CStruct* params = NewStructure();
                if (newGaps > 1)
                    params->PutInt(kMultipleNewGaps, newGaps);
                params->AppendWord(kGapText, gap->m_label.c_str());
                Scripts::LaunchScript(kGotGapForFirstTime, params, nullptr, false, nullptr);
                DeleteStructure(params);
            }
            gap->m_num += gap->m_pending;
            gap->m_pending = 0;
        }
        gap->m_pending = 0;
    }
}

// 0x005792E0
bool __cdecl CFunc_AddGapsToMenu(Scripts::CStruct* params, Scripts::CScript* script)
{
    return SkateChecklist()->AddGapsToMenu(params, script);
}

// 0x00579300
bool __cdecl CFunc_CreateGapList(Scripts::CStruct* params, Scripts::CScript* script)
{
    return SkateChecklist()->CreateGapList(params, script);
}

// 0x00579320
bool __cdecl CFunc_GetLevelGapTotals(Scripts::CStruct* params, Scripts::CScript* script)
{
    int level = 0; // (the original's is uninitialised: level= is required)
    params->FetchIntValue(kLevel, &level, 1);
    return LevelChecklist(Career(), level)->GetGapTotals(params, script);
}

// 0x00579360
bool __cdecl CFunc_GiveAllGaps(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    void* career = Career();
    int level;
    if (!params->FetchIntValue(kLevel, &level, 0))
        level = CurrentLevel(career);
    CGapTally* checklist = LevelChecklist(career, level);
    for (CGapEntry* gap = GapOf(checklist->m_gaps.next); gap; gap = Next(gap))
        ++gap->m_num;
    return true;
}

// 0x00585520
void CGapTally::DestroyBody() { m_gaps.UnlinkAs(kGapNodeVtable); }

} // namespace Game
