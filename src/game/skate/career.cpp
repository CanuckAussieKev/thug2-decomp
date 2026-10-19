// The skater's career, 0x00584110-0x00584CF3. See career.h.

#include "game/skate/career.h"

#include <cstdint>

#include "game/script/struct.h"
#include "game/skate/gap_checklist.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

// Levels left out of the totals (a byte each).
bool* LeftOut() { return &GameVariable<bool>(0x007CE4E8); }

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
