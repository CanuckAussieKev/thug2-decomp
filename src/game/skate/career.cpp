// The skater's career, 0x00584110-0x00584CF3. See career.h.

#include "game/skate/career.h"

#include <cstddef>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/level_editor_career.h"
#include "game/maths/vec4.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/net/gamenet_manager.h"

namespace GameObjects {

// Levels left out of the totals (a byte each, up to the cheat flag at 0x007CE4FD).
bool g_careerLeftOut[CRiderStoryProgress::kLevels];

namespace {

bool* LeftOut() { return g_careerLeftOut; }

uint32_t Bit(int index) { return 1u << (index & 0x1F); }
int Word(int index) { return index >> 5; }

int PopCount(uint32_t word)
{
    int count = 0;
    for (int i = 0; i < 32; ++i, word >>= 1)
        count += word & 1;
    return count;
}

} // namespace

// 0x00584110 / 0x00584130
GoalSystem::CGapTally* CRiderStoryProgress::FetchGapList(int level) { return m_gaps[level == -1 ? m_level : level]; }
int CRiderStoryProgress::FetchStage() { return m_level; }

// 0x00584140
void CRiderStoryProgress::SetGoal(int goal, int level)
{
    if (level == -1)
        level = m_level;
    GoalWords()[level * 8 + Word(goal)] |= Bit(goal);
}

// 0x00584180
void CRiderStoryProgress::UnSetGoal(int goal, int level)
{
    if (level == -1)
        level = m_level;
    uint32_t mask = ~Bit(goal);
    GoalWords()[level * 8 + Word(goal)] &= mask;
    m_startGoals[Word(goal)] &= mask;
}

// 0x005841D0
bool CRiderStoryProgress::GetGoal(int goal, int level)
{
    if (level == -1)
        level = m_level;
    return (GoalWords()[level * 8 + Word(goal)] & Bit(goal)) != 0;
}

// 0x00584210
bool CRiderStoryProgress::JustGotGoal(int goal)
{
    return ((m_startGoals[Word(goal)] ^ GoalWords()[m_level * 8 + Word(goal)]) & Bit(goal)) != 0;
}

// 0x00584250
int CRiderStoryProgress::CountGoals(int level)
{
    if (level == -1)
        level = m_level;
    return PopCount(m_goals[level][0]) + PopCount(m_goals[level][1]);
}

// 0x005842A0
int CRiderStoryProgress::CountTotalGoals()
{
    int total = 0;
    for (int level = 0; level < kLevels; ++level) {
        if (!LeftOut()[level])
            total += PopCount(m_goals[level][1]) + PopCount(m_goals[level][0]);
    }
    return total;
}

// 0x00584320
int CRiderStoryProgress::CountLevelsWithGoals()
{
    int count = 0;
    for (int level = 0; level < kLevels; ++level) {
        if (LeftOut()[level])
            count += PopCount(m_goals[level][1]) + PopCount(m_goals[level][0]) > 0 ? 1 : 0;
    }
    return count;
}

// 0x005843A0 / 0x005843D0 / 0x00584400
void CRiderStoryProgress::SetGlobalFlag(int flag) { m_globalFlags[Word(flag)] |= Bit(flag); }
void CRiderStoryProgress::UnSetGlobalFlag(int flag) { m_globalFlags[Word(flag)] &= ~Bit(flag); }
bool CRiderStoryProgress::GetGlobalFlag(int flag) { return (m_globalFlags[Word(flag)] & Bit(flag)) != 0; }

// 0x00584430
void CRiderStoryProgress::SetFlag(int flag, int level)
{
    if (level == -1)
        level = m_level;
    FlagWords()[level * 8 + Word(flag)] |= Bit(flag);
}

// 0x00584470
void CRiderStoryProgress::UnSetFlag(int flag, int level)
{
    if (level == -1)
        level = m_level;
    uint32_t mask = ~Bit(flag);
    FlagWords()[level * 8 + Word(flag)] &= mask;
    m_startFlags[Word(flag)] &= mask;
}

// 0x005844C0
bool CRiderStoryProgress::FetchMarker(int flag, int level)
{
    if (level == -1)
        level = m_level;
    return (FlagWords()[level * 8 + Word(flag)] & Bit(flag)) != 0;
}

// 0x00584500
bool CRiderStoryProgress::JustGotFlag(int flag)
{
    return ((m_startFlags[Word(flag)] ^ FlagWords()[m_level * 8 + Word(flag)]) & Bit(flag)) != 0;
}

// 0x00584540 / 0x00584560
int CRiderStoryProgress::GetInfoInteger(uint32_t name)
{
    int value;
    m_details->FetchIntValue(name, &value, 1);
    return value;
}
bool CRiderStoryProgress::SetInfoInteger(uint32_t name, int value)
{
    m_details->PutInt(name, value);
    return true;
}

// 0x00584580
void CRiderStoryProgress::AddToInfo471FC4DD(int amount)
{
    int value;
    m_details->FetchIntValue(0x471FC4DD, &value, 1);
    m_details->PutInt(0x471FC4DD, value + amount);
}

// 0x005845C0
uint16_t* CRiderStoryProgress::DepositMoney(int amount)
{
    m_lifetimeFunds = static_cast<uint16_t>(m_lifetimeFunds + amount);
    m_funds[m_level] = static_cast<uint16_t>(m_funds[m_level] + amount);
    return &m_funds[m_level];
}

// 0x005845F0
void CRiderStoryProgress::SetLevelAmount(int amount)
{
    uint16_t& level = m_amount[m_level];
    m_totalAmount = static_cast<uint16_t>(m_totalAmount + static_cast<uint16_t>(amount - level));
    level = static_cast<uint16_t>(amount);
}

// 0x00584620 / 0x00584640 / 0x00584650
uint8_t* CRiderStoryProgress::IncrementCounter()
{
    uint8_t* counter = &m_counter[m_level];
    ++*counter;
    ++m_totalCounter;
    return counter;
}
uint8_t CRiderStoryProgress::GetCounter(int level) { return m_counter[level]; }
void CRiderStoryProgress::SetFlag5EF(uint8_t value) { m_flag5EF = value; }

// 0x00584660
void CRiderStoryProgress::SetValue634(int value)
{
    m_value634 = value;
    if (value > m_maxValue634)
        m_maxValue634 = value;
}

// 0x00584680
bool CRiderStoryProgress::Reset(bool resetGaps, uint32_t part)
{
    if (resetGaps) {
        for (int level = 0; level < kLevels; ++level)
            m_gaps[level]->Reset();
        MenuSystem::LevelEditor::Career::CareerReplaced(); // (NOT ORIGINAL: an edited copy's baseline taken again)
    }
    m_level = 0;
    // A flag and the goal of the same number cleared (and their level-start copies).
    auto clear = [this](int bit, int level) {
        UnSetFlag(bit, level);
        uint32_t mask = ~Bit(bit);
        GoalWords()[level * 8 + Word(bit)] &= mask;
        m_startGoals[Word(bit)] &= mask;
    };
    if (part == 0 || part == 0x4DA4937B) {
        for (int level = 0; level < kLevels; ++level) {
            for (int bit = 0; bit < 200; ++bit)
                clear(bit, level);
            LeftOut()[level] = false;
            m_funds[level] = 0;
            m_amount[level] = 1000;
        }
        m_startGoals[0] = 0;
        m_startFlags[0] = 0;
        m_startGoals[1] = 0;
        m_startFlags[1] = 0;
        m_lifetimeFunds = 0;
        m_value634 = 0;
        m_maxValue634 = 0;
        m_totalAmount = 6000;
    }
    if (part == 0 || part == 0x4C10DE52) {
        for (int level = 0; level < 18; ++level) {
            m_counter[level] = 0;
            for (int bit = 200; bit < 256; ++bit)
                clear(bit, level);
        }
        m_totalCounter = 0;
        m_flag5EF = 0x82;
        m_details->AppendStructureIfAny(Scripts::ReadSubStruct(0x0486FD79));
    }
    return true;
}

// 0x00584BA0
bool CRiderStoryProgress::ResetPart(uint32_t part)
{
    Reset(false, part);
    return true;
}

// 0x00584BC0
bool CRiderStoryProgress::AllGapsGot()
{
    Scripts::CArray* levels = Scripts::FetchList(0xEB1A4BC9);
    uint32_t count = levels->m_extent;
    for (int level = 0; level < kLevels; ++level) {
        uint32_t i = 0;
        for (; i < count; ++i) {
            if (level == Scripts::FetchIntValue(levels->FetchPart(i)))
                break;
        }
        if (i == count)
            continue; // (not listed)
        if (!m_visited[level] || !m_gaps[level]->AllGot())
            return false;
    }
    return true;
}

// 0x00584C60
void CRiderStoryProgress::BeginLevel(int level)
{
    if (level == -1)
        level = m_level;
    else
        m_level = level;
    m_visited[m_level] = true;
    m_startGoals[0] = m_goals[level][0];
    m_startGoals[1] = m_goals[level][1];
    m_startFlags[0] = m_flagBits[level][0];
    m_startFlags[1] = m_flagBits[level][1];
}

// 0x00584CC0
bool CRiderStoryProgress::IsCodeOn(uint32_t name)
{
    int flag = Scripts::FetchIntValue(name);
    bool on = (m_globalFlags[Word(flag)] & Bit(flag)) != 0;
    OnlinePlay::g_cheatFlag = on;
    return on;
}

namespace {

// The save's names.
constexpr uint32_t kCareerStructure = 0x4DA4937B;
constexpr uint32_t kGoalsArray = 0xB7EF8BFC;
constexpr uint32_t kFlagsArray = 0xC0FA354F;
constexpr uint32_t kGlobalFlagsArray = 0xA888FE32;
constexpr uint32_t kGapsArray = 0x9DD7E4A6;
constexpr uint32_t kGapLevel = 0x651533EC;
constexpr uint32_t kGapsBuilt = 0x73F8CA00;
constexpr uint32_t kGapList = 0xD76C173E;
constexpr uint32_t kGapName = 0x699FCFC0;
constexpr uint32_t kGapCount = 0x25EB70DB;
constexpr uint32_t kGapScore = 0x92AB03E8;
constexpr uint32_t kGapPos = 0xA4605544;
constexpr uint32_t kGapDir = 0x9E12C9F8;
constexpr uint32_t kVisitedArray = 0x7106C6BF;
constexpr uint32_t kCashArray = 0x414FD20F;
constexpr uint32_t kAmountArray = 0x9CC5BBF6;
constexpr uint32_t kTotalCash = 0x0444419A;
constexpr uint32_t kTotalAmount = 0x95947B61;
constexpr uint32_t kCounterArray = 0xE888DD02;
constexpr uint32_t kTotalCounter = 0x6C1A5E39;
constexpr uint32_t kFlag5EF = 0x7A6BF912;
constexpr uint32_t kValue634 = 0xF884773C;
constexpr uint32_t kMaxValue634 = 0x1C6CF075;
constexpr uint32_t kInfoStructure = 0x55D761CF;

static VtableSlot kCareerVtable_slots[] = {&GameObjects::CRiderStoryProgress::Teardown};
void** const kCareerVtable = reinterpret_cast<void**>(kCareerVtable_slots);

Scripts::CArray* NewArray(uint32_t size, Scripts::EQbValueKind type)
{
    Scripts::CArray* memory = Scripts::NewArray();
    Scripts::CArray* array = memory ? memory->PrepareAll() : nullptr;
    array->ConfigureSizeKind(size, type);
    return array;
}
Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}

} // namespace

// 0x00585270
CRiderStoryProgress* CRiderStoryProgress::MakeNew()
{
    m_vtable = kCareerVtable;
    for (int level = 0; level < kLevels; ++level) {
        void* memory = MemSys::Alloc(sizeof(GoalSystem::CGapTally));
        m_gaps[level] = memory ? static_cast<GoalSystem::CGapTally*>(memory)->MakeNew() : nullptr;
        m_visited[level] = false;
    }
    m_details = NewStructure();
    Reset(true, 0);
    return this;
}

// 0x00585E40
void CRiderStoryProgress::DestroyBody()
{
    m_vtable = kCareerVtable;
    for (GoalSystem::CGapTally* gaps : m_gaps) {
        if (gaps) {
            gaps->DestroyBody();
            MemSys::Hub::Pull()->Dealloc(gaps);
        }
    }
    if (Scripts::CStruct* info = m_details) {
        info->Clear();
        Scripts::DeleteStruct(info);
    }
}

// 0x00585EA0
CRiderStoryProgress* CRiderStoryProgress::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00584D00
void CRiderStoryProgress::DecodeFromStruct(Scripts::CStruct* save)
{
    Scripts::CStruct* career = nullptr;
    save->ReadSubStruct(kCareerStructure, &career, 0);
    Scripts::CArray* goals = nullptr;
    career->FetchList(kGoalsArray, &goals, 0);
    Scripts::CArray* flags = nullptr;
    career->FetchList(kFlagsArray, &flags, 0);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            GoalWords()[first + i] = goals->FetchPart(first + i);
        for (uint32_t i = 0; i < 8; ++i)
            FlagWords()[first + i] = flags->FetchPart(first + i);
    }
    Scripts::CArray* globals = nullptr;
    career->FetchList(kGlobalFlagsArray, &globals, 0);
    for (uint32_t i = 0; i < 16; ++i)
        m_globalFlags[i] = globals->FetchPart(i);
    // The gap checklists.
    Scripts::CArray* gapLevels = nullptr;
    career->FetchList(kGapsArray, &gapLevels, 0);
    if (gapLevels) {
        for (int level = 0; level < kLevels; ++level) {
            GoalSystem::CGapTally* checklist = m_gaps[level];
            checklist->Clear();
            auto* entry = reinterpret_cast<Scripts::CStruct*>(gapLevels->FetchPart(level));
            int savedLevel = 0, built = 0;
            entry->FetchIntValue(kGapLevel, &savedLevel, 1);
            entry->FetchIntValue(kGapsBuilt, &built, 1);
            if (!built)
                continue;
            Scripts::CArray* list = nullptr;
            entry->FetchList(kGapList, &list, 0);
            int count = static_cast<int>(list->m_extent);
            for (int i = 0; i < count; ++i) {
                auto* gap = reinterpret_cast<Scripts::CStruct*>(list->FetchPart(i));
                const char* name = nullptr;
                int got = 0, score = 0;
                gap->FetchText(kGapName, &name, 0);
                gap->FetchIntValue(kGapCount, &got, 0);
                gap->FetchIntValue(kGapScore, &score, 0);
                checklist->RegisterGapEntry(name, got, score);
                if (got) {
                    Scripts::VectorValue pos, dir;
                    gap->FetchVec(kGapPos, &pos, 0);
                    gap->FetchVec(kGapDir, &dir, 0);
                    checklist->SetGapPositions(name, {pos.x, pos.y, pos.z, pos.w}, {dir.x, dir.y, dir.z, dir.w});
                }
            }
        }
    }
    Scripts::CArray* visited = nullptr;
    career->FetchList(kVisitedArray, &visited, 0);
    if (visited) {
        for (int level = 0; level < kLevels; ++level)
            m_visited[level] = visited->FetchPart(level) != 0;
    }
    Scripts::CArray* cash = nullptr;
    Scripts::CArray* amounts = nullptr;
    career->FetchList(kCashArray, &cash, 0);
    career->FetchList(kAmountArray, &amounts, 0);
    for (int level = 0; level < kLevels; ++level) {
        m_funds[level] = static_cast<uint16_t>(cash->FetchPart(level));
        m_amount[level] = static_cast<uint16_t>(amounts->FetchPart(level));
    }
    int value = 0; // (the original's is left as it was when a value is missing)
    career->FetchIntValue(kTotalCash, &value, 0);
    m_lifetimeFunds = static_cast<uint16_t>(value);
    career->FetchIntValue(kTotalAmount, &value, 0);
    m_totalAmount = static_cast<uint16_t>(value);
    Scripts::CArray* counters = nullptr;
    career->FetchList(kCounterArray, &counters, 0);
    for (int level = 0; level < 18; ++level)
        m_counter[level] = static_cast<uint8_t>(counters->FetchPart(level));
    if (career->FetchIntValue(kTotalCounter, &value, 0)) {
        m_totalCounter = static_cast<uint8_t>(value);
        career->FetchIntValue(kFlag5EF, &value, 0);
        m_flag5EF = static_cast<uint8_t>(value);
    } else {
        // An older save: the total from the counters (+0x5EF 0x8C once it passes it).
        m_totalCounter = 0;
        m_flag5EF = 0x82;
        for (int level = 0; level < 18; ++level) {
            m_totalCounter = static_cast<uint8_t>(m_totalCounter + m_counter[level]);
            if (m_totalCounter > m_flag5EF)
                m_flag5EF = 0x8C;
        }
    }
    career->FetchIntValue(kValue634, &m_value634, 0);
    career->FetchIntValue(kMaxValue634, &m_maxValue634, 0);
    Scripts::CStruct* info = nullptr; // (the original's is left as it was)
    career->ReadSubStruct(kInfoStructure, &info, 0);
    m_details->AppendStructureIfAny(info);
    MenuSystem::LevelEditor::Career::CareerReplaced(); // (NOT ORIGINAL: an edited copy's baseline taken again)
}

// 0x00585550
void CRiderStoryProgress::WriteToStructure(Scripts::CStruct* save)
{
    Scripts::CArray* goals = NewArray(kLevels * 8, Scripts::QBVAL_INTEGER);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            goals->SetElement(first + i, GoalWords()[first + i]);
    }
    Scripts::CArray* flags = NewArray(kLevels * 8, Scripts::QBVAL_INTEGER);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            flags->SetElement(first + i, FlagWords()[first + i]);
    }
    Scripts::CArray* globals = NewArray(16, Scripts::QBVAL_INTEGER);
    for (uint32_t i = 0; i < 16; ++i)
        globals->SetElement(i, m_globalFlags[i]);
    Scripts::CArray* gapLevels = NewArray(kLevels, Scripts::QBVAL_STRUCT);
    for (int level = 0; level < kLevels; ++level) {
        GoalSystem::CGapTally* checklist = m_gaps[level];
        Scripts::CStruct* entry = NewStructure();
        entry->PutInt(kGapLevel, level);
        // NOT ORIGINAL: an edited copy's checklist (level_editor_career.h) is written as before the edited copy - never
        // the editor's gaps, nor gaps landed in it.
        MenuSystem::LevelEditor::Career::Checklist view;
        if (MenuSystem::LevelEditor::Career::SaveView(checklist, &view)) {
            entry->PutInt(kGapsBuilt, view.built ? 1 : 0);
            if (view.built) {
                Scripts::CArray* list = NewArray(static_cast<uint32_t>(view.gaps.size()), Scripts::QBVAL_STRUCT);
                for (size_t i = 0; i < view.gaps.size(); ++i) {
                    const MenuSystem::LevelEditor::Career::Gap& g = view.gaps[i];
                    Scripts::CStruct* gap = NewStructure();
                    gap->AppendWord(kGapName, g.name.c_str());
                    gap->PutInt(kGapCount, g.count);
                    gap->PutInt(kGapScore, g.score);
                    gap->PutVec3(kGapPos, g.pos[0], g.pos[1], g.pos[2]);
                    gap->PutVec3(kGapDir, g.dir[0], g.dir[1], g.dir[2]);
                    list->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(gap));
                }
                entry->AppendArrayRef(kGapList, list);
            }
        } else if (!checklist->m_built) {
            entry->PutInt(kGapsBuilt, 0);
        } else {
            entry->PutInt(kGapsBuilt, 1);
            int count = checklist->TotalOf();
            Scripts::CArray* list = NewArray(static_cast<uint32_t>(count), Scripts::QBVAL_STRUCT);
            for (int i = 0; i < count; ++i) {
                Scripts::CStruct* gap = NewStructure();
                gap->AppendWord(kGapName, checklist->GetGapTitle(i));
                gap->PutInt(kGapCount, checklist->CountGapsFound(i));
                gap->PutInt(kGapScore, checklist->GetGapPoints(i));
                Maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f}, dir = {0.0f, 0.0f, 0.0f, 1.0f};
                if (checklist->CountGapsFound(i))
                    checklist->GetGapPositions(i, &pos, &dir);
                gap->PutVec3(kGapPos, pos.x, pos.y, pos.z);
                gap->PutVec3(kGapDir, dir.x, dir.y, dir.z);
                list->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(gap));
            }
            entry->AppendArrayRef(kGapList, list);
        }
        gapLevels->SetElement(static_cast<uint32_t>(level), reinterpret_cast<uint32_t>(entry));
    }
    Scripts::CArray* visited = NewArray(kLevels, Scripts::QBVAL_INTEGER);
    for (int level = 0; level < kLevels; ++level)
        visited->SetElement(static_cast<uint32_t>(level), m_visited[level]);
    Scripts::CStruct* career = NewStructure();
    career->AddComponentOfType(kGoalsArray, Scripts::QBVAL_ARRAY, reinterpret_cast<uint32_t>(goals));
    career->AddComponentOfType(kFlagsArray, Scripts::QBVAL_ARRAY, reinterpret_cast<uint32_t>(flags));
    career->AddComponentOfType(kGlobalFlagsArray, Scripts::QBVAL_ARRAY, reinterpret_cast<uint32_t>(globals));
    career->AddComponentOfType(kGapsArray, Scripts::QBVAL_ARRAY, reinterpret_cast<uint32_t>(gapLevels));
    career->AppendArrayRef(kVisitedArray, visited);
    career->PutNameKey(0xE3335D2F, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x378)); // (the skate module's)
    career->PutNameKey(0x2CC06F5E, static_cast<GoalSystem::CMatchRules*>(static_cast<void*>(GoalSystem::g_gameMode))->GetNameKey());
    // (both made first, then sized, as the original)
    Scripts::CArray* cashMemory = Scripts::NewArray();
    Scripts::CArray* cash = cashMemory ? cashMemory->PrepareAll() : nullptr;
    Scripts::CArray* amountMemory = Scripts::NewArray();
    Scripts::CArray* amounts = amountMemory ? amountMemory->PrepareAll() : nullptr;
    cash->ConfigureSizeKind(kLevels, Scripts::QBVAL_INTEGER);
    amounts->ConfigureSizeKind(kLevels, Scripts::QBVAL_INTEGER);
    for (int level = 0; level < kLevels; ++level) {
        cash->SetElement(static_cast<uint32_t>(level), m_funds[level]);
        amounts->SetElement(static_cast<uint32_t>(level), m_amount[level]);
    }
    career->AppendArrayRef(kCashArray, cash);
    career->AppendArrayRef(kAmountArray, amounts);
    career->PutInt(kTotalCash, m_lifetimeFunds);
    career->PutInt(kTotalAmount, m_totalAmount);
    Scripts::CArray* counters = NewArray(18, Scripts::QBVAL_INTEGER);
    for (int level = 0; level < 18; ++level)
        counters->SetElement(static_cast<uint32_t>(level), m_counter[level]);
    career->AppendArrayRef(kCounterArray, counters);
    career->PutInt(kTotalCounter, m_totalCounter);
    career->PutInt(kFlag5EF, m_flag5EF);
    career->PutInt(kValue634, m_value634);
    career->PutInt(kMaxValue634, m_maxValue634);
    career->PutSubStruct(kInfoStructure, m_details);
    save->AddComponentOfType(kCareerStructure, 11, reinterpret_cast<uint32_t>(career));
}

// 0x00585E20 (GetCareerSaveInfo): the skate module's career written to the script's results.
bool __cdecl CFunc_GetCareerSaveInfo(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    auto* career = *reinterpret_cast<CRiderStoryProgress**>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x20);
    career->WriteToStructure(*reinterpret_cast<Scripts::CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14));
    return true;
}

} // namespace Obj
