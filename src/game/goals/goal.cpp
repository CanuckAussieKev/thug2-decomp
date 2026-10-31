// GoalSystem::CObjective, the goal base class: 0x00553E00-0x00557ABF (see goal.h).

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

namespace GoalSystem {

namespace {

using namespace GoalCalls;

using namespace GoalNames;

// The x87 code's float to int conversion (_ftol): truncation.
int Truncate(double value) { return static_cast<int>(value); }

} // namespace

// 0x00553E00
void CObjective::PassInheritedFlags(int depth)
{
    void* manager = ObjectiveBoard();
    for (GoalLink* link = m_subNodes; link && link->m_objectiveKey; link = link->m_next) {
        CObjective* child = GetGoal(manager, link->m_objectiveKey, 1);
        if (child) {
            child->m_inherited |= m_inherited;
            child->PassInheritedFlags(depth + 1);
        }
    }
}

// 0x00553E50
void CObjective::ClearChildWins()
{
    void* manager = ObjectiveBoard();
    for (GoalLink* link = m_subNodes; link && link->m_objectiveKey; link = link->m_next) {
        link->m_won = false;
        CObjective* child = GetGoal(manager, link->m_objectiveKey, 1);
        if (child)
            child->ClearChildWins();
    }
}

// 0x00553E90
Scripts::CStruct* CObjective::GetParams() { return m_params; }

// 0x00553EA0
void CObjective::ResetTimer()
{
    int unlimited = 0;
    m_params->FetchIntValue(kUnlimitedTime, &unlimited, 0);
    if (unlimited == 1)
        return;
    m_secondsRemaining = 120;
    int seconds;
    if (m_params->FetchIntValue(kTime, &seconds, 0))
        m_secondsRemaining = seconds;
    m_secondsRemaining = m_secondsRemaining * 1000 + 20;
    m_state &= ~kStateNoTimeLimit;
}

// 0x00553F10
void CObjective::SetSecondsRemaining(int seconds) { m_secondsRemaining = seconds * 1000; }

// 0x00553F20
int CObjective::GetSecondsRemaining() { return m_secondsRemaining; }

// 0x00553F30
void CObjective::AdvanceClock(int seconds) { m_secondsRemaining += seconds * 1000 + 20; }

// 0x00553F50
void CObjective::SetActive() { m_state |= kStateActive; }

// 0x00553F60
void CObjective::ClearEngagedBit() { m_state &= ~kStateActive; }

// 0x00553F70
bool CObjective::RewriteArgs(Scripts::CStruct* params)
{
    m_params->AppendStructureIfAny(params);
    return false;
}

// 0x00553F90
bool CObjective::IsActive() { return (m_state & kStateActive) != 0; }

// 0x00553FA0
bool CObjective::HasSeen() { return (m_flags & kFlagSeen) != 0; }

// 0x00553FB0
void CObjective::Pause() { m_state |= kStatePaused; }

// 0x00553FC0
void CObjective::UnPause() { m_state &= ~kStatePaused; }

// 0x00553FD0
bool CObjective::BitMatches(uint32_t flag, int value, uint32_t sub)
{
    Scripts::CStruct* flags = m_goalFlags;
    if (sub) {
        // (a sub-structure that isn't there leaves the name itself as the pointer, as the original)
        flags = reinterpret_cast<Scripts::CStruct*>(sub);
        m_goalFlags->ReadSubStruct(sub, &flags, 1);
    }
    if (!flags->HasEntryNamed(flag))
        return false;
    // (the original's local is the sub-structure argument's slot: what the lookup left there, or 0)
    int set = sub ? static_cast<int>(reinterpret_cast<uintptr_t>(flags)) : 0;
    flags->FetchIntValue(flag, &set, 1);
    return value == set;
}

// 0x00554030
void CObjective::ClearFlagValues(Scripts::CStruct* flags)
{
    Scripts::CComponent* component = flags->StepToEntry(nullptr);
    while (component) {
        Scripts::CComponent* next = flags->StepToEntry(component);
        // (num_flags isn't stepped over: a structure with it loops here for ever, as in the original)
        if (component->name != kNumFlags) {
            component->raw = 0;
            component = next;
        }
    }
}

// 0x00554070
void CObjective::ZeroObjectiveBits()
{
    if (m_goalFlags->HasBareFlag(kSkaterFlags)) {
        Scripts::CComponent* component = m_goalFlags->StepToEntry(nullptr);
        while (component) {
            Scripts::CComponent* next = m_goalFlags->StepToEntry(component);
            if ((component->typeAndFlag & 0xFE) == (Scripts::QBVAL_STRUCT << 1)) {
                Scripts::CStruct* skater = component->structure;
                Scripts::CComponent* flag = skater->StepToEntry(nullptr);
                while (flag) {
                    Scripts::CComponent* following = skater->StepToEntry(flag);
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
    m_params->PutInt(kNumFlagsSet, 0);
}

// 0x00554110
bool CObjective::HasEveryBit()
{
    int toWin = m_numFlags;
    m_params->FetchIntValue(kNumFlagsToWin, &toWin, 0);
    if (m_goalFlags->HasBareFlag(kSkaterFlags)) {
        for (Scripts::CComponent* component = m_goalFlags->StepToEntry(nullptr); component;) {
            Scripts::CComponent* next = m_goalFlags->StepToEntry(component);
            if ((component->typeAndFlag & 0xFE) == (Scripts::QBVAL_STRUCT << 1)) {
                Scripts::CStruct* skater = component->structure;
                int set;
                int count;
                skater->FetchIntValue(kNumFlagsSet, &set, 1);
                skater->FetchIntValue(kNumFlags, &count, 1);
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
bool CObjective::BitIsOn(uint32_t flag, uint32_t sub)
{
    Scripts::CStruct* flags = m_goalFlags;
    if (sub) {
        Scripts::CStruct* found = reinterpret_cast<Scripts::CStruct*>(sub); // (as BitMatches)
        flags->ReadSubStruct(sub, &found, 1);
        flags = found;
    }
    // (an integer that isn't there leaves the goal's address in the original's local: true)
    int set = static_cast<int>(reinterpret_cast<uintptr_t>(this));
    flags->FetchIntValue(flag, &set, 1);
    return set != 0;
}

// 0x00554220
bool CObjective::IsLocked()
{
    void* manager = ObjectiveBoard();
    return (m_flags & kFlagLocked) || IsGroupLocked(manager, m_bundle, m_level);
}

// 0x00554250
bool CObjective::IsGraffiti() { return m_params->HasBareFlag(kGraffiti); }

// 0x00554260
bool CObjective::IsSpecial() { return m_params->HasBareFlag(kSpecial); }

// 0x00554270
bool CObjective::IsTetris() { return m_params->HasBareFlag(kTetris); }

// 0x00554280
uint32_t CObjective::GetObjectiveKey()
{
    uint32_t id = 0;
    m_params->ReadHashValue(kGoalIdName, &id, 1);
    return id;
}

// 0x005542B0
bool CObjective::IsUserMadeObjective() { return m_params->HasBareFlag(kEditedGoal); }

// 0x00554300
bool CObjective::IsEditedGoal2() { return m_params->HasBareFlag(kEditedGoal); }

// 0x005542C0
bool CObjective::HasWonGoalOrChild()
{
    if (Virtual<bool>(this, kVtHasWon))
        return true;
    for (GoalLink* link = m_subNodes; link; link = link->m_next) {
        if (link->m_won)
            return true;
    }
    return false;
}

// 0x005542F0
bool CObjective::IsSelected() { return (m_flags & kFlagSelected) != 0; }
// 0x005519F0
bool CObjective::IsPrepared() { return (m_flags & kFlagInitialized) != 0; }

// 0x00554320
void CObjective::PickedUpCountable()
{
    int collected = 0;
    m_params->FetchIntValue(kNumberCollected, &collected, 0);
    ++collected;
    m_params->PutInt(kNumberCollected, collected);
    if (m_params->HasBareFlag(kUseHudCounter))
        Scripts::LaunchScript(kGoalUpdateCounter, m_params, nullptr, false, nullptr);
}

// 0x00554390
bool CObjective::TallyObjectiveFinished()
{
    int collected = 0;
    m_params->FetchIntValue(kNumberCollected, &collected, 0);
    int number;
    m_params->FetchIntValue(kNumber, &number, 1);
    return collected >= number;
}

// 0x005543E0
bool CObjective::State02() { return (m_state & kState02) != 0; }

// 0x005543F0
bool CObjective::HasProsetPrefix(const char* prefix)
{
    const char* own;
    if (!m_params->GetString(kProsetPrefix, &own, 0))
        return false;
    return _stricmp(own, prefix) == 0;
}

// 0x00554430
void CObjective::MarkViewed() { m_flags |= kFlagSeen; }

// 0x00554440
void CObjective::Select() { m_flags |= kFlagSelected; }

// 0x00554450
void CObjective::Deselect() { m_flags &= ~kFlagSelected; }

// 0x00554460
bool CObjective::MayTryAgain() { return m_params->HasBareFlag(kCanRetryGoal); }

// 0x00554470
void CObjective::ResetCurrentTime() { m_params->PutInt(kCurrentTime, 0); }

// 0x00554480
void CObjective::UpdateCurrentTime()
{
    if (Virtual<bool>(this, kVtIsPaused))
        return;
    // (a current_time that isn't there leaves the goal's address in the original's local)
    int time = static_cast<int>(reinterpret_cast<uintptr_t>(this));
    m_params->FetchIntValue(kCurrentTime, &time, 0);
    time += Truncate(FrameClock::GetSlomoFrameSeconds() * Gfx2D::g_thousand); // (1000)
    m_params->PutInt(kCurrentTime, time);
    uint32_t ms = static_cast<uint32_t>(time);
    m_params->PutInt(kCurrentTimeSeconds, static_cast<int>(ms / 1000));
    m_params->PutInt(kCurrentTimeFraction, static_cast<int>(ms / 10 % 100));
}

// 0x00554510
void CObjective::SetQuickStart() { m_params->PutNameKey(kNoName, kQuickStart); }

// 0x00554520
void CObjective::UnsetQuickStart() { m_params->RemoveFlag(kQuickStart); }

// 0x00554530
bool CObjective::IsProChallenge() { return m_params->HasEntryNamed(kProGoal); }

// 0x00554540
bool CObjective::IsOnlineObjective() { return m_params->HasEntryNamed(kNet); }

// 0x00554550
bool CObjective::IsContestObjective() { return m_params->HasBareFlag(kCompetition); }

// 0x00554560
bool CObjective::IsPaused() { return (m_state & kStatePaused) != 0; }

// 0x00554570
bool CObjective::GetObjectiveListCaption(const char** text) { return m_params->GetString(kViewGoalsTextName, text, 0); }

// 0x00554590
bool CObjective::EndOfRunStarted() { return (m_flags & kFlagEndOfRunStarted) != 0; }

// 0x005545A0
bool CObjective::HasTimeLimit()
{
    int unlimited = 0;
    m_params->FetchIntValue(kUnlimitedTime, &unlimited, 0);
    if (m_params->HasBareFlag(kUnlimitedTime))
        unlimited = 1;
    return unlimited == 0;
}

// 0x005545F0
bool CObjective::HasComponentA302D7CD() { return m_params->HasEntryNamed(kComponentA302D7CD); }

// 0x00554610
void CObjective::RestoreSaveBlock(Scripts::CStruct* data)
{
    int beaten = 0;
    data->FetchIntValue(kHasBeaten, &beaten, 1);
    if (beaten)
        Virtual<void>(this, kVtMarkWon);
    int locked = 1;
    data->FetchIntValue(kIsLocked, &locked, 1);
    if (!locked)
        m_flags &= ~kFlagLocked;
    int seen = 0;
    data->FetchIntValue(kHasSeen, &seen, 1);
    if (seen)
        m_flags |= kFlagSeen;
    int record = 0;
    data->FetchIntValue(kWinRecord, &record, 1);
    m_params->PutInt(kWinRecord, record);
    uint32_t type;
    if (m_params->ReadHashValue(kRecordType, &type, 0)) {
        if (type == kTime)
            m_bestTime = record;
        else if (type == kScore)
            m_bestScore = record;
    }
    if (data->HasEntryNamed(kGoalFlags)) {
        m_goalFlags->Clear();
        Scripts::CStruct* flags = nullptr;
        data->ReadSubStruct(kGoalFlags, &flags, 1);
        m_goalFlags->AppendStructureIfAny(flags);
        m_numFlags = 0;
        m_numFlagsSet = 0;
        for (Scripts::CComponent* component = m_goalFlags->StepToEntry(nullptr); component;) {
            Scripts::CComponent* next = m_goalFlags->StepToEntry(component);
            ++m_numFlags;
            if (component->raw)
                ++m_numFlagsSet;
            component = next;
        }
    }
}

// 0x00554770
void CObjective::CaptureSaveBlock(Scripts::CStruct* data)
{
    data->PutInt(kLevelNum, m_level);
    void* manager = ObjectiveBoard();
    bool locked = (m_flags & kFlagLocked) || IsGroupLocked(manager, m_bundle, m_level);
    data->PutInt(kIsLocked, locked);
    data->PutInt(kHasSeen, (m_flags & kFlagSeen) ? 1 : 0);
    data->PutInt(kHasBeaten, Virtual<bool>(this, kVtHasWon));
    int record = 0;
    uint32_t type;
    if (m_params->ReadHashValue(kRecordType, &type, 0)) {
        if (type == kTime)
            record = m_bestTime;
        else if (type == kScore)
            record = m_bestScore;
    }
    data->PutInt(kWinRecord, record);
    data->PutNameKey(kNoName, m_params->HasEntryNamed(kClassicModeGoal) ? kClassic : kCareer);
    if (m_params->HasBareFlag(kSaveGoalFlags) && !m_goalFlags->IsEmpty())
        data->PutSubStruct(kGoalFlags, m_goalFlags);
}

// 0x00554890
void CObjective::ReplaceTrickTokens(const char* text, char* out, Scripts::CArray* keyCombos, Scripts::CArray* tricks, int)
{
    if (!*text) {
        *out = 0;
        return;
    }
    while (*text) {
        char c = text[0];
        Scripts::CArray* source = nullptr;
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
        const char* replacement = source->FindInlineText(static_cast<uint32_t>(number - 1));
        std::snprintf(out, 0x7FFFFFFF, replacement); // (the original: sprintf, unbounded, the text as the format)
        int length = static_cast<int>(std::strlen(replacement));
        if (length > 0)
            out += length;
        text += 3;
    }
    *out = 0;
}

// 0x005549A0
int CObjective::CountPickedUp()
{
    int collected = 0;
    if (!m_params->FetchIntValue(kNumberCollected, &collected, 0))
        collected = m_numFlagsSet;
    return collected;
}

// 0x005549D0
int CObjective::GetNumFlagsToWin()
{
    int toWin = m_numFlags;
    m_params->FetchIntValue(kNumFlagsToWin, &toWin, 0);
    return toWin ? toWin : -1;
}

// 0x00554A00
int CObjective::GetTimesStarted() { return m_timesStarted; }

// 0x00554A10
void CObjective::ChooseRunEnding(int type) { m_runEnding = type; }

// 0x00554A20
void CObjective::SetDeactivateOnExpire(bool deactivate)
{
    m_flags = static_cast<uint8_t>((m_flags & ~kFlagDeactivateOnExpire) | ((deactivate << 6) & kFlagDeactivateOnExpire));
}

// 0x00554A40
bool CObjective::IsHorse() { return m_params->HasBareFlag(kHorse); }

// 0x00554A50
bool CObjective::IsFindGaps() { return m_params->HasBareFlag(kFindGaps); }

// 0x00554A60
bool CObjective::HasNoTimeLimit() { return (m_state & kStateNoTimeLimit) != 0; }

// 0x00554A70
bool CObjective::RequiresCombo() { return m_params->HasBareFlag(kRequireCombo); }

} // namespace Game
