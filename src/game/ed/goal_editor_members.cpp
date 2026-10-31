// The goal editor component's script member functions (Ed::CGoalEditorComponent, the table at 0x00684270): see
// goal_editor.h. From the disassembly of 0x004E8490-0x004E8F45, 0x004EA810, 0x004EBFF0 and 0x004EC1D0-0x004EC212
// (none of them Ghidra functions).

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/ed/goal_editor.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/ed/goal_editor_util.h"
#include "game/sys/crt.h"

#pragma warning(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace Ed {

namespace {

constexpr uint32_t kParkEditorGameplayLevel = 0xB664035D;

template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}

const char* Empty() { return ""; } // ""

} // namespace

// 0x004E8490
bool CGoalEditorComponent::EditedGoalAddGap(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int gap = 0;
        params->GetInteger(0x3AC9AB5E, &gap, 0);
        goal->AddGap(static_cast<uint8_t>(gap));
    }
    return true;
}

// 0x004E84E0
bool CGoalEditorComponent::EditedGoalRemoveGap(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int gap = 0;
        params->GetInteger(0x3AC9AB5E, &gap, 0);
        goal->RemoveGap(static_cast<uint8_t>(gap));
    }
    return true;
}

// 0x004E8530
bool CGoalEditorComponent::EditedGoalGotGap(Script::CStruct* params, Script::CScript* /*script*/)
{
    CEditedGoal* goal = GetGoalFromParams(params);
    if (!goal)
        return false;
    int gap = 0;
    params->GetInteger(0x3AC9AB5E, &gap, 0);
    for (int i = 0; i < goal->m_numGaps; ++i) {
        if (goal->m_gaps[i] == static_cast<uint8_t>(gap))
            return true;
    }
    return false;
}

// 0x004E8590
bool CGoalEditorComponent::AddKeyComboSet(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int set = 0, spin = 0, taps = 0;
        params->GetInteger(0xEB7D6498, &set, 0);
        params->GetInteger(0xEDF5DB70, &spin, 0);
        params->GetInteger(0xA4BEE6A1, &taps, 0);
        goal->AddKeyComboSet(set, params->ContainsFlag(0x687E2C25), spin, taps);
    }
    return true;
}

// 0x004E8630
bool CGoalEditorComponent::RemoveKeyComboSet(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        int set = 0;
        params->GetInteger(0xEB7D6498, &set, 0);
        goal->RemoveKeyComboSet(set);
    }
    return true;
}

// 0x004E8680
bool CGoalEditorComponent::GetKeyComboSet(Script::CStruct* params, Script::CScript* script)
{
    CEditedGoal* goal = GetGoalFromParams(params);
    if (!goal)
        return true;
    int set = 0;
    params->GetInteger(0xEB7D6498, &set, 0);
    KeyComboSet* entry = goal->FindKeyComboSet(set);
    if (!entry)
        return false;
    script->m_params->AddInteger(0xEDF5DB70, entry->spin * 180);
    script->m_params->AddInteger(0xA4BEE6A1, entry->numTaps);
    if (entry->requirePerfect)
        script->m_params->AddChecksum(0u, 0x687E2C25u);
    return true;
}

// 0x004E8720
bool CGoalEditorComponent::RemoveGoalSpecificFlag(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params)) {
        uint32_t flag = 0;
        params->GetChecksum(0u, &flag, 0);
        if (goal->m_type == kGoalTypeComboTricks && flag == 0xDF47B144)
            goal->m_singleCombo = false;
    }
    return true;
}

// 0x004E8780
bool CGoalEditorComponent::SetGoalSpecificParams(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->ReadSpecificParams(params);
    return true;
}

// 0x004E87A0
bool CGoalEditorComponent::FlagGoalAsWon(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->m_won = true;
    return true;
}

// 0x004E87C0
bool CGoalEditorComponent::FindUnfinishedGoal(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t type = 0;
    params->GetChecksum("type", &type, 0);
    mp_current = nullptr;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_type == type && goal.m_numPoints < goal.m_minPoints)
            mp_current = &goal; // (the last one)
    }
    return mp_current != nullptr;
}

// 0x004E8900
bool CGoalEditorComponent::GoalExists(Script::CStruct* params, Script::CScript* /*script*/)
{
    return GetGoalFromParams(params) != nullptr;
}

// 0x004E8920
bool CGoalEditorComponent::RemovedCreatedGoal(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        RemoveGoal(goal);
    return true;
}

// 0x004E8950
bool CGoalEditorComponent::GoalHasAllPositionsSet(Script::CStruct* params, Script::CScript* /*script*/)
{
    CEditedGoal* goal = GetGoalFromParams(params);
    return goal && goal->m_numPoints >= goal->m_minPoints;
}

// 0x004E8980
bool CGoalEditorComponent::SetCurrentEditorGoal(Script::CStruct* params, Script::CScript* /*script*/)
{
    mp_current = GetGoalFromParams(params);
    return true;
}

// 0x004E89A0
bool CGoalEditorComponent::SetEditorGoalType(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t type = 0;
    params->GetChecksum("type", &type, 0);
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->SetType(type);
    return true;
}

// 0x004E89F0
bool CGoalEditorComponent::SetEditorGoalDescription(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* text = Empty();
    params->GetString("text", &text, 0);
    if (CEditedGoal* goal = GetGoalFromParams(params))
        std::strcpy(goal->m_description, text);
    return true;
}

// 0x004E8A40
bool CGoalEditorComponent::SetEditorGoalWinMessage(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* text = Empty();
    params->GetString("text", &text, 0);
    if (CEditedGoal* goal = GetGoalFromParams(params))
        std::strcpy(goal->m_winMessage, text);
    return true;
}

// 0x004E8AA0
bool CGoalEditorComponent::GetNumEditedGoals(Script::CStruct* params, Script::CScript* script)
{
    uint32_t level = 0;
    params->GetChecksum(0x651533EC, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    int count = CountCompleteGoals(level);
    if (!level && params->ContainsFlag(0xB99F6330))
        count -= CountCompleteGoals(kParkEditorLevel);
    script->m_params->AddInteger("NumGoals", count);
    return true;
}

// 0x004E8B30
bool CGoalEditorComponent::GetCurrentEditedGoalId(Script::CStruct* /*params*/, Script::CScript* script)
{
    script->m_params->RemoveComponent("goal_id");
    if (mp_current) {
        char* name = g_createdGoalName;
        Sprintf(name, "CreatedGoal%d", mp_current->m_goalIndex);
        script->m_params->AddChecksum("goal_id", Crc::GenerateCRCFromString(name));
    }
    return true;
}

// 0x004E8B90
bool CGoalEditorComponent::WriteEditedGoalNodePositions(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->WriteNodePositions();
    return true;
}

// 0x004E8BB0
bool CGoalEditorComponent::SetEditorGoalName(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* text = Empty();
    if (mp_current && params->GetString("Name", &text, 0)) {
        mp_current = GetGoalFromParams(params); // (none named: a null write, as the original)
        std::strcpy(mp_current->m_name, text);
    }
    return true;
}

// 0x004E8C20
bool CGoalEditorComponent::SetEditorPedName(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* text = Empty();
    if (mp_current && params->GetString("Name", &text, 0)) {
        mp_current = GetGoalFromParams(params);
        std::strcpy(mp_current->m_pedName, text);
    }
    return true;
}

// 0x004E8C90
bool CGoalEditorComponent::MaxEditedGoalsReached(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CountUsedGoals() == 50)
        return true;
    uint32_t level = 0;
    params->GetChecksum(0x651533EC, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    return CountCompleteGoals(level) >= 10;
}

// 0x004E8CF0
bool CGoalEditorComponent::NukeAllGoals(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (params->ContainsFlag(0x98439808))
        ClearParkGoals();
    else
        ClearLevelGoals();
    return true;
}

// 0x004E8D30
bool CGoalEditorComponent::GetMaxGoalsPerLevel(Script::CStruct* /*params*/, Script::CScript* script)
{
    script->m_params->AddInteger(0x8BC2E0DB, 10);
    return true;
}

// 0x004E8D50
bool CGoalEditorComponent::SetGoalScore(Script::CStruct* params, Script::CScript* /*script*/)
{
    mp_current = GetGoalFromParams(params);
    if (mp_current) {
        int score = 0;
        if (params->GetInteger(0u, &score, 0))
            mp_current->m_score = score;
    }
    return true;
}

// 0x004E8DB0
bool CGoalEditorComponent::SetGoalTimeLimit(Script::CStruct* params, Script::CScript* /*script*/)
{
    mp_current = GetGoalFromParams(params);
    if (mp_current) {
        int seconds = 120;
        if (params->GetInteger(0u, &seconds, 0))
            mp_current->SetTimeLimit(seconds);
    }
    return true;
}

// 0x004E8E00
bool CGoalEditorComponent::SetGoalControlType(Script::CStruct* params, Script::CScript* /*script*/)
{
    mp_current = GetGoalFromParams(params);
    if (mp_current) {
        uint32_t control = kGoalTypeSkate;
        if (params->GetChecksum(0u, &control, 0))
            mp_current->m_controlType = control;
    }
    return true;
}

// 0x004E8E60
bool CGoalEditorComponent::RefreshGoalCursorPosition(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    if (!mp_current)
        return true;
    Mth::Vector pos = {0.0f, 0.0f, 0.0f, 0.0f};
    float height = 0.0f, angle = 0.0f;
    mp_current->GetCurrentPoint(&pos, &height, &angle);
    Script::CStruct cursor;
    cursor.Init();
    cursor.AddVector(0x7F261953, pos.x, pos.y + height, pos.z);
    cursor.AddFloat(0xFF7EBAF6, angle);
    Script::RunScript(0xDEA7FE56, &cursor, nullptr, false, nullptr);
    cursor.Clear();
    return true;
}

// 0x004EA810
bool CGoalEditorComponent::EditGoal(Script::CStruct* params, Script::CScript* /*script*/)
{
    mp_current = GetGoalFromParams(params);
    if (mp_current) {
        mp_current->CreateMarkers();
        MoveCursorToCurrentPoint();
        RefreshCursor();
    }
    return true;
}

// 0x004EBFF0
bool CGoalEditorComponent::GetCurrentEditedGoalInfo(Script::CStruct* /*params*/, Script::CScript* script)
{
    if (mp_current)
        mp_current->WriteIntoStructure(script->m_params);
    return true;
}

// 0x004EC1D0
bool CGoalEditorComponent::AddEditedGoalToGoalManager(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (CEditedGoal* goal = GetGoalFromParams(params))
        goal->AddToGoalManager(params->ContainsFlag(0xC6322A25));
    return true;
}

// 0x004EC200
bool CGoalEditorComponent::GetCreatedGoals(Script::CStruct* /*params*/, Script::CScript* script)
{
    WriteGoals(script->m_params);
    return true;
}

} // namespace Ed
