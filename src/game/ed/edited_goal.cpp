// An edited goal's own code (ParkEditing::CEditedGoal): see goal_editor.h. From the disassembly of 0x004E6B80-0x004E7B1E and
// 0x004E93C0.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/ed/goal_editor.h"
#include "game/ed/goal_editor_util.h"
#include "game/ed/park_editor.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/sys/crt.h"

#pragma warning(disable : 4996) // (strcpy: the original's inline copies of the texts)

namespace ParkEditing {

char g_goalNameBuffer[104];

namespace {

constexpr float kPi = 3.14159274101257324f;          // 0x006453D8
constexpr float kRadiansToDegrees = 57.2957763671875f; // 0x00645468

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}

// The goal editor's shared name buffer (also GoalEditorMarkerObject<n>'s).
char* NameBuffer() { return g_goalNameBuffer; }

uint32_t CurrentLevel() { return GameModules::Skate::Sole()->m_level; }

} // namespace

// 0x004E93C0
CEditedGoal* CEditedGoal::MakeNew()
{
    InitEditedGoal(reinterpret_cast<uint8_t*>(this));
    return this;
}

// 0x004E6B80
void CEditedGoal::DropGapEntry(uint8_t gap)
{
    for (int i = 0; i < m_numGaps; ++i) {
        if (m_gaps[i] != gap)
            continue;
        for (int j = i; j < m_numGaps - 1; ++j)
            m_gaps[j] = m_gaps[j + 1];
        --m_numGaps;
        return;
    }
}

// 0x004E6BF0
void CEditedGoal::RemoveGapAndRenumber(uint8_t gap)
{
    DropGapEntry(gap);
    for (int i = 0; i < m_numGaps; ++i) {
        if (m_gaps[i] > gap)
            --m_gaps[i];
    }
}

// 0x004E7390
void CEditedGoal::RegisterGapEntry(uint8_t gap)
{
    uint8_t count = m_numGaps;
    for (int i = 0; i < m_numGaps; ++i) {
        if (m_gaps[i] == gap)
            return;
    }
    if (count >= 10) {
        DebugOut::Printf("Warning! MAX_GAPS limit reached!\n");
        return;
    }
    m_gaps[count] = gap;
    ++m_numGaps;
}

// 0x004E6C40
void CEditedGoal::AddKeyComboSet(int set, int mustBeFlawless, int spin, int tapTotal)
{
    int index = m_numKeyComboSets;
    bool found = false;
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        if (m_keyComboSets[i].set == set) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found)
        m_numKeyComboSets = index + 1;
    KeyComboSet& entry = m_keyComboSets[index];
    entry.tapTotal = static_cast<uint8_t>(tapTotal);
    entry.mustBeFlawless = static_cast<uint8_t>(mustBeFlawless);
    entry.set = static_cast<uint8_t>(set);
    entry.spin = static_cast<uint8_t>(spin / 180);
}

// 0x004E6CD0
void CEditedGoal::RemoveKeyComboSet(int set)
{
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        if (m_keyComboSets[i].set != set)
            continue;
        for (int j = i; j < m_numKeyComboSets - 1; ++j)
            m_keyComboSets[j] = m_keyComboSets[j + 1];
        --m_numKeyComboSets;
        return;
    }
}

// 0x004E6D50
KeyComboSet* CEditedGoal::FindKeyComboSet(int set)
{
    int count = m_numKeyComboSets;
    for (int i = 0; i < count; ++i) {
        if (m_keyComboSets[i].set == set)
            return &m_keyComboSets[i];
    }
    return nullptr;
}

// 0x004E6DA0
void CEditedGoal::ChangeSpecies(uint32_t type)
{
    m_kindTag = type;
    m_pathVertices[0].kind = kPointPro;
    m_pathVertices[0].height = 0.0f;
    m_pathVertices[1].kind = kPointStart;
    m_pathVertices[1].height = 0.0f;
    Scripts::CStruct* defaults = GoalTypeStructure(type);
    switch (m_kindTag) {
    case kGoalTypeRace:
        m_minPoints = 4;
        m_maxPoints = 12;
        for (int i = 2; i < 13; ++i) {
            m_pathVertices[i].kind = kPointCheckpoint;
            m_pathVertices[i].height = 0.0f;
        }
        break;
    case kGoalTypeCombo:
        m_minPoints = 7;
        m_maxPoints = 7;
        for (int i = 2; i < m_minPoints; ++i) {
            m_pathVertices[i].kind = kPointObject;
            m_pathVertices[i].height = 0.0f;
        }
        break;
    case kGoalTypeSkate:
        m_minPoints = 7;
        m_maxPoints = 7;
        for (int i = 2; i < 13; ++i) {
            m_pathVertices[i].kind = kPointObject;
            m_pathVertices[i].height = 0.0f;
        }
        break;
    case kGoalTypeComboTricks:
    case kGoalTypeGap:
    case kGoalTypeScore:
    case kGoalTypeTrickAttack:
    case kGoalTypeKeyCombo:
    case kGoalTypeTricktris:
    case kGoalTypeHighCombo:
        m_minPoints = 2;
        m_maxPoints = 2;
        if (m_kindTag == kGoalTypeKeyCombo)
            m_twistRate = 0;
        break;
    default:
        break;
    }
    if (!defaults)
        return;
    defaults->FetchIntValue(0xC51BF6E3, &m_timeLimit, 0);
    defaults->FetchIntValue(0xCD66C8AE, &m_pointsHeld, 0);
    const char* text = ""; // ""
    defaults->FetchText(0x4BC5229D, &text, 0);
    std::strcpy(m_label, text);
}

// 0x004E6F70
void CEditedGoal::SetCountdownSeconds(int seconds)
{
    m_timeLimit = seconds;
    if (m_kindTag == kGoalTypeComboTricks || m_kindTag == kGoalTypeTrickAttack || m_kindTag == kGoalTypeTricktris) {
        if (m_timeToStopAddingTricks > seconds - 5)
            m_timeToStopAddingTricks = seconds - 5;
    }
}

// 0x004E7050
void CEditedGoal::CreateMarker(int index)
{
    Scripts::CStruct params;
    params.PrepareAll();
    switch (m_pathVertices[index].kind) {
    case kPointPro:
        params.PutNameKey(0x7321A8D6, 0x061A741E);
        break;
    case kPointStart:
        params.PutNameKey(0x7321A8D6, 0x1806DDF8);
        params.AppendWord(0x286A8D26, "gameobjects\\p1_cursor\\p1_cursor.mdl");
        break;
    case kPointObject:
        params.PutNameKey(0x7321A8D6, 0x71FD11F5);
        params.AppendWord(0x286A8D26, GoalObjectModel(m_kindTag, index - 2));
        params.PutNameKey(0u, 0x3BD176D9u); // (a flag: no name)
        break;
    case kPointCheckpoint:
        params.PutNameKey(0x7321A8D6, 0xF0FF0841);
        params.AppendWord(0x286A8D26, GoalObjectModel(m_kindTag, index - 2));
        break;
    default:
        break;
    }
    Sprintf(NameBuffer(), "GoalEditorMarkerObject%d", index);
    params.PutNameKey(0xA1DC81F9, Checksums::HashFromText(NameBuffer()));
    const EditedGoalPoint& point = m_pathVertices[index];
    params.PutVec3(0x7F261953, point.pos.x, point.height + point.pos.y, point.pos.z);
    params.PushFloat(0xFF7EBAF6, point.angle * kRadiansToDegrees);
    Scripts::LaunchScript(0xEFC08BAE, &params, nullptr, false, nullptr);
    params.Clear();
}

// 0x004E7780
void CEditedGoal::CreateMarkers()
{
    for (int i = 1; i < m_vertexTotal; ++i)
        CreateMarker(i);
    m_currentPoint = 0;
    m_won = false;
}

// 0x004E7210
bool CEditedGoal::NearOtherPoint(const Maths::Vec4& pos)
{
    float radius = Scripts::FetchFloat(0x71B20276);
    float startToObjectRadius = Scripts::FetchFloat(0x3F6E9739);
    int count = m_vertexTotal;
    int current = m_currentPoint;
    for (int i = 0; i < count; ++i) {
        if (i == current)
            continue;
        const EditedGoalPoint& point = m_pathVertices[i];
        int currentKind = m_pathVertices[current].kind;
        double within = radius;
        if ((currentKind == kPointObject && point.kind == kPointStart) ||
            (currentKind == kPointStart && point.kind == kPointObject))
            within = startToObjectRadius;
        // (x's difference rounded to a float; y's and z's not, as the original's x87 sums)
        float dx = static_cast<float>(static_cast<double>(point.pos.x) - pos.x);
        double dy = (static_cast<double>(point.pos.y) + point.height) - pos.y;
        double dz = static_cast<double>(point.pos.z) - pos.z;
        double distance = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx);
        if (distance < within)
            return true;
    }
    return false;
}

// 0x004E7300
bool CEditedGoal::CanPlacePoint(const Maths::Vec4& pos, float height)
{
    if (CurrentLevel() == kParkEditorLevel && !CParkBuilderMode::IsInsidePark(pos, 30.0f))
        return false;
    Maths::Vec4 raised = pos;
    raised.y = pos.y + height;
    return !NearOtherPoint(raised);
}

// 0x004E77C0
bool CEditedGoal::PlacePoint(const Maths::Vec4& pos, float height, float angle)
{
    m_lastPointPlaced = false;
    if (!CanPlacePoint(pos, height))
        return false;
    EditedGoalPoint& point = m_pathVertices[m_currentPoint];
    point.pos = pos;
    m_pathVertices[m_currentPoint].height = height;
    m_pathVertices[m_currentPoint].angle = angle;
    if (m_currentPoint == m_maxPoints - 1)
        m_lastPointPlaced = true;
    if (m_currentPoint == 0)
        Scripts::LaunchScript(0xD066A889, nullptr, nullptr, false, nullptr);
    CreateMarker(m_currentPoint);
    ++m_currentPoint;
    RunMarkerObjectScript(m_currentPoint);
    if (m_currentPoint < m_vertexTotal)
        return true;
    m_vertexTotal = m_currentPoint;
    if (m_currentPoint == m_maxPoints) {
        m_currentPoint = static_cast<uint8_t>(m_maxPoints - 1);
        return true;
    }
    // The next point starts where this one is.
    m_pathVertices[m_currentPoint].pos = pos;
    m_pathVertices[m_currentPoint].height = height;
    m_pathVertices[m_currentPoint].angle = angle;
    if ((m_kindTag == kGoalTypeCombo || m_kindTag == kGoalTypeSkate) && m_currentPoint == 2) {
        float& next = m_pathVertices[2].height;
        next = static_cast<float>(static_cast<double>(Scripts::FetchFloat(0x71EC75E9)) + next);
    }
    return true;
}

// 0x004E7960
bool CEditedGoal::GetCurrentPoint(Maths::Vec4* pos, float* height, float* angle)
{
    *pos = m_pathVertices[m_currentPoint].pos;
    *height = m_pathVertices[m_currentPoint].height;
    *angle = m_pathVertices[m_currentPoint].angle;
    return true;
}

// 0x004E79C0
void CEditedGoal::WriteNodePosition(int index, uint32_t nodeName)
{
    Scripts::CStruct* node = LevelNodes::FetchVertex(nodeName, true);
    const EditedGoalPoint& point = m_pathVertices[index];
    node->PutVec3(0x7F261953, point.pos.x, point.height + point.pos.y, point.pos.z);
    node->PutVec3(0x9D2D0915, 0.0f, point.angle - kPi, 0.0f);
    if (static_cast<uint32_t>(index) >= 2) {
        node->PutNameKey(0x7321A8D6, GoalObjectName(m_kindTag, index - 2));
        node->AppendWord(0x286A8D26, GoalObjectModel(m_kindTag, index - 2));
    }
}

// 0x004E7A80
void CEditedGoal::EmitNodeCoords()
{
    char name[100];
    for (int i = 0; i < m_vertexTotal; ++i) {
        name[0] = '\0';
        if (i == 0)
            Sprintf(name, "TRG_EditedGoal%d_Pro", m_goalIndex);
        else if (i == 1)
            Sprintf(name, "TRG_EditedGoal%d_Restart", m_goalIndex);
        else
            Sprintf(name, "TRG_EditedGoal%d_Object_%d", m_goalIndex, i - 2);
        WriteNodePosition(i, Checksums::HashFromText(name));
    }
}

// 0x004E73F0
void CEditedGoal::ReadKeyComboSets(Scripts::CStruct* params)
{
    Scripts::CArray* sets = nullptr;
    if (!params->FetchList(0x0490F171, &sets, 0))
        return;
    m_numKeyComboSets = static_cast<int>(sets->m_extent);
    for (int i = 0; i < m_numKeyComboSets; ++i) {
        auto* entry = reinterpret_cast<Scripts::CStruct*>(sets->FetchPart(static_cast<uint32_t>(i)));
        KeyComboSet& set = m_keyComboSets[i];
        int value = 0;
        entry->FetchIntValue(0xEB7D6498, &value, 0);
        set.set = static_cast<uint8_t>(value);
        value = 0;
        entry->FetchIntValue(0xEDF5DB70, &value, 0);
        set.spin = static_cast<uint8_t>(value / 180);
        value = 0;
        entry->FetchIntValue(0xA4BEE6A1, &value, 0);
        set.tapTotal = static_cast<uint8_t>(value);
        set.mustBeFlawless = entry->HasBareFlag(0x687E2C25);
    }
}

// 0x004E74F0
void CEditedGoal::ReadTrickSettings(Scripts::CStruct* params)
{
    params->FetchIntValue(0x01181B29, &m_accelerationInterval, 0);
    params->FetchFloat(0x076F65C8, &m_accelerationPercent, 0);
    params->FetchIntValue(0xDED8540A, &m_trickTime, 0);
    params->FetchIntValue(0xA6D96603, &m_timeToStopAddingTricks, 0);
    params->FetchIntValue(0x89473DB7, &m_maxTricks, 0);
    params->FetchIntValue(0xEDF5DB70, &m_twistRate, 0);
    ReadKeyComboSets(params);
}

// 0x004E7590
void CEditedGoal::ReadSpecificParams(Scripts::CStruct* params)
{
    switch (m_kindTag) {
    case kGoalTypeRace:
        params->FetchIntValue(0x1DAAE95B, &m_raceTotalLaps, 0);
        params->FetchIntValue(0xD9426372, &m_racePerCheckpointTime, 0);
        break;
    case kGoalTypeComboTricks:
        params->FetchIntValue(0x67DD90CA, &m_comboSize, 0);
        if (params->HasBareFlag(0xDF47B144))
            m_singleCombo = true;
        ReadTrickSettings(params);
        break;
    case kGoalTypeGap: {
        const char* trick = nullptr;
        if (params->FetchText(0x730844A3, &trick, 0))
            std::strcpy(m_requiredTrick, trick);
        Scripts::CArray* gaps = nullptr;
        params->FetchList(0xD76C173E, &gaps, 0);
        if (!gaps)
            break;
        m_numGaps = static_cast<uint8_t>(gaps->m_extent);
        for (int i = 0; i < m_numGaps; ++i)
            m_gaps[i] = static_cast<uint8_t>(gaps->FetchPart(static_cast<uint32_t>(i)));
        break;
    }
    case kGoalTypeScore:
    case kGoalTypeHighCombo:
        params->FetchIntValue(0xCD66C8AE, &m_pointsHeld, 0);
        break;
    case kGoalTypeTrickAttack:
        ReadTrickSettings(params);
        break;
    case kGoalTypeKeyCombo:
        ReadKeyComboSets(params);
        break;
    case kGoalTypeTricktris:
        params->FetchIntValue(0x45A375C9, &m_tricktrisBlockSize, 0);
        params->FetchIntValue(0xBFD3A831, &m_tricktrisTotalToWin, 0);
        ReadTrickSettings(params);
        break;
    default:
        break;
    }
}

} // namespace Ed
