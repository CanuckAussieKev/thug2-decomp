// The skater's career, 0x00584110-0x00584CF3. See career.h.

#include "game/skate/career.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/level_editor_career.h"
#include "game/mth/vector.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

GAME_GLOBAL(bool, g_careerLeftOut, 0x007CE4E8);

namespace {

// Levels left out of the totals (a byte each).
bool* LeftOut() { return &g_careerLeftOut; }

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
Game::CGapChecklist* CSkaterCareer::GetGapChecklist(int level) { return m_gaps[level == -1 ? m_level : level]; }
int CSkaterCareer::GetLevel() { return m_level; }

// 0x00584140
void CSkaterCareer::SetGoal(int goal, int level)
{
    if (level == -1)
        level = m_level;
    GoalWords()[level * 8 + Word(goal)] |= Bit(goal);
}

// 0x00584180
void CSkaterCareer::UnSetGoal(int goal, int level)
{
    if (level == -1)
        level = m_level;
    uint32_t mask = ~Bit(goal);
    GoalWords()[level * 8 + Word(goal)] &= mask;
    m_startGoals[Word(goal)] &= mask;
}

// 0x005841D0
bool CSkaterCareer::GetGoal(int goal, int level)
{
    if (level == -1)
        level = m_level;
    return (GoalWords()[level * 8 + Word(goal)] & Bit(goal)) != 0;
}

// 0x00584210
bool CSkaterCareer::JustGotGoal(int goal)
{
    return ((m_startGoals[Word(goal)] ^ GoalWords()[m_level * 8 + Word(goal)]) & Bit(goal)) != 0;
}

// 0x00584250
int CSkaterCareer::CountGoals(int level)
{
    if (level == -1)
        level = m_level;
    return PopCount(m_goals[level][0]) + PopCount(m_goals[level][1]);
}

// 0x005842A0
int CSkaterCareer::CountTotalGoals()
{
    int total = 0;
    for (int level = 0; level < kLevels; ++level) {
        if (!LeftOut()[level])
            total += PopCount(m_goals[level][1]) + PopCount(m_goals[level][0]);
    }
    return total;
}

// 0x00584320
int CSkaterCareer::CountLevelsWithGoals()
{
    int count = 0;
    for (int level = 0; level < kLevels; ++level) {
        if (LeftOut()[level])
            count += PopCount(m_goals[level][1]) + PopCount(m_goals[level][0]) > 0 ? 1 : 0;
    }
    return count;
}

// 0x005843A0 / 0x005843D0 / 0x00584400
void CSkaterCareer::SetGlobalFlag(int flag) { m_globalFlags[Word(flag)] |= Bit(flag); }
void CSkaterCareer::UnSetGlobalFlag(int flag) { m_globalFlags[Word(flag)] &= ~Bit(flag); }
bool CSkaterCareer::GetGlobalFlag(int flag) { return (m_globalFlags[Word(flag)] & Bit(flag)) != 0; }

// 0x00584430
void CSkaterCareer::SetFlag(int flag, int level)
{
    if (level == -1)
        level = m_level;
    FlagWords()[level * 8 + Word(flag)] |= Bit(flag);
}

// 0x00584470
void CSkaterCareer::UnSetFlag(int flag, int level)
{
    if (level == -1)
        level = m_level;
    uint32_t mask = ~Bit(flag);
    FlagWords()[level * 8 + Word(flag)] &= mask;
    m_startFlags[Word(flag)] &= mask;
}

// 0x005844C0
bool CSkaterCareer::GetFlag(int flag, int level)
{
    if (level == -1)
        level = m_level;
    return (FlagWords()[level * 8 + Word(flag)] & Bit(flag)) != 0;
}

// 0x00584500
bool CSkaterCareer::JustGotFlag(int flag)
{
    return ((m_startFlags[Word(flag)] ^ FlagWords()[m_level * 8 + Word(flag)]) & Bit(flag)) != 0;
}

// 0x00584540 / 0x00584560
int CSkaterCareer::GetInfoInteger(uint32_t name)
{
    int value;
    m_info->GetInteger(name, &value, 1);
    return value;
}
bool CSkaterCareer::SetInfoInteger(uint32_t name, int value)
{
    m_info->AddInteger(name, value);
    return true;
}

// 0x00584580
void CSkaterCareer::AddToInfo471FC4DD(int amount)
{
    int value;
    m_info->GetInteger(0x471FC4DD, &value, 1);
    m_info->AddInteger(0x471FC4DD, value + amount);
}

// 0x005845C0
uint16_t* CSkaterCareer::AddCash(int amount)
{
    m_totalCash = static_cast<uint16_t>(m_totalCash + amount);
    m_cash[m_level] = static_cast<uint16_t>(m_cash[m_level] + amount);
    return &m_cash[m_level];
}

// 0x005845F0
void CSkaterCareer::SetLevelAmount(int amount)
{
    uint16_t& level = m_amount[m_level];
    m_totalAmount = static_cast<uint16_t>(m_totalAmount + static_cast<uint16_t>(amount - level));
    level = static_cast<uint16_t>(amount);
}

// 0x00584620 / 0x00584640 / 0x00584650
uint8_t* CSkaterCareer::IncrementCounter()
{
    uint8_t* counter = &m_counter[m_level];
    ++*counter;
    ++m_totalCounter;
    return counter;
}
uint8_t CSkaterCareer::GetCounter(int level) { return m_counter[level]; }
void CSkaterCareer::SetFlag5EF(uint8_t value) { m_flag5EF = value; }

// 0x00584660
void CSkaterCareer::SetValue634(int value)
{
    m_value634 = value;
    if (value > m_maxValue634)
        m_maxValue634 = value;
}

// 0x00584680
bool CSkaterCareer::Reset(bool resetGaps, uint32_t part)
{
    if (resetGaps) {
        for (int level = 0; level < kLevels; ++level)
            m_gaps[level]->Reset();
        Front::LevelEditor::Career::CareerReplaced(); // (NOT ORIGINAL: an edited copy's baseline taken again)
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
            m_cash[level] = 0;
            m_amount[level] = 1000;
        }
        m_startGoals[0] = 0;
        m_startFlags[0] = 0;
        m_startGoals[1] = 0;
        m_startFlags[1] = 0;
        m_totalCash = 0;
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
        m_info->AppendStructureIfAny(Script::GetStructure(0x0486FD79));
    }
    return true;
}

// 0x00584BA0
bool CSkaterCareer::ResetPart(uint32_t part)
{
    Reset(false, part);
    return true;
}

// 0x00584BC0
bool CSkaterCareer::AllGapsGot()
{
    Script::CArray* levels = Script::GetArray(0xEB1A4BC9);
    uint32_t count = levels->m_size;
    for (int level = 0; level < kLevels; ++level) {
        uint32_t i = 0;
        for (; i < count; ++i) {
            if (level == Script::GetInteger(levels->GetElement(i)))
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
void CSkaterCareer::StartLevel(int level)
{
    if (level == -1)
        level = m_level;
    else
        m_level = level;
    m_visited[m_level] = true;
    m_startGoals[0] = m_goals[level][0];
    m_startGoals[1] = m_goals[level][1];
    m_startFlags[0] = m_flags[level][0];
    m_startFlags[1] = m_flags[level][1];
}

// 0x00584CC0
bool CSkaterCareer::GetCheat(uint32_t name)
{
    int flag = Script::GetInteger(name);
    bool on = (m_globalFlags[Word(flag)] & Bit(flag)) != 0;
    GameVariable<bool>(0x007CE4FD) = on;
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

void** const kCareerVtable = reinterpret_cast<void**>(0x0064EFD4);

Script::CArray* NewArray(uint32_t size, Script::ESymbolType type)
{
    Script::CArray* memory = Script::NewArray();
    Script::CArray* array = memory ? memory->Init() : nullptr;
    array->SetSizeAndType(size, type);
    return array;
}
Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}

} // namespace

// 0x00585270
CSkaterCareer* CSkaterCareer::Construct()
{
    m_vtable = kCareerVtable;
    for (int level = 0; level < kLevels; ++level) {
        void* memory = Mem::New(sizeof(Game::CGapChecklist));
        m_gaps[level] = memory ? static_cast<Game::CGapChecklist*>(memory)->Construct() : nullptr;
        m_visited[level] = false;
    }
    m_info = NewStructure();
    Reset(true, 0);
    return this;
}

// 0x00585E40
void CSkaterCareer::DestroyBody()
{
    m_vtable = kCareerVtable;
    for (Game::CGapChecklist* gaps : m_gaps) {
        if (gaps) {
            gaps->DestroyBody();
            Mem::Manager::Get()->Free(gaps);
        }
    }
    if (Script::CStruct* info = m_info) {
        info->Clear();
        Script::DeleteStruct(info);
    }
}

// 0x00585EA0
CSkaterCareer* CSkaterCareer::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00584D00
void CSkaterCareer::ReadFromStructure(Script::CStruct* save)
{
    Script::CStruct* career = nullptr;
    save->GetStructure(kCareerStructure, &career, 0);
    Script::CArray* goals = nullptr;
    career->GetArray(kGoalsArray, &goals, 0);
    Script::CArray* flags = nullptr;
    career->GetArray(kFlagsArray, &flags, 0);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            GoalWords()[first + i] = goals->GetElement(first + i);
        for (uint32_t i = 0; i < 8; ++i)
            FlagWords()[first + i] = flags->GetElement(first + i);
    }
    Script::CArray* globals = nullptr;
    career->GetArray(kGlobalFlagsArray, &globals, 0);
    for (uint32_t i = 0; i < 16; ++i)
        m_globalFlags[i] = globals->GetElement(i);
    // The gap checklists.
    Script::CArray* gapLevels = nullptr;
    career->GetArray(kGapsArray, &gapLevels, 0);
    if (gapLevels) {
        for (int level = 0; level < kLevels; ++level) {
            Game::CGapChecklist* checklist = m_gaps[level];
            checklist->Clear();
            auto* entry = reinterpret_cast<Script::CStruct*>(gapLevels->GetElement(level));
            int savedLevel = 0, built = 0;
            entry->GetInteger(kGapLevel, &savedLevel, 1);
            entry->GetInteger(kGapsBuilt, &built, 1);
            if (!built)
                continue;
            Script::CArray* list = nullptr;
            entry->GetArray(kGapList, &list, 0);
            int count = static_cast<int>(list->m_size);
            for (int i = 0; i < count; ++i) {
                auto* gap = reinterpret_cast<Script::CStruct*>(list->GetElement(i));
                const char* name = nullptr;
                int got = 0, score = 0;
                gap->GetString(kGapName, &name, 0);
                gap->GetInteger(kGapCount, &got, 0);
                gap->GetInteger(kGapScore, &score, 0);
                checklist->AddGap(name, got, score);
                if (got) {
                    Script::VectorValue pos, dir;
                    gap->GetVector(kGapPos, &pos, 0);
                    gap->GetVector(kGapDir, &dir, 0);
                    checklist->SetGapPositions(name, {pos.x, pos.y, pos.z, pos.w}, {dir.x, dir.y, dir.z, dir.w});
                }
            }
        }
    }
    Script::CArray* visited = nullptr;
    career->GetArray(kVisitedArray, &visited, 0);
    if (visited) {
        for (int level = 0; level < kLevels; ++level)
            m_visited[level] = visited->GetElement(level) != 0;
    }
    Script::CArray* cash = nullptr;
    Script::CArray* amounts = nullptr;
    career->GetArray(kCashArray, &cash, 0);
    career->GetArray(kAmountArray, &amounts, 0);
    for (int level = 0; level < kLevels; ++level) {
        m_cash[level] = static_cast<uint16_t>(cash->GetElement(level));
        m_amount[level] = static_cast<uint16_t>(amounts->GetElement(level));
    }
    int value = 0; // (the original's is left as it was when a value is missing)
    career->GetInteger(kTotalCash, &value, 0);
    m_totalCash = static_cast<uint16_t>(value);
    career->GetInteger(kTotalAmount, &value, 0);
    m_totalAmount = static_cast<uint16_t>(value);
    Script::CArray* counters = nullptr;
    career->GetArray(kCounterArray, &counters, 0);
    for (int level = 0; level < 18; ++level)
        m_counter[level] = static_cast<uint8_t>(counters->GetElement(level));
    if (career->GetInteger(kTotalCounter, &value, 0)) {
        m_totalCounter = static_cast<uint8_t>(value);
        career->GetInteger(kFlag5EF, &value, 0);
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
    career->GetInteger(kValue634, &m_value634, 0);
    career->GetInteger(kMaxValue634, &m_maxValue634, 0);
    Script::CStruct* info = nullptr; // (the original's is left as it was)
    career->GetStructure(kInfoStructure, &info, 0);
    m_info->AppendStructureIfAny(info);
    Front::LevelEditor::Career::CareerReplaced(); // (NOT ORIGINAL: an edited copy's baseline taken again)
}

// 0x00585550
void CSkaterCareer::WriteToStructure(Script::CStruct* save)
{
    Script::CArray* goals = NewArray(kLevels * 8, Script::ESYMBOLTYPE_INTEGER);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            goals->SetElement(first + i, GoalWords()[first + i]);
    }
    Script::CArray* flags = NewArray(kLevels * 8, Script::ESYMBOLTYPE_INTEGER);
    for (uint32_t first = 0; first < kLevels * 8; first += 8) {
        for (uint32_t i = 0; i < 8; ++i)
            flags->SetElement(first + i, FlagWords()[first + i]);
    }
    Script::CArray* globals = NewArray(16, Script::ESYMBOLTYPE_INTEGER);
    for (uint32_t i = 0; i < 16; ++i)
        globals->SetElement(i, m_globalFlags[i]);
    Script::CArray* gapLevels = NewArray(kLevels, Script::ESYMBOLTYPE_STRUCTURE);
    for (int level = 0; level < kLevels; ++level) {
        Game::CGapChecklist* checklist = m_gaps[level];
        Script::CStruct* entry = NewStructure();
        entry->AddInteger(kGapLevel, level);
        // NOT ORIGINAL: an edited copy's checklist (level_editor_career.h) is written as before the edited copy - never
        // the editor's gaps, nor gaps landed in it.
        Front::LevelEditor::Career::Checklist view;
        if (Front::LevelEditor::Career::SaveView(checklist, &view)) {
            entry->AddInteger(kGapsBuilt, view.built ? 1 : 0);
            if (view.built) {
                Script::CArray* list = NewArray(static_cast<uint32_t>(view.gaps.size()), Script::ESYMBOLTYPE_STRUCTURE);
                for (size_t i = 0; i < view.gaps.size(); ++i) {
                    const Front::LevelEditor::Career::Gap& g = view.gaps[i];
                    Script::CStruct* gap = NewStructure();
                    gap->AddString(kGapName, g.name.c_str());
                    gap->AddInteger(kGapCount, g.count);
                    gap->AddInteger(kGapScore, g.score);
                    gap->AddVector(kGapPos, g.pos[0], g.pos[1], g.pos[2]);
                    gap->AddVector(kGapDir, g.dir[0], g.dir[1], g.dir[2]);
                    list->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(gap));
                }
                entry->AddArrayPointer(kGapList, list);
            }
        } else if (!checklist->m_built) {
            entry->AddInteger(kGapsBuilt, 0);
        } else {
            entry->AddInteger(kGapsBuilt, 1);
            int count = checklist->Count();
            Script::CArray* list = NewArray(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_STRUCTURE);
            for (int i = 0; i < count; ++i) {
                Script::CStruct* gap = NewStructure();
                gap->AddString(kGapName, checklist->GetGapName(i));
                gap->AddInteger(kGapCount, checklist->GetGapCount(i));
                gap->AddInteger(kGapScore, checklist->GetGapScore(i));
                Mth::Vector pos = {0.0f, 0.0f, 0.0f, 1.0f}, dir = {0.0f, 0.0f, 0.0f, 1.0f};
                if (checklist->GetGapCount(i))
                    checklist->GetGapPositions(i, &pos, &dir);
                gap->AddVector(kGapPos, pos.x, pos.y, pos.z);
                gap->AddVector(kGapDir, dir.x, dir.y, dir.z);
                list->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(gap));
            }
            entry->AddArrayPointer(kGapList, list);
        }
        gapLevels->SetElement(static_cast<uint32_t>(level), reinterpret_cast<uint32_t>(entry));
    }
    Script::CArray* visited = NewArray(kLevels, Script::ESYMBOLTYPE_INTEGER);
    for (int level = 0; level < kLevels; ++level)
        visited->SetElement(static_cast<uint32_t>(level), m_visited[level]);
    Script::CStruct* career = NewStructure();
    career->AddComponentOfType(kGoalsArray, Script::ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(goals));
    career->AddComponentOfType(kFlagsArray, Script::ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(flags));
    career->AddComponentOfType(kGlobalFlagsArray, Script::ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(globals));
    career->AddComponentOfType(kGapsArray, Script::ESYMBOLTYPE_ARRAY, reinterpret_cast<uint32_t>(gapLevels));
    career->AddArrayPointer(kVisitedArray, visited);
    career->AddChecksum(0xE3335D2F, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(Mdl::g_skateModule) + 0x378)); // (the skate module's)
    career->AddChecksum(0x2CC06F5E, static_cast<Game::CGameMode*>(static_cast<void*>(Game::g_gameMode))->GetNameChecksum());
    // (both made first, then sized, as the original)
    Script::CArray* cashMemory = Script::NewArray();
    Script::CArray* cash = cashMemory ? cashMemory->Init() : nullptr;
    Script::CArray* amountMemory = Script::NewArray();
    Script::CArray* amounts = amountMemory ? amountMemory->Init() : nullptr;
    cash->SetSizeAndType(kLevels, Script::ESYMBOLTYPE_INTEGER);
    amounts->SetSizeAndType(kLevels, Script::ESYMBOLTYPE_INTEGER);
    for (int level = 0; level < kLevels; ++level) {
        cash->SetElement(static_cast<uint32_t>(level), m_cash[level]);
        amounts->SetElement(static_cast<uint32_t>(level), m_amount[level]);
    }
    career->AddArrayPointer(kCashArray, cash);
    career->AddArrayPointer(kAmountArray, amounts);
    career->AddInteger(kTotalCash, m_totalCash);
    career->AddInteger(kTotalAmount, m_totalAmount);
    Script::CArray* counters = NewArray(18, Script::ESYMBOLTYPE_INTEGER);
    for (int level = 0; level < 18; ++level)
        counters->SetElement(static_cast<uint32_t>(level), m_counter[level]);
    career->AddArrayPointer(kCounterArray, counters);
    career->AddInteger(kTotalCounter, m_totalCounter);
    career->AddInteger(kFlag5EF, m_flag5EF);
    career->AddInteger(kValue634, m_value634);
    career->AddInteger(kMaxValue634, m_maxValue634);
    career->AddStructure(kInfoStructure, m_info);
    save->AddComponentOfType(kCareerStructure, 11, reinterpret_cast<uint32_t>(career));
}

// 0x00585E20 (GetCareerSaveInfo): the skate module's career written to the script's results.
bool __cdecl CFunc_GetCareerSaveInfo(Script::CStruct* /*params*/, Script::CScript* script)
{
    auto* career = *reinterpret_cast<CSkaterCareer**>(reinterpret_cast<uint8_t*>(Mdl::g_skateModule) + 0x20);
    career->WriteToStructure(*reinterpret_cast<Script::CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14));
    return true;
}

} // namespace Obj

REPLACE(0x00584110, &Obj::CSkaterCareer::GetGapChecklist);
REPLACE(0x00584130, &Obj::CSkaterCareer::GetLevel);
REPLACE(0x00584140, &Obj::CSkaterCareer::SetGoal);
REPLACE(0x00584180, &Obj::CSkaterCareer::UnSetGoal);
REPLACE(0x005841D0, &Obj::CSkaterCareer::GetGoal);
REPLACE(0x00584210, &Obj::CSkaterCareer::JustGotGoal);
REPLACE(0x00584250, &Obj::CSkaterCareer::CountGoals);
REPLACE(0x005842A0, &Obj::CSkaterCareer::CountTotalGoals);
REPLACE(0x00584320, &Obj::CSkaterCareer::CountLevelsWithGoals);
REPLACE(0x005843A0, &Obj::CSkaterCareer::SetGlobalFlag);
REPLACE(0x005843D0, &Obj::CSkaterCareer::UnSetGlobalFlag);
REPLACE(0x00584400, &Obj::CSkaterCareer::GetGlobalFlag);
REPLACE(0x00584430, &Obj::CSkaterCareer::SetFlag);
REPLACE(0x00584470, &Obj::CSkaterCareer::UnSetFlag);
REPLACE(0x005844C0, &Obj::CSkaterCareer::GetFlag);
REPLACE(0x00584500, &Obj::CSkaterCareer::JustGotFlag);
REPLACE(0x00584540, &Obj::CSkaterCareer::GetInfoInteger);
REPLACE(0x00584560, &Obj::CSkaterCareer::SetInfoInteger);
REPLACE(0x00584580, &Obj::CSkaterCareer::AddToInfo471FC4DD);
REPLACE(0x005845C0, &Obj::CSkaterCareer::AddCash);
REPLACE(0x005845F0, &Obj::CSkaterCareer::SetLevelAmount);
REPLACE(0x00584620, &Obj::CSkaterCareer::IncrementCounter);
REPLACE(0x00584640, &Obj::CSkaterCareer::GetCounter);
REPLACE(0x00584650, &Obj::CSkaterCareer::SetFlag5EF);
REPLACE(0x00584660, &Obj::CSkaterCareer::SetValue634);
REPLACE(0x00584680, &Obj::CSkaterCareer::Reset);
REPLACE(0x00584BA0, &Obj::CSkaterCareer::ResetPart);
REPLACE(0x00584BC0, &Obj::CSkaterCareer::AllGapsGot);
REPLACE(0x00584C60, &Obj::CSkaterCareer::StartLevel);
REPLACE(0x00584CC0, &Obj::CSkaterCareer::GetCheat);
REPLACE(0x00584D00, &Obj::CSkaterCareer::ReadFromStructure);
REPLACE(0x00585270, &Obj::CSkaterCareer::Construct);
REPLACE(0x00585550, &Obj::CSkaterCareer::WriteToStructure);
REPLACE(0x00585E20, Obj::CFunc_GetCareerSaveInfo);
REPLACE(0x00585E40, &Obj::CSkaterCareer::DestroyBody);
REPLACE(0x00585EA0, &Obj::CSkaterCareer::Destroy);
