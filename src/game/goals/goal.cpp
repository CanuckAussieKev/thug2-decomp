// Game::CGoal, the goal base class: 0x00553E00-0x00557ABF (see goal.h).

#include "game/goals/goal.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/gfx/tod_manager.h"

namespace Game {

namespace {

using namespace GoalCalls;

using namespace GoalNames;

// The x87 code's float to int conversion (_ftol): truncation.
int Truncate(double value) { return static_cast<int>(value); }

} // namespace

// 0x00553E00
void CGoal::PassInheritedFlags(int depth)
{
    void* manager = GoalManager();
    for (GoalLink* link = m_children; link && link->m_goalId; link = link->m_next) {
        CGoal* child = GetGoal(manager, link->m_goalId, 1);
        if (child) {
            child->m_inherited |= m_inherited;
            child->PassInheritedFlags(depth + 1);
        }
    }
}

// 0x00553E50
void CGoal::ClearChildWins()
{
    void* manager = GoalManager();
    for (GoalLink* link = m_children; link && link->m_goalId; link = link->m_next) {
        link->m_won = false;
        CGoal* child = GetGoal(manager, link->m_goalId, 1);
        if (child)
            child->ClearChildWins();
    }
}

// 0x00553E90
Script::CStruct* CGoal::GetParams() { return m_params; }

// 0x00553EA0
void CGoal::ResetTimer()
{
    int unlimited = 0;
    m_params->GetInteger(kUnlimitedTime, &unlimited, 0);
    if (unlimited == 1)
        return;
    m_timeLeft = 120;
    int seconds;
    if (m_params->GetInteger(kTime, &seconds, 0))
        m_timeLeft = seconds;
    m_timeLeft = m_timeLeft * 1000 + 20;
    m_state &= ~kStateNoTimeLimit;
}

// 0x00553F10
void CGoal::SetTimeLeft(int seconds) { m_timeLeft = seconds * 1000; }

// 0x00553F20
int CGoal::GetTimeLeft() { return m_timeLeft; }

// 0x00553F30
void CGoal::AddTime(int seconds) { m_timeLeft += seconds * 1000 + 20; }

// 0x00553F50
void CGoal::SetActive() { m_state |= kStateActive; }

// 0x00553F60
void CGoal::SetInactive() { m_state &= ~kStateActive; }

// 0x00553F70
bool CGoal::EditParams(Script::CStruct* params)
{
    m_params->AppendStructureIfAny(params);
    return false;
}

// 0x00553F90
bool CGoal::IsActive() { return (m_state & kStateActive) != 0; }

// 0x00553FA0
bool CGoal::HasSeen() { return (m_flags & kFlagSeen) != 0; }

// 0x00553FB0
void CGoal::Pause() { m_state |= kStatePaused; }

// 0x00553FC0
void CGoal::UnPause() { m_state &= ~kStatePaused; }

// 0x00553FD0
bool CGoal::GoalFlagEquals(uint32_t flag, int value, uint32_t sub)
{
    Script::CStruct* flags = m_goalFlags;
    if (sub) {
        // (a sub-structure that isn't there leaves the name itself as the pointer, as the original)
        flags = reinterpret_cast<Script::CStruct*>(sub);
        m_goalFlags->GetStructure(sub, &flags, 1);
    }
    if (!flags->ContainsComponentNamed(flag))
        return false;
    // (the original's local is the sub-structure argument's slot: what the lookup left there, or 0)
    int set = sub ? static_cast<int>(reinterpret_cast<uintptr_t>(flags)) : 0;
    flags->GetInteger(flag, &set, 1);
    return value == set;
}

// 0x00554030
void CGoal::ClearFlagValues(Script::CStruct* flags)
{
    Script::CComponent* component = flags->GetNextComponent(nullptr);
    while (component) {
        Script::CComponent* next = flags->GetNextComponent(component);
        // (num_flags isn't stepped over: a structure with it loops here for ever, as in the original)
        if (component->name != kNumFlags) {
            component->raw = 0;
            component = next;
        }
    }
}

// 0x00554070
void CGoal::ResetGoalFlags()
{
    if (m_goalFlags->ContainsFlag(kSkaterFlags)) {
        Script::CComponent* component = m_goalFlags->GetNextComponent(nullptr);
        while (component) {
            Script::CComponent* next = m_goalFlags->GetNextComponent(component);
            if ((component->typeAndFlag & 0xFE) == (Script::ESYMBOLTYPE_STRUCTURE << 1)) {
                Script::CStruct* skater = component->structure;
                Script::CComponent* flag = skater->GetNextComponent(nullptr);
                while (flag) {
                    Script::CComponent* following = skater->GetNextComponent(flag);
                    // (the same endless loop at num_flags as ClearFlagValues)
                    if (flag->name != kNumFlags) {
                        flag->raw = 0;
                        flag = following;
                    }
                }
            }
            component = next;
        }
    } else {
        ClearFlagValues(m_goalFlags);
    }
    m_numFlagsSet = 0;
    m_params->AddInteger(kNumFlagsSet, 0);
}

// 0x00554110
bool CGoal::AllFlagsSet()
{
    int toWin = m_numFlags;
    m_params->GetInteger(kNumFlagsToWin, &toWin, 0);
    if (m_goalFlags->ContainsFlag(kSkaterFlags)) {
        for (Script::CComponent* component = m_goalFlags->GetNextComponent(nullptr); component;) {
            Script::CComponent* next = m_goalFlags->GetNextComponent(component);
            if ((component->typeAndFlag & 0xFE) == (Script::ESYMBOLTYPE_STRUCTURE << 1)) {
                Script::CStruct* skater = component->structure;
                int set;
                int count;
                skater->GetInteger(kNumFlagsSet, &set, 1);
                skater->GetInteger(kNumFlags, &count, 1);
                if (set < count)
                    return false;
            }
            component = next;
        }
        return true;
    }
    if (!toWin)
        return false;
    return m_numFlagsSet >= toWin;
}

// 0x005541E0
bool CGoal::GoalFlagSet(uint32_t flag, uint32_t sub)
{
    Script::CStruct* flags = m_goalFlags;
    if (sub) {
        Script::CStruct* found = reinterpret_cast<Script::CStruct*>(sub); // (as GoalFlagEquals)
        flags->GetStructure(sub, &found, 1);
        flags = found;
    }
    // (an integer that isn't there leaves the goal's address in the original's local: true)
    int set = static_cast<int>(reinterpret_cast<uintptr_t>(this));
    flags->GetInteger(flag, &set, 1);
    return set != 0;
}

// 0x00554220
bool CGoal::IsLocked()
{
    void* manager = GoalManager();
    return (m_flags & kFlagLocked) || IsGroupLocked(manager, m_group, m_level);
}

// 0x00554250
bool CGoal::IsGraffiti() { return m_params->ContainsFlag(kGraffiti); }

// 0x00554260
bool CGoal::IsSpecial() { return m_params->ContainsFlag(kSpecial); }

// 0x00554270
bool CGoal::IsTetris() { return m_params->ContainsFlag(kTetris); }

// 0x00554280
uint32_t CGoal::GetGoalId()
{
    uint32_t id = 0;
    m_params->GetChecksum(kGoalIdName, &id, 1);
    return id;
}

// 0x005542B0
bool CGoal::IsEditedGoal() { return m_params->ContainsFlag(kEditedGoal); }

// 0x00554300
bool CGoal::IsEditedGoal2() { return m_params->ContainsFlag(kEditedGoal); }

// 0x005542C0
bool CGoal::HasWonGoalOrChild()
{
    if (Virtual<bool>(this, kVtHasWon))
        return true;
    for (GoalLink* link = m_children; link; link = link->m_next) {
        if (link->m_won)
            return true;
    }
    return false;
}

// 0x005542F0
bool CGoal::IsSelected() { return (m_flags & kFlagSelected) != 0; }
// 0x005519F0
bool CGoal::IsInitialized() { return (m_flags & kFlagInitialized) != 0; }

// 0x00554320
void CGoal::GotCounterObject()
{
    int collected = 0;
    m_params->GetInteger(kNumberCollected, &collected, 0);
    ++collected;
    m_params->AddInteger(kNumberCollected, collected);
    if (m_params->ContainsFlag(kUseHudCounter))
        Script::RunScript(kGoalUpdateCounter, m_params, nullptr, false, nullptr);
}

// 0x00554390
bool CGoal::CounterGoalDone()
{
    int collected = 0;
    m_params->GetInteger(kNumberCollected, &collected, 0);
    int number;
    m_params->GetInteger(kNumber, &number, 1);
    return collected >= number;
}

// 0x005543E0
bool CGoal::State02() { return (m_state & kState02) != 0; }

// 0x005543F0
bool CGoal::HasProsetPrefix(const char* prefix)
{
    const char* own;
    if (!m_params->GetString(kProsetPrefix, &own, 0))
        return false;
    return _stricmp(own, prefix) == 0;
}

// 0x00554430
void CGoal::SetHasSeen() { m_flags |= kFlagSeen; }

// 0x00554440
void CGoal::Select() { m_flags |= kFlagSelected; }

// 0x00554450
void CGoal::Deselect() { m_flags &= ~kFlagSelected; }

// 0x00554460
bool CGoal::CanRetry() { return m_params->ContainsFlag(kCanRetryGoal); }

// 0x00554470
void CGoal::ResetCurrentTime() { m_params->AddInteger(kCurrentTime, 0); }

// 0x00554480
void CGoal::UpdateCurrentTime()
{
    if (Virtual<bool>(this, kVtIsPaused))
        return;
    // (a current_time that isn't there leaves the goal's address in the original's local)
    int time = static_cast<int>(reinterpret_cast<uintptr_t>(this));
    m_params->GetInteger(kCurrentTime, &time, 0);
    time += Truncate(Tmr::GetSlomoFrameSeconds() * Gfx::g_thousand); // (1000)
    m_params->AddInteger(kCurrentTime, time);
    uint32_t ms = static_cast<uint32_t>(time);
    m_params->AddInteger(kCurrentTimeSeconds, static_cast<int>(ms / 1000));
    m_params->AddInteger(kCurrentTimeFraction, static_cast<int>(ms / 10 % 100));
}

// 0x00554510
void CGoal::SetQuickStart() { m_params->AddChecksum(kNoName, kQuickStart); }

// 0x00554520
void CGoal::UnsetQuickStart() { m_params->RemoveFlag(kQuickStart); }

// 0x00554530
bool CGoal::IsProGoal() { return m_params->ContainsComponentNamed(kProGoal); }

// 0x00554540
bool CGoal::IsNetGoal() { return m_params->ContainsComponentNamed(kNet); }

// 0x00554550
bool CGoal::IsCompetition() { return m_params->ContainsFlag(kCompetition); }

// 0x00554560
bool CGoal::IsPaused() { return (m_state & kStatePaused) != 0; }

// 0x00554570
bool CGoal::GetViewGoalsText(const char** text) { return m_params->GetString(kViewGoalsTextName, text, 0); }

// 0x00554590
bool CGoal::EndOfRunStarted() { return (m_flags & kFlagEndOfRunStarted) != 0; }

// 0x005545A0
bool CGoal::HasTimeLimit()
{
    int unlimited = 0;
    m_params->GetInteger(kUnlimitedTime, &unlimited, 0);
    if (m_params->ContainsFlag(kUnlimitedTime))
        unlimited = 1;
    return unlimited == 0;
}

// 0x005545F0
bool CGoal::HasComponentA302D7CD() { return m_params->ContainsComponentNamed(kComponentA302D7CD); }

// 0x00554610
void CGoal::LoadSaveData(Script::CStruct* data)
{
    int beaten = 0;
    data->GetInteger(kHasBeaten, &beaten, 1);
    if (beaten)
        Virtual<void>(this, kVtMarkWon);
    int locked = 1;
    data->GetInteger(kIsLocked, &locked, 1);
    if (!locked)
        m_flags &= ~kFlagLocked;
    int seen = 0;
    data->GetInteger(kHasSeen, &seen, 1);
    if (seen)
        m_flags |= kFlagSeen;
    int record = 0;
    data->GetInteger(kWinRecord, &record, 1);
    m_params->AddInteger(kWinRecord, record);
    uint32_t type;
    if (m_params->GetChecksum(kRecordType, &type, 0)) {
        if (type == kTime)
            m_bestTime = record;
        else if (type == kScore)
            m_bestScore = record;
    }
    if (data->ContainsComponentNamed(kGoalFlags)) {
        m_goalFlags->Clear();
        Script::CStruct* flags = nullptr;
        data->GetStructure(kGoalFlags, &flags, 1);
        m_goalFlags->AppendStructureIfAny(flags);
        m_numFlags = 0;
        m_numFlagsSet = 0;
        for (Script::CComponent* component = m_goalFlags->GetNextComponent(nullptr); component;) {
            Script::CComponent* next = m_goalFlags->GetNextComponent(component);
            ++m_numFlags;
            if (component->raw)
                ++m_numFlagsSet;
            component = next;
        }
    }
}

// 0x00554770
void CGoal::GetSaveData(Script::CStruct* data)
{
    data->AddInteger(kLevelNum, m_level);
    void* manager = GoalManager();
    bool locked = (m_flags & kFlagLocked) || IsGroupLocked(manager, m_group, m_level);
    data->AddInteger(kIsLocked, locked);
    data->AddInteger(kHasSeen, (m_flags & kFlagSeen) ? 1 : 0);
    data->AddInteger(kHasBeaten, Virtual<bool>(this, kVtHasWon));
    int record = 0;
    uint32_t type;
    if (m_params->GetChecksum(kRecordType, &type, 0)) {
        if (type == kTime)
            record = m_bestTime;
        else if (type == kScore)
            record = m_bestScore;
    }
    data->AddInteger(kWinRecord, record);
    data->AddChecksum(kNoName, m_params->ContainsComponentNamed(kClassicModeGoal) ? kClassic : kCareer);
    if (m_params->ContainsFlag(kSaveGoalFlags) && !m_goalFlags->IsEmpty())
        data->AddStructure(kGoalFlags, m_goalFlags);
}

// 0x00554890
void CGoal::ReplaceTrickTokens(const char* text, char* out, Script::CArray* keyCombos, Script::CArray* tricks, int)
{
    if (!*text) {
        *out = 0;
        return;
    }
    while (*text) {
        char c = text[0];
        Script::CArray* source = nullptr;
        if (c == '\\' && text[1] == 't' && text[2] <= '9' && text[2] >= '0')
            source = tricks;
        else if (c == '\\' && text[1] == 'k' && text[2] <= '9' && text[2] >= '0')
            source = keyCombos;
        if (!source) {
            *out++ = c;
            ++text;
            continue;
        }
        int number = text[2] - '0';
        if (text[3] <= '9' && text[3] >= '0') {
            number = number * 10 + (text[3] - '0');
            ++text;
        }
        const char* replacement = source->GetLocalString(static_cast<uint32_t>(number - 1));
        std::snprintf(out, 0x7FFFFFFF, replacement); // (the original: sprintf, unbounded, the text as the format)
        int length = static_cast<int>(std::strlen(replacement));
        if (length > 0)
            out += length;
        text += 3;
    }
    *out = 0;
}

// 0x005549A0
int CGoal::GetNumberCollected()
{
    int collected = 0;
    if (!m_params->GetInteger(kNumberCollected, &collected, 0))
        collected = m_numFlagsSet;
    return collected;
}

// 0x005549D0
int CGoal::GetNumFlagsToWin()
{
    int toWin = m_numFlags;
    m_params->GetInteger(kNumFlagsToWin, &toWin, 0);
    return toWin ? toWin : -1;
}

// 0x00554A00
int CGoal::GetTimesStarted() { return m_timesStarted; }

// 0x00554A10
void CGoal::SetEndRunType(int type) { m_endRunType = type; }

// 0x00554A20
void CGoal::SetDeactivateOnExpire(bool deactivate)
{
    m_flags = static_cast<uint8_t>((m_flags & ~kFlagDeactivateOnExpire) | ((deactivate << 6) & kFlagDeactivateOnExpire));
}

// 0x00554A40
bool CGoal::IsHorse() { return m_params->ContainsFlag(kHorse); }

// 0x00554A50
bool CGoal::IsFindGaps() { return m_params->ContainsFlag(kFindGaps); }

// 0x00554A60
bool CGoal::HasNoTimeLimit() { return (m_state & kStateNoTimeLimit) != 0; }

// 0x00554A70
bool CGoal::RequiresCombo() { return m_params->ContainsFlag(kRequireCombo); }

} // namespace Game
