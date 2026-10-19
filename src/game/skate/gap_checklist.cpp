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
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Game {

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

void** const kGapVtable = reinterpret_cast<void**>(0x0064EC7C);
void** const kGapNodeVtable = reinterpret_cast<void**>(0x0064EC78);  // Lst::Node<CGapCheck>
void** const kGapHeadVtable = reinterpret_cast<void**>(0x0064ED20);  // Lst::Head<CGapCheck>

CGapChecklist*& Building() { return GameVariable<CGapChecklist*>(0x007CE4B0); }

// The list's nodes: the head's data is -1.
CGapCheck* GapOf(Mem::ListNode* node)
{
    return node->data == reinterpret_cast<void*>(-1) ? nullptr : reinterpret_cast<CGapCheck*>(node);
}
CGapCheck* Next(CGapCheck* gap) { return GapOf(gap->m_node.next); }

Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

// The skate module's checklist (0x005656C0) and the career's of a level (0x00584110; the current level 0x00584130).
CGapChecklist* SkateChecklist()
{
    return GameFunction<CGapChecklist*(__thiscall*)(void*)>(0x005656C0)(GameVariable<void*>(0x007CE478));
}
void* Career() { return *reinterpret_cast<void**>(GameVariable<uint8_t*>(0x007CE478) + 0x20); }
CGapChecklist* LevelChecklist(void* career, int level)
{
    return GameFunction<CGapChecklist*(__thiscall*)(void*, int)>(0x00584110)(career, level);
}
int CurrentLevel(void* career) { return GameFunction<int(__thiscall*)(void*)>(0x00584130)(career); }

} // namespace

// 0x00578680
void CGapCheckNode::DestroyBody() { m_node.UnlinkAs(kGapNodeVtable); }

// 0x005786A0
CGapCheckNode* CGapCheckNode::Destroy(unsigned int flags)
{
    m_node.UnlinkAs(kGapNodeVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005794B0
CGapCheckNode* CGapCheckNode::Destroy2(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005786E0
CGapCheck* CGapCheck::Construct()
{
    m_node.data = this;
    m_node.priority = 0;
    m_node.next = &m_node;
    m_node.prev = &m_node;
    m_node.vtable = kGapVtable;
    new (&m_name) Str::String();
    m_count = 0;
    m_pending = 0;
    m_inLevel = true;
    return this;
}

// 0x00579510
void CGapCheck::DestroyBody()
{
    m_name.~String();
    m_node.UnlinkAs(kGapNodeVtable);
}

// 0x005794E0
CGapCheck* CGapCheck::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00579690
CGapChecklist* CGapChecklist::Construct()
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
void CGapChecklist::Clear()
{
    while (CGapCheck* gap = GapOf(m_gaps.next))
        reinterpret_cast<void*(__thiscall*)(CGapCheck*, int)>(gap->m_node.vtable[0])(gap, 1);
    m_built = false;
}

// 0x00578880
void CGapChecklist::AddGap(const char* name, int count, int score)
{
    m_built = true;
    CGapCheck* after = nullptr;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (std::strcmp(gap->m_name.c_str(), name) == 0 && gap->m_score == score) {
            gap->m_inLevel = true;
            return;
        }
        if (gap->m_score < score || (gap->m_score == score && Str::CompareCaseless(gap->m_name.c_str(), name) < 0))
            after = gap;
    }
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    void* memory = Mem::Calloc(sizeof(CGapCheck));
    CGapCheck* gap = memory ? static_cast<CGapCheck*>(memory)->Construct() : nullptr;
    gap->m_count = 0;
    gap->m_name = name;
    gap->m_count = count;
    gap->m_score = score;
    Mem::ListNode* before = after ? &after->m_node : &m_gaps;
    gap->m_node.prev = before;
    gap->m_node.next = before->next;
    before->next->prev = &gap->m_node;
    before->next = &gap->m_node;
    Mem::Manager::Get()->PopContext();
}

// 0x00578A00
void CGapChecklist::AddGapFromNode(Script::CStruct* params, const uint8_t* /*call*/)
{
    int score = 0;
    params->GetInteger(kScore, &score, 0);
    const char* text = "";
    params->GetString(kText, &text, 0);
    if (score && *text) {
        AddGap(text, 0, score);
        return;
    }
    if (Script::GetInteger(kPrintGaps))
        Dbg::Printf("(Name '%s' Score %d - not in checklist)\n", text, score);
}

// 0x00578AA0
void __cdecl AddGapFromNodeCallback(Script::CStruct* params, const uint8_t* call)
{
    Building()->AddGapFromNode(params, call);
}

// 0x00578AC0
void CGapChecklist::Build()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    Building() = this;
    m_built = true;
    if (Script::GetInteger("PrintGaps"))
        Dbg::Printf("\nGAP LIST\n--------------------------------------\n");
    SkateScript::ForEachNodeScriptCall(kTriggerScript, kEndGap, &AddGapFromNodeCallback);
    Mem::Manager::Get()->PopContext();
}

// 0x00578B30
int CGapChecklist::FindGap(const char* name)
{
    int index = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++index) {
        if (std::strcmp(gap->m_name.c_str(), name) == 0)
            return index;
    }
    return -1;
}

// 0x00578BC0
void CGapChecklist::SetGapPositions(const char* name, Mth::Vector pos, Mth::Vector dir)
{
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (std::strcmp(gap->m_name.c_str(), name) == 0) {
            gap->m_skaterStartPos = pos;
            gap->m_skaterStartDir = dir;
            return;
        }
    }
}

// 0x00578C70
void CGapChecklist::GotGap(const char* name)
{
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (std::strcmp(gap->m_name.c_str(), name) == 0) {
            ++gap->m_pending;
            return;
        }
    }
}

// 0x00578CF0
void CGapChecklist::ClearPending()
{
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap))
        gap->m_pending = 0;
}

// 0x00578D20
int CGapChecklist::Count()
{
    int count = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap))
        ++count;
    return count;
}

// 0x00578D50
bool CGapChecklist::AllGot()
{
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        if (!gap->m_count)
            return false;
    }
    return true;
}

// 0x00578D90
void CGapChecklist::Reset()
{
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        gap->m_count = 0;
        gap->m_pending = 0;
        gap->m_inLevel = true;
    }
}

// 0x00578DD0
bool CGapChecklist::GetGapTotals(Script::CStruct* /*params*/, Script::CScript* script)
{
    int total = 0, collected = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap)) {
        ++total;
        if (gap->m_count)
            ++collected;
    }
    script->m_params->AddInteger(kNumGaps, total);
    script->m_params->AddInteger(kNumCollectedGaps, collected);
    return true;
}

// 0x00578E40
bool CGapChecklist::AddGapsToMenu(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t chooseScript = kNullScript;
    params->GetChecksum(kChooseScript, &chooseScript, 0);
    int index = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap;) {
        Script::CStruct* item = NewStructure();
        CGapCheck* next = Next(gap);
        if (index == 0)
            item->AddChecksum(0u, kFirstItem);
        else if (!next)
            item->AddChecksum(0u, kLastItem);
        item->AddInteger(kGapScore, gap->m_score);
        item->AddString(kGapName, gap->m_name.c_str());
        item->AddInteger(kTimes, gap->m_count);
        item->AddInteger(kGapNumber, index);
        item->AddVector(kSkaterStartPos, gap->m_skaterStartPos.x, gap->m_skaterStartPos.y, gap->m_skaterStartPos.z);
        item->AddVector(kSkaterStartDir, gap->m_skaterStartDir.x, gap->m_skaterStartDir.y, gap->m_skaterStartDir.z);
        item->AddChecksum(kChooseScript, chooseScript);
        Script::RunScript(kGapMenuAddItem, item, nullptr, false, nullptr);
        DeleteStructure(item);
        ++index;
        gap = next;
    }
    return index != 0;
}

// 0x00579080
bool CGapChecklist::CreateGapList(Script::CStruct* /*params*/, Script::CScript* script)
{
    int count = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap))
        ++count;
    script->m_params->AddInteger(kArraySize, count);
    if (count == 0) {
        script->m_params->AddInteger(kNumGapsGot, count);
        return false;
    }
    Script::CArray* memory = Script::NewArray();
    Script::CArray* list = memory ? memory->Init() : nullptr;
    list->SetSizeAndType(count, Script::ESYMBOLTYPE_STRUCTURE);
    int got = 0;
    int index = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++index) {
        if (gap->m_count)
            ++got;
        Script::CStruct* item = NewStructure();
        item->AddInteger("gap_score", gap->m_score);
        item->AddString("gap_name", gap->m_name.c_str());
        item->AddInteger("times", gap->m_count);
        item->AddInteger("gap_number", index);
        item->AddVector(kSkaterStartPos, gap->m_skaterStartPos.x, gap->m_skaterStartPos.y, gap->m_skaterStartPos.z);
        item->AddVector(kSkaterStartDir, gap->m_skaterStartDir.x, gap->m_skaterStartDir.y, gap->m_skaterStartDir.z);
        list->SetElement(index, reinterpret_cast<uint32_t>(item));
    }
    script->m_params->AddArrayPointer(kGapList, list);
    script->m_params->AddInteger(kNumGapsGot, got);
    return true;
}

// 0x005793D0
CGapCheck* CGapChecklist::GetGap(int index)
{
    int i = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++i) {
        if (i == index)
            return gap;
    }
    return nullptr;
}

// 0x00579420
const char* CGapChecklist::GetGapName(int index) { return GetGap(index)->m_name.c_str(); }
// 0x00579440
int CGapChecklist::GetGapCount(int index) { return GetGap(index)->m_count; }
// 0x00579450
int CGapChecklist::GetGapScore(int index) { return GetGap(index)->m_score; }

// 0x00579460
void CGapChecklist::GetGapPositions(int index, Mth::Vector* pos, Mth::Vector* dir)
{
    CGapCheck* gap = GetGap(index);
    *pos = gap->m_skaterStartPos;
    *dir = gap->m_skaterStartDir;
}

// 0x005796C0
void CGapChecklist::AwardPendingGaps()
{
    int8_t newGaps = 0;
    int index = 0;
    for (CGapCheck* gap = GapOf(m_gaps.next); gap; gap = Next(gap), ++index) {
        if (gap->m_pending) {
            uint8_t* goals = GameFunction<uint8_t*(__cdecl*)()>(0x005591B0)(); // the goal manager
            if (GameFunction<bool(__thiscall*)(void*)>(0x0055DEA0)(goals))
                GameFunction<void(__thiscall*)(void*, int)>(0x005586E0)(goals, index);
            if (gap->m_count == 0) {
                ++newGaps;
                Script::CStruct* params = NewStructure();
                if (newGaps > 1)
                    params->AddInteger(kMultipleNewGaps, newGaps);
                params->AddString(kGapText, gap->m_name.c_str());
                Script::RunScript(kGotGapForFirstTime, params, nullptr, false, nullptr);
                DeleteStructure(params);
            }
            gap->m_count += gap->m_pending;
            gap->m_pending = 0;
        }
        gap->m_pending = 0;
    }
}

// 0x005792E0
bool __cdecl CFunc_AddGapsToMenu(Script::CStruct* params, Script::CScript* script)
{
    return SkateChecklist()->AddGapsToMenu(params, script);
}

// 0x00579300
bool __cdecl CFunc_CreateGapList(Script::CStruct* params, Script::CScript* script)
{
    return SkateChecklist()->CreateGapList(params, script);
}

// 0x00579320
bool __cdecl CFunc_GetLevelGapTotals(Script::CStruct* params, Script::CScript* script)
{
    int level = 0; // (the original's is uninitialised: level= is required)
    params->GetInteger(kLevel, &level, 1);
    return LevelChecklist(Career(), level)->GetGapTotals(params, script);
}

// 0x00579360
bool __cdecl CFunc_GiveAllGaps(Script::CStruct* params, Script::CScript* /*script*/)
{
    void* career = Career();
    int level;
    if (!params->GetInteger(kLevel, &level, 0))
        level = CurrentLevel(career);
    CGapChecklist* checklist = LevelChecklist(career, level);
    for (CGapCheck* gap = GapOf(checklist->m_gaps.next); gap; gap = Next(gap))
        ++gap->m_count;
    return true;
}

// 0x00585520
void CGapChecklist::DestroyBody() { m_gaps.UnlinkAs(kGapNodeVtable); }

} // namespace Game

REPLACE(0x00578680, &Game::CGapCheckNode::DestroyBody);
REPLACE(0x005786A0, &Game::CGapCheckNode::Destroy);
REPLACE(0x005786E0, &Game::CGapCheck::Construct);
REPLACE(0x00578840, &Game::CGapChecklist::Clear);
REPLACE(0x00578880, &Game::CGapChecklist::AddGap);
REPLACE(0x00578A00, &Game::CGapChecklist::AddGapFromNode);
REPLACE(0x00578AA0, Game::AddGapFromNodeCallback);
REPLACE(0x00578AC0, &Game::CGapChecklist::Build);
REPLACE(0x00578B30, &Game::CGapChecklist::FindGap);
REPLACE(0x00578BC0, &Game::CGapChecklist::SetGapPositions);
REPLACE(0x00578C70, &Game::CGapChecklist::GotGap);
REPLACE(0x00578CF0, &Game::CGapChecklist::ClearPending);
REPLACE(0x00578D20, &Game::CGapChecklist::Count);
REPLACE(0x00578D50, &Game::CGapChecklist::AllGot);
REPLACE(0x00578D90, &Game::CGapChecklist::Reset);
REPLACE(0x00578DD0, &Game::CGapChecklist::GetGapTotals);
REPLACE(0x00578E40, &Game::CGapChecklist::AddGapsToMenu);
REPLACE(0x00579080, &Game::CGapChecklist::CreateGapList);
REPLACE(0x005792E0, Game::CFunc_AddGapsToMenu);
REPLACE(0x00579300, Game::CFunc_CreateGapList);
REPLACE(0x00579320, Game::CFunc_GetLevelGapTotals);
REPLACE(0x00579360, Game::CFunc_GiveAllGaps);
REPLACE(0x005793D0, &Game::CGapChecklist::GetGap);
REPLACE(0x00579420, &Game::CGapChecklist::GetGapName);
REPLACE(0x00579440, &Game::CGapChecklist::GetGapCount);
REPLACE(0x00579450, &Game::CGapChecklist::GetGapScore);
REPLACE(0x00579460, &Game::CGapChecklist::GetGapPositions);
REPLACE(0x005794B0, &Game::CGapCheckNode::Destroy2);
REPLACE(0x005794E0, &Game::CGapCheck::Destroy);
REPLACE(0x00579510, &Game::CGapCheck::DestroyBody);
REPLACE(0x00579690, &Game::CGapChecklist::Construct);
REPLACE(0x005796C0, &Game::CGapChecklist::AwardPendingGaps);
REPLACE(0x00585520, &Game::CGapChecklist::DestroyBody);
