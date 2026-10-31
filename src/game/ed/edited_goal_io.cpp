// An edited goal to and from script structures, and its points dropped to the ground (ParkEditing::CEditedGoal): see
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
#include "game/sys/crt.h"

#pragma warning(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace ParkEditing {

char g_createdGoalName[24];
char g_goalIoNameBuffer[100];

namespace {

constexpr float kDropHeight = 10000.0f; // 0x0064B308
constexpr uint32_t kStructureArray = 10, kChecksumArray = 13, kIntegerArray = 1;

template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}

char* CreatedGoalName() { return g_createdGoalName; } // the shared CreatedGoal<n> buffer

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
Scripts::CArray* NewArrayOf(uint32_t size, uint32_t type)
{
    Scripts::CArray* array = Scripts::NewArray();
    array = array ? array->PrepareAll() : nullptr;
    array->ConfigureSizeKind(size, static_cast<Scripts::EQbValueKind>(type));
    return array;
}
Scripts::CStruct* StructureElement(Scripts::CArray* array, uint32_t index)
{
    return reinterpret_cast<Scripts::CStruct*>(array->FetchPart(index));
}

} // namespace

// 0x004E93D0
void CEditedGoal::DropPointToGround(int index)
{
    Maths::Vec4& pos = m_pathVertices[index].pos;
    Maths::Vec4 start = {pos.x, pos.y + kDropHeight, pos.z, pos.w};
    Maths::Vec4 end = {pos.x, pos.y - kDropHeight, pos.z, pos.w};
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    feeler.ChangeOrigin(&start);
    feeler.ChangeFinish(&end);
    if (!feeler.GetHitShape(true, false)) {
        // Missed: again from higher up.
        start.y = start.y + kDropHeight;
        feeler.ChangeOrigin(&start);
        feeler.ChangeFinish(&end);
        if (!feeler.GetHitShape(true, false))
            return;
    }
    pos = feeler.m_contactPos;
}

// 0x004E9590
void CEditedGoal::PlacePointsOnGround()
{
    char* name = g_goalIoNameBuffer;
    for (int i = 0; i < m_vertexTotal; ++i) {
        DropPointToGround(i);
        Scripts::CStruct params;
        params.PrepareAll();
        uint32_t node = 0;
        if (i == 0) {
            Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
            node = Checksums::HashFromText(name);
        } else if (i > 1) {
            Sprintf(name, "TRG_EditedGoal%d_Object_%d", m_goalIndex, i - 2);
            node = Checksums::HashFromText(name);
        }
        params.PutNameKey("Name", node);
        const EditedGoalPoint& point = m_pathVertices[i];
        params.PutVec3("Pos", point.pos.x, point.height + point.pos.y, point.pos.z);
        Scripts::LaunchScript(0x79582240, &params, nullptr, false, nullptr);
        params.Clear();
    }
}

// 0x004EB550
bool CEditedGoal::PlacePointAndStepBack(const Maths::Vec4& pos, float height, float angle)
{
    if (m_currentPoint == 0 || !CanPlacePoint(pos, height))
        return false;
    m_pathVertices[m_currentPoint].pos = pos;
    m_pathVertices[m_currentPoint].height = height;
    m_pathVertices[m_currentPoint].angle = angle;
    CreateMarker(m_currentPoint);
    if (m_currentPoint == m_vertexTotal)
        ++m_vertexTotal;
    --m_currentPoint;
    RunMarkerObjectScript(m_currentPoint);
    if (m_currentPoint == 0) {
        Scripts::CStruct* params = Scripts::NewStruct();
        params = params ? params->PrepareAll() : nullptr;
        params->PutNameKey(0u, 0x7F1538E0u);
        Scripts::LaunchScript(0xD066A889, params, nullptr, false, nullptr);
        if (params) {
            params->Clear();
            Scripts::DeleteStruct(params);
        }
    }
    return true;
}

// 0x004E96D0
void CEditedGoal::DecodeFromStruct(Scripts::CStruct* params)
{
    InitEditedGoal(reinterpret_cast<uint8_t*>(this));
    m_takenCount = true;
    m_won = false;
    uint32_t level = 0;
    params->ReadHashValue(0x651533EC, &level, 0);
    if (level == 0xB664035D) // (Load_Sk5Ed_gameplay counts as the park editor's)
        level = kParkEditorLevel;
    m_level = level;
    params->FetchIntValue(0x60CE75AE, &m_goalIndex, 0);
    params->ReadHashValue(0x6D11ED74, &m_kindTag, 0);
    ChangeSpecies(m_kindTag);
    params->FetchIntValue(0xCD66C8AE, &m_pointsHeld, 0);
    params->FetchIntValue(0xC51BF6E3, &m_timeLimit, 0);
    params->ReadHashValue(0x81CFF663, &m_controlType, 0);
    const char* empty = ""; // ""
    const char* text = empty;
    params->FetchText(0x4BC5229D, &text, 0);
    std::strcpy(m_label, text);
    text = empty;
    params->FetchText(0x0C5D7E6B, &text, 0);
    std::strcpy(m_summaryText, text);
    text = empty;
    params->FetchText(0x5DE57284, &text, 0);
    std::strcpy(m_winMessage, text);
    text = empty;
    params->FetchText(0xB9426608, &text, 0);
    std::strcpy(m_pedName, text);
    Scripts::CArray* points = nullptr;
    if (params->FetchList(0x32DF12E0, &points, 0)) {
        m_vertexTotal = static_cast<uint8_t>(points->m_extent);
        for (uint32_t i = 0; i < m_vertexTotal; ++i) {
            Scripts::CStruct* point = StructureElement(points, i);
            point->FetchVec(0x7F261953, reinterpret_cast<Scripts::VectorValue*>(&m_pathVertices[i].pos), 0);
            point->FetchFloat(0x0AB21AF0, &m_pathVertices[i].height, 0);
            point->FetchFloat(0xFF7EBAF6, &m_pathVertices[i].angle, 0);
        }
    }
    ReadSpecificParams(params);
}

// 0x004E9BC0
void CEditedGoal::WriteKeyComboSets(Scripts::CStruct* out)
{
    if (m_numKeyComboSets == 0)
        return;
    Scripts::CArray* sets = NewArrayOf(static_cast<uint32_t>(m_numKeyComboSets), kStructureArray);
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        const KeyComboSet& set = m_keyComboSets[i];
        Scripts::CStruct* entry = NewStructure();
        entry->PutInt(0xEB7D6498, set.set);
        if (set.mustBeFlawless)
            entry->PutNameKey(0u, 0x687E2C25u);
        entry->PutInt(0xEDF5DB70, set.spin * 180);
        entry->PutInt(0xA4BEE6A1, set.tapTotal);
        sets->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
    }
    out->AppendArrayRef(0x0490F171, sets);
}

// 0x004E98F0
void CEditedGoal::WriteGoalKeyCombos(Scripts::CStruct* out)
{
    if (m_numKeyComboSets == 0) {
        DebugOut::Printf("!!!!!!!! Warning! No combo sets specified, using emergency_key_combos\n");
        out->PutNameKey(0x7BE1E689, 0x6D641BCB);
        return;
    }
    Scripts::CArray* allSets = Scripts::FetchList(0xDF1451EB);
    // How many key combos the sets hold (a set without its array: a null read, as the original).
    uint32_t total = 0;
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        Scripts::CArray* combos = nullptr;
        StructureElement(allSets, m_keyComboSets[i].set)->FetchList(0x79704516, &combos, 0);
        total += combos->m_extent;
    }
    Scripts::CArray* result = NewArrayOf(total, kStructureArray);
    uint32_t index = 0;
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        const KeyComboSet& set = m_keyComboSets[i];
        Scripts::CArray* combos = nullptr;
        StructureElement(allSets, set.set)->FetchList(0x79704516, &combos, 0);
        for (uint32_t j = 0; j < combos->m_extent; ++j) {
            Scripts::CStruct* entry = NewStructure();
            if (combos->m_kindTag == kChecksumArray)
                entry->PutNameKey(0xACFDB27A, combos->FetchPart(j));
            else
                entry->AppendStructureIfAny(StructureElement(combos, j));
            if (set.tapTotal)
                entry->PutInt(0xA4BEE6A1, set.tapTotal);
            if (set.mustBeFlawless)
                entry->PutNameKey(0u, 0x687E2C25u);
            if (m_twistRate)
                entry->PutInt(0x8C8ABD19, m_twistRate);
            result->SetElement(index++, reinterpret_cast<uint32_t>(entry));
        }
    }
    out->AppendArrayRef(m_kindTag == kGoalTypeKeyCombo ? 0x548B4A38 : 0x7BE1E689, result);
}

// 0x004E9D60
void CEditedGoal::WriteTrickSettings(Scripts::CStruct* out, int forGoalManager)
{
    out->PutInt(0x01181B29, m_accelerationInterval);
    out->PushFloat(0x076F65C8, m_accelerationPercent);
    out->PutInt(0xDED8540A, m_trickTime);
    out->PutInt(0xA6D96603, m_timeToStopAddingTricks);
    out->PutInt(0x89473DB7, m_maxTricks);
    if (forGoalManager == 1) {
        WriteGoalKeyCombos(out);
        return;
    }
    out->PutInt(0xEDF5DB70, m_twistRate);
    WriteKeyComboSets(out);
}

// 0x004E9E00
void CEditedGoal::WriteSpecificParams(Scripts::CStruct* out, int forGoalManager)
{
    char text[0x80];
    switch (m_kindTag) {
    case kGoalTypeRace:
        if (forGoalManager == 1) {
            if (m_racePerCheckpointTime > 0)
                out->PutInt(0xA4038EC9, m_racePerCheckpointTime);
        } else {
            out->PutInt(0xD9426372, m_racePerCheckpointTime);
        }
        out->PutInt(0x1DAAE95B, m_raceTotalLaps);
        break;
    case kGoalTypeComboTricks:
        out->PutNameKey(0u, static_cast<uint32_t>(kGoalTypeCombo));
        out->PutInt(0x67DD90CA, m_comboSize);
        if (m_singleCombo) {
            out->PutNameKey(0u, 0xDF47B144u);
            m_maxTricks = m_comboSize;
        }
        WriteTrickSettings(out, forGoalManager);
        break;
    case kGoalTypeGap:
        out->AppendWord(0x730844A3, m_requiredTrick);
        if (forGoalManager == 1) {
            Scripts::CArray* flags = NewArrayOf(m_numGaps, kChecksumArray);
            for (int i = 0; i < m_numGaps; ++i) {
                Sprintf(text, "got_%d", i + 1);
                flags->SetElement(static_cast<uint32_t>(i), Checksums::HashFromText(text));
            }
            out->AppendArrayRef(0xCC3C4CC4, flags);
            Scripts::CArray* gaps = NewArrayOf(m_numGaps, kIntegerArray);
            for (int i = 0; i < m_numGaps; ++i)
                gaps->SetElement(static_cast<uint32_t>(i), m_gaps[i]);
            out->AppendArrayRef(0x52D4489E, gaps);
        } else if (m_numGaps) {
            Scripts::CArray* gaps = NewArrayOf(m_numGaps, kIntegerArray);
            for (int i = 0; i < m_numGaps; ++i)
                gaps->SetElement(static_cast<uint32_t>(i), m_gaps[i]);
            out->AppendArrayRef(0xD76C173E, gaps);
        }
        break;
    case kGoalTypeScore:
    case kGoalTypeHighCombo:
        out->PutInt(0xCD66C8AE, m_pointsHeld);
        if (forGoalManager == 1) {
            const char* format = Scripts::FetchText(m_kindTag == kGoalTypeHighCombo ? 0x3693BFE4 : 0xD6EFB1B5);
            Sprintf(text, format, m_pointsHeld);
            out->AppendWord(0xDA441D9A, text);
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
        out->PutNameKey(0u, static_cast<uint32_t>(kGoalTypeTricktris));
        out->PutInt(0x45A375C9, m_tricktrisBlockSize);
        out->PutInt(0xBFD3A831, m_tricktrisTotalToWin);
        m_maxTricks = m_tricktrisBlockSize;
        WriteTrickSettings(out, forGoalManager);
        break;
    default:
        break;
    }
}

// 0x004EA850
void CEditedGoal::WriteGoalManagerParams(Scripts::CStruct* out)
{
    char name[0x64];
    out->AppendStructureIfAny(GoalTypeStructure(m_kindTag));
    out->AppendStructureIfAny(GoalTypeDefaults(m_kindTag));
    name[0] = '\0';
    Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
    out->PutNameKey(0x02D7E03D, Checksums::HashFromText(name));
    name[0] = '\0';
    Sprintf(name, "TRG_EditedGoal%d_Restart", m_goalIndex);
    out->PutNameKey(0x3494F170, Checksums::HashFromText(name));
    out->PutNameKey(0xA302D7CD, 1u);
    out->PutNameKey(0u, 0xDBEB46E2u);
    switch (m_kindTag) {
    case kGoalTypeSkate: {
        static const uint32_t kLetters[5] = {0xBA54BD5C, 0x150A97C2, 0x84CA8B0A, 0xB091B445, 0x008085F0};
        for (int i = 0; i < 5; ++i) {
            name[0] = '\0';
            Sprintf(name, "TRG_EditedGoal%d_Object_%d", m_goalIndex, i);
            out->PutNameKey(kLetters[i], Checksums::HashFromText(name));
        }
        break;
    }
    case kGoalTypeCombo: {
        static const char* const kLetters[5] = {"C", "O", "M", "B", "O"};
        Scripts::CArray* letters = NewArrayOf(5, kStructureArray);
        for (int i = 0; i < 5; ++i) {
            Scripts::CStruct* letter = NewStructure();
            name[0] = '\0';
            Sprintf(name, "TRG_EditedGoal%d_Object_%d", m_goalIndex, i);
            letter->PutNameKey(0x99F6CCBB, Checksums::HashFromText(name));
            letter->AppendWord(0xC4745838, kLetters[i]);
            letters->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(letter));
        }
        out->AppendArrayRef(0xB3C7F1B2, letters);
        break;
    }
    case kGoalTypeRace: {
        int count = m_vertexTotal - 2;
        Scripts::CArray* waypoints = NewArrayOf(static_cast<uint32_t>(count), kStructureArray);
        Scripts::CArray* flags = NewArrayOf(static_cast<uint32_t>(count), kChecksumArray);
        char flag[0x64];
        for (int i = 0; i < count; ++i) {
            Scripts::CStruct* waypoint = NewStructure();
            waypoint->PutNameKey(0x40C698AF, EditedGoalNodeName(i + 2, m_goalIndex));
            Sprintf(flag, "got_%d", i + 1);
            flags->SetElement(static_cast<uint32_t>(i), Checksums::HashFromText(flag));
            waypoint->PutNameKey(0x2E0B1465, Checksums::HashFromText(flag));
            if (m_racePerCheckpointTime > 0)
                waypoint->PutInt(0x906B67BA, m_racePerCheckpointTime);
            waypoint->PutInt(0xC48391A5, 5);
            waypoints->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(waypoint));
        }
        out->AppendArrayRef(0xA77B1F07, waypoints);
        out->AppendArrayRef(0xCC3C4CC4, flags);
        break;
    }
    default:
        break;
    }
    WriteSpecificParams(out, 1);
    if (m_summaryText[0])
        out->AppendWord(0x0C5D7E6B, m_summaryText);
    if (m_winMessage[0]) {
        // The win message's dialog: a copy of the global template array with the text and the pro's node.
        Scripts::CArray* source = Scripts::FetchList(0x8070C110);
        Scripts::CArray* dialog = Scripts::NewArray();
        dialog = dialog ? dialog->PrepareAll() : nullptr;
        Scripts::DuplicateArray(dialog, source);
        StructureElement(dialog, 0)->AppendWord(0xA7AB3B6D, m_winMessage);
        name[0] = '\0';
        Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
        uint32_t pro = Checksums::HashFromText(name);
        StructureElement(dialog, 0)->PutNameKey(0x531E4D28, pro);
        out->AppendArrayRef(0x27CD32E1, dialog);
    }
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    out->AppendWord(0xBFECC45B, CreatedGoalName());
    if (m_label[0])
        out->AppendWord(0x4BC5229D, m_label);
    if (m_pedName[0])
        out->AppendWord(0x243B9C3B, m_pedName);
    out->PutInt(0x906B67BA, m_timeLimit);
    out->PutNameKey(0x81CFF663, m_controlType);
    out->PutNameKey(0u, 0x981D3AD0u);
}

// 0x004EB260
void CEditedGoal::ExportToStruct(Scripts::CStruct* out)
{
    char name[0x64];
    out->PutNameKey(0u, 0x981D3AD0u);
    out->PutNameKey(0x651533EC, m_level);
    out->PutInt(0x60CE75AE, m_goalIndex);
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    out->PutNameKey(0x9982E501, Checksums::HashFromText(CreatedGoalName()));
    out->PutInt(0xC51BF6E3, m_timeLimit);
    out->PutInt(0x14938611, m_won);
    out->PutNameKey(0x81CFF663, m_controlType);
    out->AppendWord(0x4BC5229D, m_label);
    out->AppendWord(0x0C5D7E6B, m_summaryText);
    out->AppendWord(0x5DE57284, m_winMessage);
    out->AppendWord(0xB9426608, m_pedName);
    out->PutNameKey(0x6D11ED74, m_kindTag);
    name[0] = '\0';
    Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
    out->PutNameKey(0x71FC348B, Checksums::HashFromText(name));
    WriteSpecificParams(out, 0);
    if (!m_vertexTotal)
        return;
    Scripts::CArray* points = NewArrayOf(m_vertexTotal, kStructureArray);
    for (uint32_t i = 0; i < m_vertexTotal; ++i) {
        const EditedGoalPoint& point = m_pathVertices[i];
        Scripts::CStruct* entry = NewStructure();
        entry->PutVec3(0x7F261953, point.pos.x, point.pos.y, point.pos.z);
        entry->PushFloat(0x0AB21AF0, point.height);
        entry->PushFloat(0xFF7EBAF6, point.angle);
        points->SetElement(i, reinterpret_cast<uint32_t>(entry));
    }
    out->AppendArrayRef(0x32DF12E0, points);
}

// 0x004EB6B0
void CEditedGoal::AddToGoalManager(bool unwin)
{
    Scripts::CStruct* params = NewStructure();
    WriteGoalManagerParams(params);
    GoalSystem::CGoalManager* manager = GoalSystem::GetObjectiveBoard();
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    manager->EnrolObjective(Checksums::HashFromText(CreatedGoalName()), params);
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_goalIndex);
    GoalSystem::CObjective* goal = manager->GetGoal(Checksums::HashFromText(CreatedGoalName()), 1);
    goal->MarkViewed();
    if (unwin)
        m_won = false;
    if (m_won)
        GoalSystem::Virtual<void>(goal, 0x68); // MarkWon
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace Ed
