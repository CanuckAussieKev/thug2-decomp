// GoalSystem::CGoalManager: its update, the timer on screen, unbeating, the view goals menu's list, the active goals'
// questions (0x0055DD10-0x0055E78D, 0x0055E790-0x0055EB3F) and the commands among them (see goal_manager.h,
// goal_manager_cfuncs.h).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "game/core/str.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/param_struct.h"
#include "game/skate/trick_goals.h"

namespace GoalSystem {

namespace {

using namespace GoalNames;

constexpr uint32_t kTheTime = 0xA6343CD4;          // the_time
constexpr uint32_t kShowGoalPoints = 0x888EF05E;   // GoalManager_ShowGoalPoints
constexpr uint32_t kViewGoalsOrder = 0x09B048FC;   // (name unknown: the goal's place in the view goals list, from 1)
constexpr uint32_t kText = 0xC4745838;             // text
constexpr uint32_t kRecordText = 0xBB48AF8B;       // (name unknown: the record's text)
constexpr uint32_t kNoRecord = 0x0C80E196;         // (name unknown: the unnamed checksum without a record type)
constexpr uint32_t kViewGoalsMenuAddItem = 0x380AEA65; // view_goals_menu_add_item
constexpr uint32_t kCountAll = 0xA21884B5;         // count_all
constexpr uint32_t kTrickBeat = 0xD0DD047B;        // TrickBeat

OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* made = Scripts::SpawnParamBag();
    return made ? made->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
Scripts::CArray* AllocScriptArray()
{
    Scripts::CArray* made = Scripts::AllocScriptArray();
    return made ? made->PrepareAll() : nullptr;
}

// The active list's nodes, counted (as GetEngagedObjective walks them).
int CountActive(MemSys::ListNode* head)
{
    MemSys::ListNode* node = head->next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return 0;
    int count = 0;
    do {
        node = node->next;
        ++count;
    } while (node->data != reinterpret_cast<void*>(-1) && node);
    return count;
}

} // namespace

// 0x0055DD10
void CGoalManager::UpdateTimeDisplay()
{
    char text[64];
    text[0] = 0;
    int count = CountActive(&m_activeGoals.m_listLink);
    for (int i = 0; i < count; ++i) {
        CObjective* goal = GetEngagedObjective(i);
        if (!Virtual<bool>(goal, CObjective::kVtSlot28))
            continue;
        int time = goal->GetSecondsRemaining();
        int minutes = 0, seconds = 0;
        if (time > 0) {
            minutes = time / 60000;
            seconds = (time - minutes * 60000) / 1000;
        }
        std::snprintf(text, sizeof(text), "%2i:%.2i", minutes, seconds); // "%2i:%.2i"
        break;
    }
    MenuSystem::ElementPtr element;
    MenuSystem::CUiTree::Sole()->FetchPart(&element, kTheTime, 0);
    if (element)
        static_cast<MenuSystem::CLabelWidget*>(element.m_ownerObject)->ChangeCaption(text);
}

// 0x0055DEA0
bool CGoalManager::HasActiveGapGoal()
{
    int count = CountActive(&m_activeGoals.m_listLink);
    for (int i = 0; i < count; ++i) {
        CObjective* goal = GetEngagedObjective(i);
        if (!Virtual<bool>(goal, CObjective::kVtIsActive))
            continue;
        Scripts::CArray* gaps = nullptr;
        if (goal->FetchArgs()->FetchList(kNeededGapList, &gaps, 0))
            return true;
    }
    return false;
}

// 0x0055DF90
void CGoalManager::RevokeEveryClear()
{
    OnlinePlay::Hub* net = NetSessionSlot();
    for (int i = 0; i < m_goals.m_num; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CObjective::kVtUnBeat) && !net->InNetGame()) {
            --m_goalsBeaten;
            --m_objectiveScore;
            --m_totalGoalPoints;
        }
    }
    if (!net->InNetGame())
        m_goalData->Clear();
    if (CountLiveGoals(false) == 0 && !static_cast<uint8_t>(ScriptCommands::CFunc_InMultiplayerGame(nullptr, nullptr)))
        Scripts::LaunchScript(kShowGoalPoints, nullptr, nullptr, false, nullptr);
}

// 0x0055E050
void CGoalManager::AppendObjectiveListEntries()
{
    int count = m_goals.m_num; // (read once)
    Scripts::CArray* ordered = AllocScriptArray();
    Scripts::CArray* unordered = AllocScriptArray();
    ordered->ConfigureSizeKind(static_cast<uint32_t>(count), Scripts::QBVAL_NAME);
    unordered->ConfigureSizeKind(static_cast<uint32_t>(count), Scripts::QBVAL_NAME);
    // The goals with a place in the list first, then the others.
    int numOrdered = 0, numUnordered = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t key;
        CObjective* goal = m_goals.GetByIndex(i, &key);
        if (goal->FetchArgs()->HasEntryNamed(kViewGoalsOrder))
            ordered->SetElement(static_cast<uint32_t>(numOrdered++), goal->GetObjectiveKey());
        else
            unordered->SetElement(static_cast<uint32_t>(numUnordered++), goal->GetObjectiveKey());
    }
    // Sorted by their places: for each slot the goal whose place is the slot's number (a goal not in the table is
    // read through null, as the original).
    for (int slot = 0; slot < numOrdered; ++slot) {
        for (int k = slot; k < numOrdered; ++k) {
            uint32_t id = ordered->FetchPart(static_cast<uint32_t>(k));
            int place;
            m_goals.Pull(id)->FetchArgs()->FetchIntValue(kViewGoalsOrder, &place, 1);
            if (place - 1 != slot)
                continue;
            if (k != slot) {
                uint32_t other = ordered->FetchPart(static_cast<uint32_t>(slot));
                int unused;
                m_goals.Pull(other)->FetchArgs()->FetchIntValue(kViewGoalsOrder, &unused, 1);
                ordered->SetElement(static_cast<uint32_t>(slot), id);
                ordered->SetElement(static_cast<uint32_t>(k), other);
            }
            break;
        }
    }
    for (int i = 0; i < numUnordered; ++i)
        ordered->SetElement(static_cast<uint32_t>(numOrdered + i), unordered->FetchPart(static_cast<uint32_t>(i)));
    // A menu item for each goal with view_goals_text (not pro goals until the cheat, not network goals): its id,
    // its text, its record if won.
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.Pull(ordered->FetchPart(static_cast<uint32_t>(i)));
        Scripts::CStruct* params = goal->FetchArgs();
        const char* text = nullptr;
        goal->GetObjectiveListCaption(&text);
        if (!text)
            continue;
        if (!m_allUnlocked && params->HasBareFlag(kProGoal))
            continue;
        if (goal->IsOnlineObjective())
            continue;
        Scripts::CStruct* item = SpawnParamTable();
        item->PutNameKey(kGoalIdChecksum, goal->GetObjectiveKey());
        item->AppendWord(kText, text);
        if (Virtual<bool>(goal, CObjective::kVtHasWon)) {
            char record[128];
            int value = 0;
            if (goal->IsContestObjective()) {
                params->FetchIntValue(kWinRecord, &value, 0);
                std::snprintf(record, sizeof(record), "%i", value); // "%i"
                const char* suffix = value == 1   ? "st place"  // "st place"
                                     : value == 2 ? "nd place"  // "nd place"
                                     : value == 3 ? "rd place"  // "rd place"
                                                  : "";
                std::size_t length = std::strlen(record);
                std::snprintf(record + length, sizeof(record) - length, "%s", suffix);
                item->AppendWord(kRecordText, record);
            } else {
                uint32_t type;
                if (params->ReadHashValue(kRecordType, &type, 0)) {
                    if (type == kScore) {
                        params->FetchIntValue(kWinRecord, &value, 0);
                        std::snprintf(record, sizeof(record), "%s", TextUtil::FormatWithSeparators(value)); // "%s"
                        item->AppendWord(kRecordText, record);
                    } else if (type == kTime) {
                        int ms = 0;
                        params->FetchIntValue(kWinRecord, &ms, 0);
                        int totalSeconds = ms / 1000;
                        int minutes = totalSeconds / 60;
                        std::snprintf(record, sizeof(record), "%i:%02i.%02i", minutes, totalSeconds - minutes * 60,
                                      (ms % 1000) / 10); // "%i:%02i.%02i"
                        item->AppendWord(kRecordText, record);
                    }
                } else {
                    item->PutNameKey(0u, kNoRecord);
                }
            }
        }
        Scripts::LaunchScript(kViewGoalsMenuAddItem, item, nullptr, false, nullptr);
        DeleteStructure(item);
    }
    Scripts::ReleaseArrayStorage(ordered);
    Scripts::ReleaseArrayStorage(unordered);
    for (Scripts::CArray* array : {ordered, unordered}) {
        if (array) {
            array->Clear();
            Scripts::DeleteArray(array);
        }
    }
}

// 0x0055E790
void CGoalManager::TrickLanded(Scripts::CStruct* trick)
{
    int count = CountActive(&m_activeGoals.m_listLink);
    for (int i = 0; i < count; ++i) {
        CObjective* goal = GetEngagedObjective(i);
        if (goal->FetchArgs()->HasBareFlag(kTrickBeat))
            reinterpret_cast<CTrickBeatGoal*>(goal)->TrickLanded(trick);
    }
}

// 0x0055E980
CObjective* CGoalManager::GetActiveNetGoal()
{
    int count = CountActive(&m_activeGoals.m_listLink);
    for (int i = 0; i < count; ++i) {
        CObjective* goal = GetEngagedObjective(i);
        if (goal->IsOnlineObjective())
            return goal;
    }
    return nullptr;
}

// 0x0055EA40
void CGoalManager::TickFrame()
{
    for (int i = 0; i < m_goals.m_num; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CObjective::kVtIsActive))
            Virtual<bool>(goal, CObjective::kVtUpdate);
        if (goal->State02()) {
            m_goals.DropOut(goal->GetObjectiveKey());
            Virtual<CObjective*>(goal, CObjective::kVtDestroy, static_cast<uint8_t>(1));
        }
    }
    UpdateTimeDisplay();
}

// 0x0055E860: active goals (count_all: network goals too, and those not active).
bool __cdecl CFunc_GoalManager_HasActiveGoals(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    bool all = params->HasBareFlag(kCountAll);
    return manager->CountLiveGoals(all) > 0;
}

// 0x0055E8A0
bool __cdecl CFunc_GoalManager_UnBeatAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->RevokeEveryClear();
    return true;
}

// 0x0055E8C0
bool __cdecl CFunc_GoalManager_AddViewGoalsList(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->AppendObjectiveListEntries();
    return true;
}

// 0x0055E8E0: an active goal is expired (the count asked again after each).
bool __cdecl CFunc_GoalManager_GoalShouldExpire(Scripts::CStruct*, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    if (!manager)
        return false;
    for (int i = 0; i < manager->CountLiveGoals(true); ++i) {
        if (Virtual<bool>(manager->GetEngagedObjective(i), CObjective::kVtIsExpired))
            return true;
    }
    return false;
}

// 0x0055EAC0
bool __cdecl CFunc_GoalManager_UpdateAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->TickFrame();
    return true;
}

// 0x0055EAE0: the active network goal's time left (ms) as the script's time_left.
bool __cdecl CFunc_GoalManager_GetTimeLeftInNetGame(Scripts::CStruct*, Scripts::CScript* script)
{
    if (CObjective* goal = GetObjectiveBoard()->GetActiveNetGoal())
        script->m_args->PutInt("time_left", goal->GetSecondsRemaining()); // "time_left"
    return true;
}

// 0x0055EB20: the active network goal has no time limit (true without one).
bool __cdecl CFunc_GoalManager_GetUnlimitedTimeInNetGame(Scripts::CStruct*, Scripts::CScript*)
{
    CObjective* goal = GetObjectiveBoard()->GetActiveNetGoal();
    return goal ? goal->HasNoTimeLimit() : true;
}

} // namespace Game
