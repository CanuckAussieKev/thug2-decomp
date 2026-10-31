// Game::CGoalManager: its update, the timer on screen, unbeating, the view goals menu's list, the active goals'
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
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/struct.h"
#include "game/skate/trick_goals.h"

namespace Game {

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

GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }

Script::CStruct* NewStructure()
{
    Script::CStruct* made = Script::NewStruct();
    return made ? made->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
Script::CArray* NewArray()
{
    Script::CArray* made = Script::NewArray();
    return made ? made->Init() : nullptr;
}

// The active list's nodes, counted (as GetActiveGoal walks them).
int CountActive(Mem::ListNode* head)
{
    Mem::ListNode* node = head->next;
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
    int count = CountActive(&m_activeGoals.m_node);
    for (int i = 0; i < count; ++i) {
        CGoal* goal = GetActiveGoal(i);
        if (!Virtual<bool>(goal, CGoal::kVtSlot28))
            continue;
        int time = goal->GetTimeLeft();
        int minutes = 0, seconds = 0;
        if (time > 0) {
            minutes = time / 60000;
            seconds = (time - minutes * 60000) / 1000;
        }
        std::snprintf(text, sizeof(text), "%2i:%.2i", minutes, seconds); // "%2i:%.2i"
        break;
    }
    Front::ElementPtr element;
    Front::CScreenElementManager::Instance()->GetElement(&element, kTheTime, 0);
    if (element)
        static_cast<Front::CTextElement*>(element.mp_object)->SetText(text);
}

// 0x0055DEA0
bool CGoalManager::HasActiveGapGoal()
{
    int count = CountActive(&m_activeGoals.m_node);
    for (int i = 0; i < count; ++i) {
        CGoal* goal = GetActiveGoal(i);
        if (!Virtual<bool>(goal, CGoal::kVtIsActive))
            continue;
        Script::CArray* gaps = nullptr;
        if (goal->GetParams()->GetArray(kRequiredGaps, &gaps, 0))
            return true;
    }
    return false;
}

// 0x0055DF90
void CGoalManager::UnBeatAllGoals()
{
    GameNet::Manager* net = NetManager();
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CGoal::kVtUnBeat) && !net->InNetGame()) {
            --m_goalsBeaten;
            --m_goalPoints;
            --m_totalGoalPoints;
        }
    }
    if (!net->InNetGame())
        m_goalData->Clear();
    if (NumActiveGoals(false) == 0 && !static_cast<uint8_t>(CFuncs::CFunc_InMultiplayerGame(nullptr, nullptr)))
        Script::RunScript(kShowGoalPoints, nullptr, nullptr, false, nullptr);
}

// 0x0055E050
void CGoalManager::AddViewGoalsList()
{
    int count = m_goals.m_count; // (read once)
    Script::CArray* ordered = NewArray();
    Script::CArray* unordered = NewArray();
    ordered->SetSizeAndType(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_NAME);
    unordered->SetSizeAndType(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_NAME);
    // The goals with a place in the list first, then the others.
    int numOrdered = 0, numUnordered = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t key;
        CGoal* goal = m_goals.GetByIndex(i, &key);
        if (goal->GetParams()->ContainsComponentNamed(kViewGoalsOrder))
            ordered->SetElement(static_cast<uint32_t>(numOrdered++), goal->GetGoalId());
        else
            unordered->SetElement(static_cast<uint32_t>(numUnordered++), goal->GetGoalId());
    }
    // Sorted by their places: for each slot the goal whose place is the slot's number (a goal not in the table is
    // read through null, as the original).
    for (int slot = 0; slot < numOrdered; ++slot) {
        for (int k = slot; k < numOrdered; ++k) {
            uint32_t id = ordered->GetElement(static_cast<uint32_t>(k));
            int place;
            m_goals.Get(id)->GetParams()->GetInteger(kViewGoalsOrder, &place, 1);
            if (place - 1 != slot)
                continue;
            if (k != slot) {
                uint32_t other = ordered->GetElement(static_cast<uint32_t>(slot));
                int unused;
                m_goals.Get(other)->GetParams()->GetInteger(kViewGoalsOrder, &unused, 1);
                ordered->SetElement(static_cast<uint32_t>(slot), id);
                ordered->SetElement(static_cast<uint32_t>(k), other);
            }
            break;
        }
    }
    for (int i = 0; i < numUnordered; ++i)
        ordered->SetElement(static_cast<uint32_t>(numOrdered + i), unordered->GetElement(static_cast<uint32_t>(i)));
    // A menu item for each goal with view_goals_text (not pro goals until the cheat, not network goals): its id,
    // its text, its record if won.
    for (int i = 0; i < count; ++i) {
        CGoal* goal = m_goals.Get(ordered->GetElement(static_cast<uint32_t>(i)));
        Script::CStruct* params = goal->GetParams();
        const char* text = nullptr;
        goal->GetViewGoalsText(&text);
        if (!text)
            continue;
        if (!m_allUnlocked && params->ContainsFlag(kProGoal))
            continue;
        if (goal->IsNetGoal())
            continue;
        Script::CStruct* item = NewStructure();
        item->AddChecksum(kGoalIdChecksum, goal->GetGoalId());
        item->AddString(kText, text);
        if (Virtual<bool>(goal, CGoal::kVtHasWon)) {
            char record[128];
            int value = 0;
            if (goal->IsCompetition()) {
                params->GetInteger(kWinRecord, &value, 0);
                std::snprintf(record, sizeof(record), "%i", value); // "%i"
                const char* suffix = value == 1   ? "st place"  // "st place"
                                     : value == 2 ? "nd place"  // "nd place"
                                     : value == 3 ? "rd place"  // "rd place"
                                                  : "";
                std::size_t length = std::strlen(record);
                std::snprintf(record + length, sizeof(record) - length, "%s", suffix);
                item->AddString(kRecordText, record);
            } else {
                uint32_t type;
                if (params->GetChecksum(kRecordType, &type, 0)) {
                    if (type == kScore) {
                        params->GetInteger(kWinRecord, &value, 0);
                        std::snprintf(record, sizeof(record), "%s", Str::FormatWithSeparators(value)); // "%s"
                        item->AddString(kRecordText, record);
                    } else if (type == kTime) {
                        int ms = 0;
                        params->GetInteger(kWinRecord, &ms, 0);
                        int totalSeconds = ms / 1000;
                        int minutes = totalSeconds / 60;
                        std::snprintf(record, sizeof(record), "%i:%02i.%02i", minutes, totalSeconds - minutes * 60,
                                      (ms % 1000) / 10); // "%i:%02i.%02i"
                        item->AddString(kRecordText, record);
                    }
                } else {
                    item->AddChecksum(0u, kNoRecord);
                }
            }
        }
        Script::RunScript(kViewGoalsMenuAddItem, item, nullptr, false, nullptr);
        DeleteStructure(item);
    }
    Script::CleanUpArray(ordered);
    Script::CleanUpArray(unordered);
    for (Script::CArray* array : {ordered, unordered}) {
        if (array) {
            array->Clear();
            Script::DeleteArray(array);
        }
    }
}

// 0x0055E790
void CGoalManager::TrickLanded(Script::CStruct* trick)
{
    int count = CountActive(&m_activeGoals.m_node);
    for (int i = 0; i < count; ++i) {
        CGoal* goal = GetActiveGoal(i);
        if (goal->GetParams()->ContainsFlag(kTrickBeat))
            reinterpret_cast<CTrickBeatGoal*>(goal)->TrickLanded(trick);
    }
}

// 0x0055E980
CGoal* CGoalManager::GetActiveNetGoal()
{
    int count = CountActive(&m_activeGoals.m_node);
    for (int i = 0; i < count; ++i) {
        CGoal* goal = GetActiveGoal(i);
        if (goal->IsNetGoal())
            return goal;
    }
    return nullptr;
}

// 0x0055EA40
void CGoalManager::Update()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CGoal::kVtIsActive))
            Virtual<bool>(goal, CGoal::kVtUpdate);
        if (goal->State02()) {
            m_goals.Remove(goal->GetGoalId());
            Virtual<CGoal*>(goal, CGoal::kVtDestroy, static_cast<uint8_t>(1));
        }
    }
    UpdateTimeDisplay();
}

// 0x0055E860: active goals (count_all: network goals too, and those not active).
bool __cdecl CFunc_GoalManager_HasActiveGoals(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    bool all = params->ContainsFlag(kCountAll);
    return manager->NumActiveGoals(all) > 0;
}

// 0x0055E8A0
bool __cdecl CFunc_GoalManager_UnBeatAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->UnBeatAllGoals();
    return true;
}

// 0x0055E8C0
bool __cdecl CFunc_GoalManager_AddViewGoalsList(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->AddViewGoalsList();
    return true;
}

// 0x0055E8E0: an active goal is expired (the count asked again after each).
bool __cdecl CFunc_GoalManager_GoalShouldExpire(Script::CStruct*, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    if (!manager)
        return false;
    for (int i = 0; i < manager->NumActiveGoals(true); ++i) {
        if (Virtual<bool>(manager->GetActiveGoal(i), CGoal::kVtIsExpired))
            return true;
    }
    return false;
}

// 0x0055EAC0
bool __cdecl CFunc_GoalManager_UpdateAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->Update();
    return true;
}

// 0x0055EAE0: the active network goal's time left (ms) as the script's time_left.
bool __cdecl CFunc_GoalManager_GetTimeLeftInNetGame(Script::CStruct*, Script::CScript* script)
{
    if (CGoal* goal = GetGoalManager()->GetActiveNetGoal())
        script->m_params->AddInteger("time_left", goal->GetTimeLeft()); // "time_left"
    return true;
}

// 0x0055EB20: the active network goal has no time limit (true without one).
bool __cdecl CFunc_GoalManager_GetUnlimitedTimeInNetGame(Script::CStruct*, Script::CScript*)
{
    CGoal* goal = GetGoalManager()->GetActiveNetGoal();
    return goal ? goal->HasNoTimeLimit() : true;
}

} // namespace Game
