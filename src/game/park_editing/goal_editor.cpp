// The goal editor component (ParkEditing::CObjectiveAuthoring): see goal_editor.h. From the disassembly of 0x004E6FB0-
// 0x004EBC3D (the component's own code; the edited goals' is in edited_goal.cpp / edited_goal_io.cpp).

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/code_address.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/core/stubs.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/goals/goal_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/control_pad.h"
#include "game/obj/editor_camera.h"
#include "game/obj/entity_hub.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/script/struct_buffer.h"
#include "game/skate/skate_module.h"
#include "game/sys/crt.h"

namespace ParkEditing {

uint32_t g_goalEditorMemberFunctions[216]{
            0x0AB710F3, 0x81F01058, 0, 0, 0, 0,
            0x5C155A29, 0x81F01058, 0, 0, 0, 0,
            0xFB279560, 0x81F01058, 0, 0, 0, 0,
            0xCCA11C78, 0x81F01058, 0, 0, 0, 0,
            0x1D30A7B6, 0x81F01058, 0, 0, 0, 0,
            0xE5ED63B7, 0x81F01058, 0, 0, 0, 0,
            0x03EBD2C9, 0x81F01058, 0, 0, 0, 0,
            0x47208C6D, 0x81F01058, 0, 0, 0, 0,
            0x769309E1, 0x81F01058, 0, 0, 0, 0,
            0x88D54A10, 0x81F01058, 0, 0, 0, 0,
            0xFF9E8752, 0x81F01058, 0, 0, 0, 0,
            0x259C59DD, 0x81F01058, 0, 0, 0, 0,
            0x1F80B5AE, 0x81F01058, 0, 0, 0, 0,
            0x26848221, 0x81F01058, 0, 0, 0, 0,
            0xFDB2C514, 0x81F01058, 0, 0, 0, 0,
            0x98ABBD09, 0x81F01058, 0, 0, 0, 0,
            0x66A33157, 0x81F01058, 0, 0, 0, 0,
            0x7AABBC09, 0x81F01058, 0, 0, 0, 0,
            0x69B872E3, 0x81F01058, 0, 0, 0, 0,
            0x96F11BD3, 0x81F01058, 0, 0, 0, 0,
            0x96874358, 0x81F01058, 0, 0, 0, 0,
            0x5BD8050A, 0x81F01058, 0, 0, 0, 0,
            0x5408AC04, 0x81F01058, 0, 0, 0, 0,
            0xB525B113, 0x81F01058, 0, 0, 0, 0,
            0x4A569426, 0x81F01058, 0, 0, 0, 0,
            0x410821DF, 0x81F01058, 0, 0, 0, 0,
            0x60288ABE, 0x81F01058, 0, 0, 0, 0,
            0x831ECA10, 0x81F01058, 0, 0, 0, 0,
            0xDBA8E3FC, 0x81F01058, 0, 0, 0, 0,
            0xD7258335, 0x81F01058, 0, 0, 0, 0,
            0x9E5A634A, 0x81F01058, 0, 0, 0, 0,
            0x7D5073F6, 0x81F01058, 0, 0, 0, 0,
            0xEC316A9C, 0x81F01058, 0, 0, 0, 0,
            0x20B566B1, 0x81F01058, 0, 0, 0, 0,
            0xD5F6EA3B, 0x81F01058, 0, 0, 0, 0,
            0, 0x81F01058, 0, 0, 0, 0};

namespace {

static VtableSlot kVtable_slots[] = {&ParkEditing::CObjectiveAuthoring::Teardown,
         &ParkEditing::CObjectiveAuthoring::TickFrame,
         Stubs::IgnoreArgument,
         &ParkEditing::CObjectiveAuthoring::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         &ParkEditing::CObjectiveAuthoring::Conceal,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
constexpr uint32_t kGoalEditor = 0x81F01058;
constexpr uint32_t kInput = 0x27D7CD28, kEditorCamera = 0xAC8946E2;
constexpr uint32_t kGoalsArray = 0x38DBE1D0;
constexpr uint32_t kParkEditorGameplayLevel = 0xB664035D; // Load_Sk5Ed_gameplay

template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}
// The game's _stricmp (0x00626EFB).
int CompareNoCase(const char* a, const char* b)
{
    return Crt::Stricmp(a, b);
}

char* CreatedGoalName() { return g_createdGoalName; }
uint32_t CreatedGoalId(int index)
{
    Sprintf(CreatedGoalName(), "CreatedGoal%d", index);
    return Checksums::HashFromText(CreatedGoalName());
}

// The current level, the park editor's gameplay level counting as the park editor's.
uint32_t CurrentLevel()
{
    uint32_t level = GameModules::Skate::Sole()->m_level;
    return level == kParkEditorGameplayLevel ? kParkEditorLevel : level;
}

GameObjects::CEditorViewModule* ViewCam(GameObjects::CObjectModule* camera)
{
    return static_cast<GameObjects::CEditorViewModule*>(camera);
}

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// Whether `x`, `z` lies outside x0..x1, z0..z1 (not a number: inside, as the original's comparisons).
bool Outside(float x, float z, float x0, float z0, float x1, float z1)
{
    return x < x0 || x > x1 || z < z0 || z > z1;
}

} // namespace

// 0x004EB7F0
GameObjects::CObjectModule* __cdecl CObjectiveAuthoring::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CObjectiveAuthoring));
    return memory ? static_cast<CObjectiveAuthoring*>(memory)->MakeNew() : nullptr;
}

// 0x004E6FB0
void __cdecl CObjectiveAuthoring::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kGoalEditor), g_goalEditorMemberFunctions, 0x18);
}

// 0x004EA230
CObjectiveAuthoring* CObjectiveAuthoring::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    for (CEditedGoal& goal : m_goals)
        goal.MakeNew();
    m_kindTag = kGoalEditor;
    ClearLevelGoals();
    ClearParkGoals();
    mp_input = nullptr;
    m_viewCamera = nullptr;
    return this;
}

// 0x004E6FD0
void CObjectiveAuthoring::DestroyBody()
{
    m_vtable = kVtable;
    Scripts::LaunchScript(0xFCC60D8B, nullptr, nullptr, false, nullptr);
    // (the goals' destructors are empty)
    CObjectModule::DestroyBody();
}

// 0x004E7B20
CObjectiveAuthoring* CObjectiveAuthoring::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004E8210
void CObjectiveAuthoring::Seal()
{
    mp_input = m_entity->FetchModule(kInput);
    m_viewCamera = m_entity->FetchModule(kEditorCamera);
}

// 0x004E8470
CObjectiveAuthoring* __cdecl CObjectiveAuthoring::Pull()
{
    auto* manager = reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::CompositeObjectManager());
    auto* object = reinterpret_cast<GameObjects::CGameEntity*>(manager->LookUpEntity(kGoalEditor));
    return static_cast<CObjectiveAuthoring*>(object->FetchModule(kGoalEditor));
}

// 0x004E7BE0
CEditedGoal* CObjectiveAuthoring::FindGoalByName(const char* name)
{
    uint32_t level = CurrentLevel();
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == level && CompareNoCase(goal.m_label, name) == 0)
            return &goal;
    }
    return nullptr;
}

// 0x004E7C60
CEditedGoal* CObjectiveAuthoring::FindGoalById(uint32_t id)
{
    uint32_t level = CurrentLevel();
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == level && CreatedGoalId(goal.m_goalIndex) == id)
            return &goal;
    }
    return nullptr;
}

// 0x004E7D00
CEditedGoal* CObjectiveAuthoring::GetGoalFromParams(Scripts::CStruct* params)
{
    uint32_t id = 0;
    if (params->ReadHashValue(0x9982E501, &id, 0))
        return FindGoalById(id);
    const char* name = nullptr;
    if (params->FetchText(0xBFECC45B, &name, 0))
        return FindGoalByName(name);
    int index = 0;
    if (params->FetchIntValue(0x474A6A7F, &index, 0)) {
        uint32_t level = CurrentLevel();
        for (CEditedGoal& goal : m_goals) {
            if (goal.m_takenCount && goal.m_level == level && goal.m_goalIndex == index)
                return &goal;
        }
        return nullptr;
    }
    return m_activeNode;
}

// 0x004E7E00
int CObjectiveAuthoring::CountUsedGoals()
{
    int count = 0;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount)
            ++count;
    }
    return count;
}

// 0x004E7E50
int CObjectiveAuthoring::CountCompleteGoals(uint32_t level)
{
    int count = 0;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && (!level || goal.m_level == level) && goal.m_vertexTotal >= goal.m_minPoints)
            ++count;
    }
    return count;
}

// 0x004E8130
bool CObjectiveAuthoring::PointNearAnyGoal(const Maths::Vec4& pos, float height, uint32_t level, int index)
{
    float x = pos.x, y = pos.y + height, z = pos.z;
    float radius = Scripts::FetchFloat(0x71B20276);
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_takenCount || goal.m_level != level || goal.m_vertexTotal <= index)
            continue;
        const EditedGoalPoint& point = goal.m_pathVertices[index];
        // (x's difference rounded to a float; y's and z's not, as the original's x87 sums)
        float dx = static_cast<float>(static_cast<double>(point.pos.x) - x);
        double dy = (static_cast<double>(point.pos.y) + point.height) - y;
        double dz = static_cast<double>(point.pos.z) - z;
        if (std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx) < radius)
            return true;
    }
    return false;
}

// 0x004E8240
void CObjectiveAuthoring::ClearLevelGoals()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level != kParkEditorLevel)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
    m_activeNode = nullptr;
}

// 0x004E8280
void CObjectiveAuthoring::ClearParkGoals()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == kParkEditorLevel)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
    m_activeNode = nullptr;
}

// 0x004E82C0
void CObjectiveAuthoring::ClearIncompleteGoals()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_vertexTotal < goal.m_minPoints)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
}

// 0x004E8300
bool CObjectiveAuthoring::AnyParkGoalOutside(float x0, float z0, float x1, float z1)
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == kParkEditorLevel && goal.m_vertexTotal > 1 &&
            Outside(goal.m_pathVertices[1].pos.x, goal.m_pathVertices[1].pos.z, x0, z0, x1, z1))
            return true;
    }
    return false;
}

// 0x004E8390
void CObjectiveAuthoring::RemoveParkGoalsOutside(float x0, float z0, float x1, float z1)
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == kParkEditorLevel && goal.m_vertexTotal > 1 &&
            Outside(goal.m_pathVertices[1].pos.x, goal.m_pathVertices[1].pos.z, x0, z0, x1, z1))
            DropObjective(&goal);
    }
}

// 0x004E8420
void CObjectiveAuthoring::RemoveGapFromParkGoals(int gap)
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == kParkEditorLevel && goal.m_kindTag == kGoalTypeGap)
            goal.RemoveGapAndRenumber(static_cast<uint8_t>(gap));
    }
}

// 0x004E8090
void CObjectiveAuthoring::DropObjective(CEditedGoal* goal)
{
    if (goal->m_takenCount) {
        GoalSystem::CGoalManager* manager = GoalSystem::GetObjectiveBoard();
        if (manager->GoalExists(CreatedGoalId(goal->m_goalIndex)))
            manager->DropObjective(CreatedGoalId(goal->m_goalIndex));
    }
    InitEditedGoal(reinterpret_cast<uint8_t*>(goal));
    if (m_activeNode == goal)
        m_activeNode = nullptr;
}

// 0x004E7B50
void CObjectiveAuthoring::UpdateCameraCollision()
{
    CEditedGoal* goal = m_activeNode;
    int kind = goal->m_vertexTotal ? goal->m_pathVertices[goal->m_currentPoint].kind : kPointPro;
    if (kind != kPointPro && kind != kPointStart) {
        ViewCam(m_viewCamera)->m_simpleCollision = true;
        return;
    }
    ViewCam(m_viewCamera)->m_simpleCollision = GameModules::Skate::Sole()->m_level == kParkEditorLevel;
}

// 0x004E7F30
void CObjectiveAuthoring::MoveCursorToCurrentPoint()
{
    const EditedGoalPoint& point = m_activeNode->m_pathVertices[m_activeNode->m_currentPoint];
    GameObjects::CEditorViewModule* camera = ViewCam(m_viewCamera);
    camera->m_levelY = point.height;
    camera->m_heading = point.angle;
    camera->m_cursor = point.pos;
}

// 0x004E7FC0
void CObjectiveAuthoring::RunCursorScript()
{
    Scripts::CStruct params;
    params.PrepareAll();
    GameObjects::CEditorViewModule* camera = ViewCam(m_viewCamera);
    const Maths::Vec4& cursor = camera->m_cursor;
    params.PutVec3(0x7F261953, cursor.x, cursor.y + camera->m_levelY, cursor.z);
    params.PushFloat(0xFF7EBAF6, camera->m_heading);
    Scripts::LaunchScript(0xDEA7FE56, &params, nullptr, false, nullptr);
    params.Clear();
}

// 0x004EA580
void CObjectiveAuthoring::RefreshCursor()
{
    CEditedGoal* goal = m_activeNode;
    int kind = goal->m_vertexTotal ? goal->m_pathVertices[goal->m_currentPoint].kind : kPointPro;
    const char* model;
    if (kind == kPointStart) {
        model = "gameobjects\\p1_cursor\\p1_cursor.mdl";
    } else if (kind == kPointObject) {
        int letter = 0;
        if (goal->m_kindTag == kGoalTypeCombo || goal->m_kindTag == kGoalTypeSkate)
            letter = goal->m_currentPoint - 2;
        model = GoalObjectModel(goal->m_kindTag, letter);
    } else {
        model = "gameobjects\\goal_cursor\\goal_cursor.mdl";
    }
    Scripts::CStruct params;
    params.PrepareAll();
    goal = m_activeNode;
    kind = goal->m_vertexTotal ? goal->m_pathVertices[goal->m_currentPoint].kind : kPointPro;
    if (kind == kPointPro)
        params.PutNameKey(0x7321A8D6, 0x061A741E);
    else if (kind == kPointCheckpoint)
        params.PutNameKey(0x7321A8D6, 0xF0FF0841);
    params.AppendWord(0x286A8D26, model);
    Scripts::LaunchScript(0x36791E3C, &params, m_entity, false, nullptr);
    UpdateCameraCollision();
    params.Clear();
}

// 0x004EA6C0
void CObjectiveAuthoring::BeginFreshObjective()
{
    ClearIncompleteGoals();
    uint32_t level = CurrentLevel();
    // The lowest number no complete goal of the level has.
    int index = 0;
    for (bool taken = true; taken;) {
        taken = false;
        for (CEditedGoal& goal : m_goals) {
            if (goal.m_takenCount && goal.m_vertexTotal >= goal.m_minPoints && goal.m_level == level &&
                goal.m_goalIndex == index) {
                ++index;
                taken = true;
                break;
            }
        }
    }
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount)
            continue;
        m_activeNode = &goal;
        InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
        m_activeNode->m_takenCount = true;
        m_activeNode->m_level = level;
        m_activeNode->m_goalIndex = index;
        RefreshCursor();
        return;
    }
}

// 0x004EA780
void CObjectiveAuthoring::PlaceParkGoalsOnGround()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == kParkEditorLevel)
            goal.PlacePointsOnGround();
    }
}

// 0x004EA470
void CObjectiveAuthoring::ReadGoals(Scripts::CStruct* params, int parkOnly)
{
    if (parkOnly)
        ClearParkGoals();
    else
        ClearLevelGoals();
    Scripts::CArray* goals = nullptr;
    params->FetchList(kGoalsArray, &goals, 0);
    if (!goals)
        return;
    uint32_t count = goals->m_extent;
    for (uint32_t i = 0; i < count; ++i) {
        auto* entry = reinterpret_cast<Scripts::CStruct*>(goals->FetchPart(i));
        uint32_t level = 0;
        entry->ReadHashValue(0x651533EC, &level, 0);
        bool park = level == kParkEditorGameplayLevel || level == kParkEditorLevel;
        if (park != (parkOnly != 0))
            continue;
        for (CEditedGoal& goal : m_goals) {
            if (!goal.m_takenCount) {
                goal.DecodeFromStruct(entry);
                break;
            }
        }
    }
    m_activeNode = nullptr;
}

// 0x004EA2C0
const uint8_t* CObjectiveAuthoring::ReadLevelGoalsFromBuffer(uint32_t level, const uint8_t* buffer)
{
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    Scripts::CStruct* params = NewStructure();
    const uint8_t* end = Scripts::DecodeFromBytes(params, buffer);
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level == level)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
    m_activeNode = nullptr;
    Scripts::CArray* goals = nullptr;
    params->FetchList(kGoalsArray, &goals, 1);
    uint32_t left = goals->m_extent;
    uint32_t next = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!left)
            break;
        if (goal.m_takenCount)
            continue;
        goal.DecodeFromStruct(reinterpret_cast<Scripts::CStruct*>(goals->FetchPart(next++)));
        goal.m_level = level;
        --left;
    }
    DeleteStructure(params);
    return end;
}

// 0x004EB850
void CObjectiveAuthoring::WriteGoals(Scripts::CStruct* out)
{
    uint32_t count = 0;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_takenCount && goal.m_level != kParkEditorLevel)
            ++count;
    }
    if (!count)
        return;
    Scripts::CArray* goals = Scripts::NewArray();
    goals = goals ? goals->PrepareAll() : nullptr;
    goals->ConfigureSizeKind(count, Scripts::QBVAL_STRUCT);
    uint32_t index = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_takenCount || goal.m_level == kParkEditorLevel)
            continue;
        Scripts::CStruct* entry = NewStructure();
        goal.ExportToStruct(entry);
        goals->SetElement(index++, reinterpret_cast<uint32_t>(entry));
    }
    out->AppendArrayRef(kGoalsArray, goals);
}

// 0x004EBA10
void CObjectiveAuthoring::WriteLevelGoals(uint32_t level, Scripts::CStruct* out)
{
    // (sized by the complete goals, filled with every used one: as the original)
    uint32_t count = static_cast<uint32_t>(CountCompleteGoals(level));
    Scripts::CArray* goals = Scripts::NewArray();
    goals = goals ? goals->PrepareAll() : nullptr;
    goals->ConfigureSizeKind(count, Scripts::QBVAL_STRUCT);
    uint32_t index = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_takenCount || goal.m_level != level)
            continue;
        Scripts::CStruct* entry = NewStructure();
        goal.ExportToStruct(entry);
        goals->SetElement(index++, reinterpret_cast<uint32_t>(entry));
    }
    out->AppendArrayRef(kGoalsArray, goals);
}

// 0x004EBB70
uint32_t CObjectiveAuthoring::WriteLevelGoalsToBuffer(uint32_t level, uint8_t* buffer, uint32_t size)
{
    Scripts::CStruct* params = NewStructure();
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    WriteLevelGoals(level, params);
    uint32_t written = Scripts::EmitIntoBuffer(params, buffer, size, 1, nullptr);
    DeleteStructure(params);
    return written;
}

// 0x004EA7C0
bool CObjectiveAuthoring::NewEditorGoal(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    BeginFreshObjective();
    Sprintf(CreatedGoalName(), "CreatedGoal%d", m_activeNode->m_goalIndex);
    script->m_args->PutNameKey("goal_id", Checksums::HashFromText(CreatedGoalName()));
    return true;
}

namespace {

// Whether a point can't go at the cursor: the start too near another goal's pro, or the pro too near another
// goal's start.
bool NearOtherGoals(CObjectiveAuthoring* editor, const Maths::Vec4& pos, float height)
{
    CEditedGoal* goal = editor->m_activeNode;
    if (goal->m_vertexTotal && goal->m_pathVertices[goal->m_currentPoint].kind == kPointStart &&
        editor->PointNearAnyGoal(pos, height, goal->m_level, 0))
        return true;
    goal = editor->m_activeNode;
    if (!goal->m_vertexTotal || goal->m_pathVertices[goal->m_currentPoint].kind == kPointPro)
        return editor->PointNearAnyGoal(pos, height, goal->m_level, 1);
    return false;
}

GameObjects::CControlButton* Button(GameObjects::CObjectModule* input, uint32_t offset)
{
    return reinterpret_cast<GameObjects::CControlButton*>(reinterpret_cast<uint8_t*>(input) + offset);
}

} // namespace

// 0x004EBD30
void CObjectiveAuthoring::TickFrame()
{
    if (!m_activeNode) {
        RunCursorScript();
        return;
    }
    GameObjects::CControlButton* place = Button(mp_input, 0x1C0);
    if (place->m_fired) {
        place->ResetActivation();
        GameObjects::CEditorViewModule* camera = ViewCam(m_viewCamera);
        Maths::Vec4 pos = camera->m_cursor;
        float height = camera->m_levelY, angle = camera->m_heading;
        if (NearOtherGoals(this, pos, height)) {
            Scripts::LaunchScript(0xAEBC7020, nullptr, m_entity, false, nullptr);
        } else if (m_activeNode->PlacePoint(pos, height, angle)) {
            Scripts::LaunchScript(0x718B071B, nullptr, m_entity, false, nullptr);
            RefreshCursor();
            MoveCursorToCurrentPoint();
        } else {
            Scripts::LaunchScript(0xAEBC7020, nullptr, m_entity, false, nullptr);
        }
        if (m_activeNode->m_lastPointPlaced)
            Scripts::LaunchScript(0xC05E7E20, nullptr, m_entity, false, nullptr);
    }
    GameObjects::CControlButton* back = Button(mp_input, 0x1A0);
    if (back->m_fired) {
        back->ResetActivation();
        GameObjects::CEditorViewModule* camera = ViewCam(m_viewCamera);
        Maths::Vec4 pos = camera->m_cursor;
        float height = camera->m_levelY, angle = camera->m_heading;
        if (!NearOtherGoals(this, pos, height) && m_activeNode->PlacePointAndStepBack(pos, height, angle)) {
            Scripts::LaunchScript(0xFA275CC5, nullptr, m_entity, false, nullptr);
            RefreshCursor();
            MoveCursorToCurrentPoint();
        } else {
            Scripts::LaunchScript(0xADE100D6, nullptr, m_entity, false, nullptr);
        }
    }
    RunCursorScript();
}

// 0x004EBC40
void CObjectiveAuthoring::Conceal(bool hide)
{
    if (hide) {
        Scripts::LaunchScript(0xFCC60D8B, nullptr, nullptr, false, nullptr);
        return;
    }
    Scripts::CStruct* params = NewStructure();
    params->PutInt(0x67E6859A, 0);
    mp_input->ConfigureFromData(params);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    RefreshCursor();
}

// 0x004EC010
bool CObjectiveAuthoring::GetEditedGoalsInfo(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t level = 0;
    params->ReadHashValue(0x651533EC, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    int count = CountCompleteGoals(level);
    if (!count)
        return true;
    Scripts::CArray* goals = Scripts::NewArray();
    goals = goals ? goals->PrepareAll() : nullptr;
    goals->ConfigureSizeKind(static_cast<uint32_t>(count), Scripts::QBVAL_STRUCT);
    uint32_t index = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_takenCount || (level && goal.m_level != level) || goal.m_vertexTotal < goal.m_minPoints)
            continue;
        Scripts::CStruct* entry = NewStructure();
        goal.ExportToStruct(entry);
        goals->SetElement(index++, reinterpret_cast<uint32_t>(entry));
    }
    script->m_args->AppendArrayRef("EditedGoalsInfo", goals);
    return true;
}

} // namespace Ed
