// The goal editor component's script member functions (ParkEditing::CObjectiveAuthoring, the table at 0x00684270): see
// goal_editor.h. From the disassembly of 0x004E8490-0x004E8F45, 0x004EA810, 0x004EBFF0 and 0x004EC1D0-0x004EC212
// (none of them Ghidra functions).

#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/park_editing/goal_editor.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/sys/crt.h"

#pragma warning(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace ParkEditing {

namespace {

constexpr uint32_t kParkEditorGameplayLevel = 0xB664035D;

template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}

const char* Vacant() { return ""; } // ""

} // namespace

// 0x004E8490
bool CObjectiveAuthoring::EditedGoalAddGap(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int gap = 0;
        params->FetchIntValue(0x3AC9AB5E, &gap, 0);
        goal->RegisterGapEntry(static_cast<uint8_t>(gap));
    }
    return true;
}

// 0x004E84E0
bool CObjectiveAuthoring::EditedGoalRemoveGap(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int gap = 0;
        params->FetchIntValue(0x3AC9AB5E, &gap, 0);
        goal->DropGapEntry(static_cast<uint8_t>(gap));
    }
    return true;
}

// 0x004E8530
bool CObjectiveAuthoring::EditedGoalGotGap(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    CEditedGoal* goal = GetGoalFromParams(params);
    if (!goal)
        return false;
    int gap = 0;
    params->FetchIntValue(0x3AC9AB5E, &gap, 0);
    for (int i = 0; i < goal->m_numGaps; ++i) {
        if (goal->m_gaps[i] == static_cast<uint8_t>(gap))
            return true;
    }
    return false;
}

// 0x004E8590
bool CObjectiveAuthoring::AddKeyComboSet(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int set = 0, spin = 0, taps = 0;
        params->FetchIntValue(0xEB7D6498, &set, 0);
        params->FetchIntValue(0xEDF5DB70, &spin, 0);
        params->FetchIntValue(0xA4BEE6A1, &taps, 0);
        goal->AddKeyComboSet(set, params->HasBareFlag(0x687E2C25), spin, taps);
    }
    return true;
}

// 0x004E8630
bool CObjectiveAuthoring::RemoveKeyComboSet(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int set = 0;
        params->FetchIntValue(0xEB7D6498, &set, 0);
        goal->RemoveKeyComboSet(set);
    }
    return true;
}

// 0x004E8680
bool CObjectiveAuthoring::GetKeyComboSet(Scripts::CStruct* params, Scripts::CScript* script)
{
    CEditedGoal* goal = GetGoalFromParams(params);
    if (!goal)
        return true;
    int set = 0;
    params->FetchIntValue(0xEB7D6498, &set, 0);
    KeyComboSet* entry = goal->FindKeyComboSet(set);
    if (!entry)
        return false;
    script->m_args->PutInt(0xEDF5DB70, entry->spin * 180);
    script->m_args->PutInt(0xA4BEE6A1, entry->tapTotal);
    if (entry->mustBeFlawless)
        script->m_args->PutNameKey(0u, 0x687E2C25u);
    return true;
}

// 0x004E8720
bool CObjectiveAuthoring::RemoveGoalSpecificFlag(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        uint32_t flag = 0;
        params->ReadHashValue(0u, &flag, 0);
        if (goal->m_kindTag == kGoalTypeComboTricks && flag == 0xDF47B144)
            goal->m_singleCombo = false;
    }
    return true;
}

// 0x004E8780
bool CObjectiveAuthoring::SetGoalSpecificParams(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->ReadSpecificParams(params);
    return true;
}

// 0x004E87A0
bool CObjectiveAuthoring::FlagGoalAsWon(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->m_won = true;
    return true;
}

// 0x004E87C0
bool CObjectiveAuthoring::FindUnfinishedGoal(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t type = 0;
    params->ReadHashValue("type", &type, 0);
    m_activeNode = nullptr;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_kindTag == type && goal.m_vertexTotal < goal.m_minPoints)
            m_activeNode = &goal; // (the last one)
    }
    return m_activeNode != nullptr;
}

// 0x004E8900
bool CObjectiveAuthoring::GoalExists(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    return GetGoalFromParams(params) != nullptr;
}

// 0x004E8920
bool CObjectiveAuthoring::RemovedCreatedGoal(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        DropObjective(goal);
    return true;
}

// 0x004E8950
bool CObjectiveAuthoring::GoalHasAllPositionsSet(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    CEditedGoal* goal = GetGoalFromParams(params);
    return goal && goal->m_vertexTotal >= goal->m_minPoints;
}

// 0x004E8980
bool CObjectiveAuthoring::SetCurrentEditorGoal(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    m_activeNode = GetGoalFromParams(params);
    return true;
}

// 0x004E89A0
bool CObjectiveAuthoring::SetEditorGoalType(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t type = 0;
    params->ReadHashValue("type", &type, 0);
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->ChangeSpecies(type);
    return true;
}

// 0x004E89F0
bool CObjectiveAuthoring::SetEditorGoalDescription(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    const char* text = Vacant();
    params->FetchText("text", &text, 0);
    if (CEditedGoal* goal = GetGoalFromParams(params))
        std::strcpy(goal->m_summaryText, text);
    return true;
}

// 0x004E8A40
bool CObjectiveAuthoring::SetEditorGoalWinMessage(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    const char* text = Vacant();
    params->FetchText("text", &text, 0);
    if (CEditedGoal* goal = GetGoalFromParams(params))
        std::strcpy(goal->m_winMessage, text);
    return true;
}

// 0x004E8AA0
bool CObjectiveAuthoring::GetNumEditedGoals(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t level = 0;
    params->ReadHashValue(0x651533EC, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    int count = CountCompleteGoals(level);
    if (!level && params->HasBareFlag(0xB99F6330))
        count -= CountCompleteGoals(kParkEditorLevel);
    script->m_args->PutInt("NumGoals", count);
    return true;
}

// 0x004E8B30
bool CObjectiveAuthoring::GetCurrentEditedGoalId(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->EraseModule("goal_id");
    if (m_activeNode) {
        char* name = g_createdGoalName;
        Sprintf(name, "CreatedGoal%d", m_activeNode->m_levelGoalNumber);
        script->m_args->PutNameKey("goal_id", Checksums::HashFromText(name));
    }
    return true;
}

// 0x004E8B90
bool CObjectiveAuthoring::WriteEditedGoalNodePositions(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->EmitNodeCoords();
    return true;
}

// 0x004E8BB0
bool CObjectiveAuthoring::SetEditorGoalName(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    const char* text = Vacant();
    if (m_activeNode && params->FetchText("Name", &text, 0)) {
        m_activeNode = GetGoalFromParams(params); // (none named: a null write, as the original)
        std::strcpy(m_activeNode->m_label, text);
    }
    return true;
}

// 0x004E8C20
bool CObjectiveAuthoring::SetEditorPedName(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    const char* text = Vacant();
    if (m_activeNode && params->FetchText("Name", &text, 0)) {
        m_activeNode = GetGoalFromParams(params);
        std::strcpy(m_activeNode->m_pedName, text);
    }
    return true;
}

// 0x004E8C90
bool CObjectiveAuthoring::MaxEditedGoalsReached(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CountUsedGoals() == 50)
        return true;
    uint32_t level = 0;
    params->ReadHashValue(0x651533EC, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    return CountCompleteGoals(level) >= 10;
}

// 0x004E8CF0
bool CObjectiveAuthoring::NukeAllGoals(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (params->HasBareFlag(0x98439808))
        ClearParkGoals();
    else
        ClearLevelGoals();
    return true;
}

// 0x004E8D30
bool CObjectiveAuthoring::GetMaxGoalsPerLevel(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->PutInt(0x8BC2E0DB, 10);
    return true;
}

// 0x004E8D50
bool CObjectiveAuthoring::SetGoalScore(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    m_activeNode = GetGoalFromParams(params);
    if (m_activeNode) {
        int score = 0;
        if (params->FetchIntValue(0u, &score, 0))
            m_activeNode->m_pointsHeld = score;
    }
    return true;
}

// 0x004E8DB0
bool CObjectiveAuthoring::SetGoalTimeLimit(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    m_activeNode = GetGoalFromParams(params);
    if (m_activeNode) {
        int seconds = 120;
        if (params->FetchIntValue(0u, &seconds, 0))
            m_activeNode->SetCountdownSeconds(seconds);
    }
    return true;
}

// 0x004E8E00
bool CObjectiveAuthoring::SetGoalControlType(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    m_activeNode = GetGoalFromParams(params);
    if (m_activeNode) {
        uint32_t control = kGoalTypeSkate;
        if (params->ReadHashValue(0u, &control, 0))
            m_activeNode->m_controlType = control;
    }
    return true;
}

// 0x004E8E60
bool CObjectiveAuthoring::RefreshGoalCursorPosition(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    if (!m_activeNode)
        return true;
    Maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 0.0f};
    float height = 0.0f, angle = 0.0f;
    m_activeNode->GetCurrentPoint(&pos, &height, &angle);
    Scripts::CStruct cursor;
    cursor.PrepareAll();
    cursor.PutVec3(0x7F261953, pos.x, pos.y + height, pos.z);
    cursor.PushFloat(0xFF7EBAF6, angle);
    Scripts::LaunchScript(0xDEA7FE56, &cursor, nullptr, false, nullptr);
    cursor.Clear();
    return true;
}

// 0x004EA810
bool CObjectiveAuthoring::EditGoal(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    m_activeNode = GetGoalFromParams(params);
    if (m_activeNode) {
        m_activeNode->CreateMarkers();
        MoveCursorToCurrentPoint();
        RefreshCursor();
    }
    return true;
}

// 0x004EBFF0
bool CObjectiveAuthoring::GetCurrentEditedGoalInfo(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    if (m_activeNode)
        m_activeNode->ExportToStruct(script->m_args);
    return true;
}

// 0x004EC1D0
bool CObjectiveAuthoring::AddEditedGoalToGoalManager(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->AddToGoalManager(params->HasBareFlag(0xC6322A25));
    return true;
}

// 0x004EC200
bool CObjectiveAuthoring::GetCreatedGoals(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    WriteGoals(script->m_args);
    return true;
}

} // namespace Ed
