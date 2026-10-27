// An edited goal to and from script structures, and its points dropped to the ground (Ed::CEditedGoal): see
// goal_editor.h. From the disassembly of 0x004E93D0-0x004EB7E2.

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/ed/goal_editor.h"
#include "game/ed/goal_editor_util.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/nx/feeler.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace Ed {

namespace {

constexpr float kDropHeight = 10000.0f; // 0x0064B308
constexpr uint32_t kStructureArray = 10, kChecksumArray = 13, kIntegerArray = 1;

template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x006234FE)(out, format, args...);
}

char* CreatedGoalName() { return &GameVariable<char>(0x007CCC40); } // the shared CreatedGoal<n> buffer

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
Script::CArray* NewArrayOf(uint32_t size, uint32_t type)
{
    Script::CArray* array = Script::NewArray();
    array = array ? array->Init() : nullptr;
    array->SetSizeAndType(size, static_cast<Script::ESymbolType>(type));
    return array;
}
Script::CStruct* StructureElement(Script::CArray* array, uint32_t index)
{
    return reinterpret_cast<Script::CStruct*>(array->GetElement(index));
}

} // namespace

// 0x004E93D0
void CEditedGoal::DropPointToGround(int index)
{
    Mth::Vector& pos = m_points[index].pos;
    Mth::Vector start = {pos.x, pos.y + kDropHeight, pos.z, pos.w};
    Mth::Vector end = {pos.x, pos.y - kDropHeight, pos.z, pos.w};
    Nx::CFeeler feeler;
    feeler.Construct();
    feeler.SetStart(&start);
    feeler.SetEnd(&end);
    if (!feeler.GetCollision(true, false)) {
        // Missed: again from higher up.
        start.y = start.y + kDropHeight;
        feeler.SetStart(&start);
        feeler.SetEnd(&end);
        if (!feeler.GetCollision(true, false))
            return;
    }
    pos = feeler.m_point;
}

// 0x004E9590
void CEditedGoal::PlacePointsOnGround()
{
    char* name = &GameVariable<char>(0x007CCCC0);
    for (int i = 0; i < m_numPoints; ++i) {
        DropPointToGround(i);
        Script::CStruct params;
        params.Init();
        uint32_t node = 0;
        if (i == 0) {
            Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
            node = Crc::GenerateCRCFromString(name);
        } else if (i > 1) {
            Sprintf(name, "TRG_EditedGoal%d_Object_%d", m_goalIndex, i - 2);
            node = Crc::GenerateCRCFromString(name);
        }
        params.AddChecksum("Name", node);
        const EditedGoalPoint& point = m_points[i];
        params.AddVector("Pos", point.pos.x, point.height + point.pos.y, point.pos.z);
        Script::RunScript(0x79582240, &params, nullptr, false, nullptr);
        params.Clear();
    }
}

// 0x004EB550
bool CEditedGoal::PlacePointAndStepBack(const Mth::Vector& pos, float height, float angle)
{
    if (m_currentPoint == 0 || !CanPlacePoint(pos, height))
        return false;
    m_points[m_currentPoint].pos = pos;
    m_points[m_currentPoint].height = height;
    m_points[m_currentPoint].angle = angle;
    CreateMarker(m_currentPoint);
    if (m_currentPoint == m_numPoints)
        ++m_numPoints;
    --m_currentPoint;
    RunMarkerObjectScript(m_currentPoint);
    if (m_currentPoint == 0) {
        Script::CStruct* params = Script::NewStruct();
        params = params ? params->Init() : nullptr;
        params->AddChecksum(0u, 0x7F1538E0u);
        Script::RunScript(0xD066A889, params, nullptr, false, nullptr);
        if (params) {
            params->Clear();
            Script::DeleteStruct(params);
        }
    }
    return true;
}

// 0x004E96D0
void CEditedGoal::ReadFromStructure(Script::CStruct* params)
{
    InitEditedGoal(reinterpret_cast<uint8_t*>(this));
    m_used = true;
    m_won = false;
    uint32_t level = 0;
    params->GetChecksum(0x651533EC, &level, 0);
    if (level == 0xB664035D) // (Load_Sk5Ed_gameplay counts as the park editor's)
        level = kParkEditorLevel;
    m_level = level;
    params->GetInteger(0x60CE75AE, &m_goalIndex, 0);
    params->GetChecksum(0x6D11ED74, &m_type, 0);
    SetType(m_type);
    params->GetInteger(0xCD66C8AE, &m_score, 0);
    params->GetInteger(0xC51BF6E3, &m_timeLimit, 0);
    params->GetChecksum(0x81CFF663, &m_controlType, 0);
    const char* empty = reinterpret_cast<const char*>(0x006469B4); // ""
    const char* text = empty;
    params->GetString(0x4BC5229D, &text, 0);
    std::strcpy(m_name, text);
    text = empty;
    params->GetString(0x0C5D7E6B, &text, 0);
    std::strcpy(m_description, text);
    text = empty;
    params->GetString(0x5DE57284, &text, 0);
    std::strcpy(m_winMessage, text);
    text = empty;
    params->GetString(0xB9426608, &text, 0);
    std::strcpy(m_pedName, text);
    Script::CArray* points = nullptr;
    if (params->GetArray(0x32DF12E0, &points, 0)) {
        m_numPoints = static_cast<uint8_t>(points->m_size);
        for (uint32_t i = 0; i < m_numPoints; ++i) {
            Script::CStruct* point = StructureElement(points, i);
            point->GetVector(0x7F261953, reinterpret_cast<Script::VectorValue*>(&m_points[i].pos), 0);
            point->GetFloat(0x0AB21AF0, &m_points[i].height, 0);
            point->GetFloat(0xFF7EBAF6, &m_points[i].angle, 0);
        }
    }
    ReadSpecificParams(params);
}

// 0x004E9BC0
void CEditedGoal::WriteKeyComboSets(Script::CStruct* out)
{
    if (m_numKeyComboSets == 0)
        return;
    Script::CArray* sets = NewArrayOf(static_cast<uint32_t>(m_numKeyComboSets), kStructureArray);
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        const KeyComboSet& set = m_keyComboSets[i];
        Script::CStruct* entry = NewStructure();
        entry->AddInteger(0xEB7D6498, set.set);
        if (set.requirePerfect)
            entry->AddChecksum(0u, 0x687E2C25u);
        entry->AddInteger(0xEDF5DB70, set.spin * 180);
        entry->AddInteger(0xA4BEE6A1, set.numTaps);
        sets->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
    }
    out->AddArrayPointer(0x0490F171, sets);
}

// 0x004E98F0
void CEditedGoal::WriteGoalKeyCombos(Script::CStruct* out)
{
    if (m_numKeyComboSets == 0) {
        Dbg::Printf("!!!!!!!! Warning! No combo sets specified, using emergency_key_combos\n");
        out->AddChecksum(0x7BE1E689, 0x6D641BCB);
        return;
    }
    Script::CArray* allSets = Script::GetArray(0xDF1451EB);
    // How many key combos the sets hold (a set without its array: a null read, as the original).
    uint32_t total = 0;
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        Script::CArray* combos = nullptr;
        StructureElement(allSets, m_keyComboSets[i].set)->GetArray(0x79704516, &combos, 0);
        total += combos->m_size;
    }
    Script::CArray* result = NewArrayOf(total, kStructureArray);
    uint32_t index = 0;
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        const KeyComboSet& set = m_keyComboSets[i];
        Script::CArray* combos = nullptr;
        StructureElement(allSets, set.set)->GetArray(0x79704516, &combos, 0);
        for (uint32_t j = 0; j < combos->m_size; ++j) {
            Script::CStruct* entry = NewStructure();
            if (combos->m_type == kChecksumArray)
                entry->AddChecksum(0xACFDB27A, combos->GetElement(j));
            else
                entry->AppendStructureIfAny(StructureElement(combos, j));
            if (set.numTaps)
                entry->AddInteger(0xA4BEE6A1, set.numTaps);
            if (set.requirePerfect)
                entry->AddChecksum(0u, 0x687E2C25u);
            if (m_spin)
                entry->AddInteger(0x8C8ABD19, m_spin);
            result->SetElement(index++, reinterpret_cast<uint32_t>(entry));
        }
    }
    out->AddArrayPointer(m_type == kGoalTypeKeyCombo ? 0x548B4A38 : 0x7BE1E689, result);
}

// 0x004E9D60
void CEditedGoal::WriteTrickSettings(Script::CStruct* out, int forGoalManager)
{
    out->AddInteger(0x01181B29, m_accelerationInterval);
    out->AddFloat(0x076F65C8, m_accelerationPercent);
    out->AddInteger(0xDED8540A, m_trickTime);
    out->AddInteger(0xA6D96603, m_timeToStopAddingTricks);
    out->AddInteger(0x89473DB7, m_maxTricks);
    if (forGoalManager == 1) {
        WriteGoalKeyCombos(out);
        return;
    }
    out->AddInteger(0xEDF5DB70, m_spin);
    WriteKeyComboSets(out);
}

// 0x004E9E00
void CEditedGoal::WriteSpecificParams(Script::CStruct* out, int forGoalManager)
{
    char text[0x80];
    switch (m_type) {
    case kGoalTypeRace:
        if (forGoalManager == 1) {
            if (m_racePerCheckpointTime > 0)
                out->AddInteger(0xA4038EC9, m_racePerCheckpointTime);
        } else {
            out->AddInteger(0xD9426372, m_racePerCheckpointTime);
        }
        out->AddInteger(0x1DAAE95B, m_raceTotalLaps);
        break;
    case kGoalTypeComboTricks:
        out->AddChecksum(0u, static_cast<uint32_t>(kGoalTypeCombo));
        out->AddInteger(0x67DD90CA, m_comboSize);
        if (m_singleCombo) {
            out->AddChecksum(0u, 0xDF47B144u);
            m_maxTricks = m_comboSize;
        }
        WriteTrickSettings(out, forGoalManager);
        break;
    case kGoalTypeGap:
        out->AddString(0x730844A3, m_requiredTrick);
        if (forGoalManager == 1) {
            Script::CArray* flags = NewArrayOf(m_numGaps, kChecksumArray);
            for (int i = 0; i < m_numGaps; ++i) {
                Sprintf(text, "got_%d", i + 1);
                flags->SetElement(static_cast<uint32_t>(i), Crc::GenerateCRCFromString(text));
            }
            out->AddArrayPointer(0xCC3C4CC4, flags);
            Script::CArray* gaps = NewArrayOf(m_numGaps, kIntegerArray);
            for (int i = 0; i < m_numGaps; ++i)
                gaps->SetElement(static_cast<uint32_t>(i), m_gaps[i]);
            out->AddArrayPointer(0x52D4489E, gaps);
        } else if (m_numGaps) {
            Script::CArray* gaps = NewArrayOf(m_numGaps, kIntegerArray);
            for (int i = 0; i < m_numGaps; ++i)
                gaps->SetElement(static_cast<uint32_t>(i), m_gaps[i]);
            out->AddArrayPointer(0xD76C173E, gaps);
        }
        break;
    case kGoalTypeScore:
    case kGoalTypeHighCombo:
        out->AddInteger(0xCD66C8AE, m_score);
        if (forGoalManager == 1) {
            const char* format = Script::GetString(m_type == kGoalTypeHighCombo ? 0x3693BFE4 : 0xD6EFB1B5);
            Sprintf(text, format, m_score);
            out->AddString(0xDA441D9A, text);
        }
        break;
    case kGoalTypeTrickAttack:
        WriteTrickSettings(out, forGoalManager);
        break;
    case kGoalTypeKeyCombo:
        if (forGoalManager == 1)
            WriteGoalKeyCombos(out);
        else
            WriteKeyComboSets(out);
        break;
    case kGoalTypeTricktris:
        out->AddChecksum(0u, static_cast<uint32_t>(kGoalTypeTricktris));
        out->AddInteger(0x45A375C9, m_tricktrisBlockSize);
        out->AddInteger(0xBFD3A831, m_tricktrisTotalToWin);
        m_maxTricks = m_tricktrisBlockSize;
        WriteTrickSettings(out, forGoalManager);
        break;
    default:
        break;
    }
}

// 0x004EA850
void CEditedGoal::WriteGoalManagerParams(Script::CStruct* out)
{
    char name[0x64];
    out->AppendStructureIfAny(GoalTypeStructure(m_type));
    out->AppendStructureIfAny(GoalTypeDefaults(m_type));
    name[0] = '\0';
    Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
    out->AddChecksum(0x02D7E03D, Crc::GenerateCRCFromString(name));
    name[0] = '\0';
    Sprintf(name, "TRG_EditedGoal%d_Restart", m_goalIndex);
    out->AddChecksum(0x3494F170, Crc::GenerateCRCFromString(name));
    out->AddChecksum(0xA302D7CD, 1u);
    out->AddChecksum(0u, 0xDBEB46E2u);
    switch (m_type) {
    case kGoalTypeSkate: {
        static const uint32_t kLetters[5] = {0xBA54BD5C, 0x150A97C2, 0x84CA8B0A, 0xB091B445, 0x008085F0};
        for (int i = 0; i < 5; ++i) {
            name[0] = '\0';
            Sprintf(name, "TRG_EditedGoal%d_Object_%d", m_goalIndex, i);
            out->AddChecksum(kLetters[i], Crc::GenerateCRCFromString(name));
        }
        break;
    }
    case kGoalTypeCombo: {
        static const char* const kLetters[5] = {"C", "O", "M", "B", "O"};
        Script::CArray* letters = NewArrayOf(5, kStructureArray);
        for (int i = 0; i < 5; ++i) {
            Script::CStruct* letter = NewStructure();
            name[0] = '\0';
            Sprintf(name, "TRG_EditedGoal%d_Object_%d", m_goalIndex, i);
            letter->AddChecksum(0x99F6CCBB, Crc::GenerateCRCFromString(name));
            letter->AddString(0xC4745838, kLetters[i]);
            letters->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(letter));
        }
        out->AddArrayPointer(0xB3C7F1B2, letters);
        break;
    }
    case kGoalTypeRace: {
        int count = m_numPoints - 2;
        Script::CArray* waypoints = NewArrayOf(static_cast<uint32_t>(count), kStructureArray);
        Script::CArray* flags = NewArrayOf(static_cast<uint32_t>(count), kChecksumArray);
        char flag[0x64];
        for (int i = 0; i < count; ++i) {
            Script::CStruct* waypoint = NewStructure();
            waypoint->AddChecksum(0x40C698AF, EditedGoalNodeName(i + 2, m_goalIndex));
            Sprintf(flag, "got_%d", i + 1);
            flags->SetElement(static_cast<uint32_t>(i), Crc::GenerateCRCFromString(flag));
            waypoint->AddChecksum(0x2E0B1465, Crc::GenerateCRCFromString(flag));
            if (m_racePerCheckpointTime > 0)
                waypoint->AddInteger(0x906B67BA, m_racePerCheckpointTime);
            waypoint->AddInteger(0xC48391A5, 5);
            waypoints->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(waypoint));
        }
        out->AddArrayPointer(0xA77B1F07, waypoints);
        out->AddArrayPointer(0xCC3C4CC4, flags);
        break;
    }
    default:
        break;
    }
    WriteSpecificParams(out, 1);
    if (m_description[0])
        out->AddString(0x0C5D7E6B, m_description);
    if (m_winMessage[0]) {
        // The win message's dialog: a copy of the global template array with the text and the pro's node.
        Script::CArray* source = Script::GetArray(0x8070C110);
        Script::CArray* dialog = Script::NewArray();
        dialog = dialog ? dialog->Init() : nullptr;
        Script::CopyArray(dialog, source);
        StructureElement(dialog, 0)->AddString(0xA7AB3B6D, m_winMessage);
        name[0] = '\0';
        Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
        uint32_t pro = Crc::GenerateCRCFromString(name);
        StructureElement(dialog, 0)->AddChecksum(0x531E4D28, pro);
        out->AddArrayPointer(0x27CD32E1, dialog);
    }
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    out->AddString(0xBFECC45B, CreatedGoalName());
    if (m_name[0])
        out->AddString(0x4BC5229D, m_name);
    if (m_pedName[0])
        out->AddString(0x243B9C3B, m_pedName);
    out->AddInteger(0x906B67BA, m_timeLimit);
    out->AddChecksum(0x81CFF663, m_controlType);
    out->AddChecksum(0u, 0x981D3AD0u);
}

// 0x004EB260
void CEditedGoal::WriteIntoStructure(Script::CStruct* out)
{
    char name[0x64];
    out->AddChecksum(0u, 0x981D3AD0u);
    out->AddChecksum(0x651533EC, m_level);
    out->AddInteger(0x60CE75AE, m_goalIndex);
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    out->AddChecksum(0x9982E501, Crc::GenerateCRCFromString(CreatedGoalName()));
    out->AddInteger(0xC51BF6E3, m_timeLimit);
    out->AddInteger(0x14938611, m_won);
    out->AddChecksum(0x81CFF663, m_controlType);
    out->AddString(0x4BC5229D, m_name);
    out->AddString(0x0C5D7E6B, m_description);
    out->AddString(0x5DE57284, m_winMessage);
    out->AddString(0xB9426608, m_pedName);
    out->AddChecksum(0x6D11ED74, m_type);
    name[0] = '\0';
    Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
    out->AddChecksum(0x71FC348B, Crc::GenerateCRCFromString(name));
    WriteSpecificParams(out, 0);
    if (!m_numPoints)
        return;
    Script::CArray* points = NewArrayOf(m_numPoints, kStructureArray);
    for (uint32_t i = 0; i < m_numPoints; ++i) {
        const EditedGoalPoint& point = m_points[i];
        Script::CStruct* entry = NewStructure();
        entry->AddVector(0x7F261953, point.pos.x, point.pos.y, point.pos.z);
        entry->AddFloat(0x0AB21AF0, point.height);
        entry->AddFloat(0xFF7EBAF6, point.angle);
        points->SetElement(i, reinterpret_cast<uint32_t>(entry));
    }
    out->AddArrayPointer(0x32DF12E0, points);
}

// 0x004EB6B0
void CEditedGoal::AddToGoalManager(bool unwin)
{
    Script::CStruct* params = NewStructure();
    WriteGoalManagerParams(params);
    Game::CGoalManager* manager = Game::GetGoalManager();
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    manager->AddGoal(Crc::GenerateCRCFromString(CreatedGoalName()), params);
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    Game::CGoal* goal = manager->GetGoal(Crc::GenerateCRCFromString(CreatedGoalName()), 1);
    goal->SetHasSeen();
    if (unwin)
        m_won = false;
    if (m_won)
        Game::Virtual<void>(goal, 0x68); // MarkWon
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace Ed

REPLACE(0x004E93D0, &Ed::CEditedGoal::DropPointToGround);
REPLACE(0x004E9590, &Ed::CEditedGoal::PlacePointsOnGround);
REPLACE(0x004EB550, &Ed::CEditedGoal::PlacePointAndStepBack);
REPLACE(0x004E96D0, &Ed::CEditedGoal::ReadFromStructure);
REPLACE(0x004EB260, &Ed::CEditedGoal::WriteIntoStructure);
REPLACE(0x004E9BC0, &Ed::CEditedGoal::WriteKeyComboSets);
REPLACE(0x004E98F0, &Ed::CEditedGoal::WriteGoalKeyCombos);
REPLACE(0x004E9D60, &Ed::CEditedGoal::WriteTrickSettings);
REPLACE(0x004E9E00, &Ed::CEditedGoal::WriteSpecificParams);
REPLACE(0x004EA850, &Ed::CEditedGoal::WriteGoalManagerParams);
REPLACE(0x004EB6B0, &Ed::CEditedGoal::AddToGoalManager);
