// The goal editor component (Ed::CGoalEditorComponent): see goal_editor.h. From the disassembly of 0x004E6FB0-
// 0x004EBC3D (the component's own code; the edited goals' is in edited_goal.cpp / edited_goal_io.cpp).

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/code_address.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/core/stubs.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/goal_editor.h"
#include "game/ed/goal_editor_util.h"
#include "game/goals/goal_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/control_pad.h"
#include "game/obj/editor_camera.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/skate/skate_module.h"
#include "game/sys/crt.h"

namespace Ed {

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

static VtableSlot kVtable_slots[] = {&Ed::CGoalEditorComponent::Destroy,
         &Ed::CGoalEditorComponent::Update,
         Stubs::IgnoreArgument,
         &Ed::CGoalEditorComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         &Ed::CGoalEditorComponent::Hide,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &Obj::CBaseComponent::RunMemberFunction,
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
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
    return Crc::GenerateCRCFromString(CreatedGoalName());
}

// The current level, the park editor's gameplay level counting as the park editor's.
uint32_t CurrentLevel()
{
    uint32_t level = Mdl::Skate::Instance()->m_level;
    return level == kParkEditorGameplayLevel ? kParkEditorLevel : level;
}

Obj::CEditorCameraComponent* Camera(Obj::CBaseComponent* camera)
{
    return static_cast<Obj::CEditorCameraComponent*>(camera);
}

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

// Whether `x`, `z` lies outside x0..x1, z0..z1 (not a number: inside, as the original's comparisons).
bool Outside(float x, float z, float x0, float z0, float x1, float z1)
{
    return x < x0 || x > x1 || z < z0 || z > z1;
}

} // namespace

// 0x004EB7F0
Obj::CBaseComponent* __cdecl CGoalEditorComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CGoalEditorComponent));
    return memory ? static_cast<CGoalEditorComponent*>(memory)->Construct() : nullptr;
}

// 0x004E6FB0
void __cdecl CGoalEditorComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kGoalEditor), g_goalEditorMemberFunctions, 0x18);
}

// 0x004EA230
CGoalEditorComponent* CGoalEditorComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    for (CEditedGoal& goal : m_goals)
        goal.Construct();
    m_type = kGoalEditor;
    ClearLevelGoals();
    ClearParkGoals();
    mp_input = nullptr;
    mp_camera = nullptr;
    return this;
}

// 0x004E6FD0
void CGoalEditorComponent::DestroyBody()
{
    m_vtable = kVtable;
    Script::RunScript(0xFCC60D8B, nullptr, nullptr, false, nullptr);
    // (the goals' destructors are empty)
    CBaseComponent::DestroyBody();
}

// 0x004E7B20
CGoalEditorComponent* CGoalEditorComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004E8210
void CGoalEditorComponent::Finalize()
{
    mp_input = m_object->GetComponent(kInput);
    mp_camera = m_object->GetComponent(kEditorCamera);
}

// 0x004E8470
CGoalEditorComponent* __cdecl CGoalEditorComponent::Get()
{
    auto* manager = reinterpret_cast<Obj::CBaseManager*>(Obj::CompositeObjectManager());
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(manager->GetObjectByID(kGoalEditor));
    return static_cast<CGoalEditorComponent*>(object->GetComponent(kGoalEditor));
}

// 0x004E7BE0
CEditedGoal* CGoalEditorComponent::FindGoalByName(const char* name)
{
    uint32_t level = CurrentLevel();
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == level && CompareNoCase(goal.m_name, name) == 0)
            return &goal;
    }
    return nullptr;
}

// 0x004E7C60
CEditedGoal* CGoalEditorComponent::FindGoalById(uint32_t id)
{
    uint32_t level = CurrentLevel();
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == level && CreatedGoalId(goal.m_goalIndex) == id)
            return &goal;
    }
    return nullptr;
}

// 0x004E7D00
CEditedGoal* CGoalEditorComponent::GetGoalFromParams(Script::CStruct* params)
{
    uint32_t id = 0;
    if (params->GetChecksum(0x9982E501, &id, 0))
        return FindGoalById(id);
    const char* name = nullptr;
    if (params->GetString(0xBFECC45B, &name, 0))
        return FindGoalByName(name);
    int index = 0;
    if (params->GetInteger(0x474A6A7F, &index, 0)) {
        uint32_t level = CurrentLevel();
        for (CEditedGoal& goal : m_goals) {
            if (goal.m_used && goal.m_level == level && goal.m_goalIndex == index)
                return &goal;
        }
        return nullptr;
    }
    return mp_current;
}

// 0x004E7E00
int CGoalEditorComponent::CountUsedGoals()
{
    int count = 0;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used)
            ++count;
    }
    return count;
}

// 0x004E7E50
int CGoalEditorComponent::CountCompleteGoals(uint32_t level)
{
    int count = 0;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && (!level || goal.m_level == level) && goal.m_numPoints >= goal.m_minPoints)
            ++count;
    }
    return count;
}

// 0x004E8130
bool CGoalEditorComponent::PointNearAnyGoal(const Mth::Vector& pos, float height, uint32_t level, int index)
{
    float x = pos.x, y = pos.y + height, z = pos.z;
    float radius = Script::GetFloat(0x71B20276);
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_used || goal.m_level != level || goal.m_numPoints <= index)
            continue;
        const EditedGoalPoint& point = goal.m_points[index];
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
void CGoalEditorComponent::ClearLevelGoals()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level != kParkEditorLevel)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
    mp_current = nullptr;
}

// 0x004E8280
void CGoalEditorComponent::ClearParkGoals()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == kParkEditorLevel)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
    mp_current = nullptr;
}

// 0x004E82C0
void CGoalEditorComponent::ClearIncompleteGoals()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_numPoints < goal.m_minPoints)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
}

// 0x004E8300
bool CGoalEditorComponent::AnyParkGoalOutside(float x0, float z0, float x1, float z1)
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == kParkEditorLevel && goal.m_numPoints > 1 &&
            Outside(goal.m_points[1].pos.x, goal.m_points[1].pos.z, x0, z0, x1, z1))
            return true;
    }
    return false;
}

// 0x004E8390
void CGoalEditorComponent::RemoveParkGoalsOutside(float x0, float z0, float x1, float z1)
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == kParkEditorLevel && goal.m_numPoints > 1 &&
            Outside(goal.m_points[1].pos.x, goal.m_points[1].pos.z, x0, z0, x1, z1))
            RemoveGoal(&goal);
    }
}

// 0x004E8420
void CGoalEditorComponent::RemoveGapFromParkGoals(int gap)
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == kParkEditorLevel && goal.m_type == kGoalTypeGap)
            goal.RemoveGapAndRenumber(static_cast<uint8_t>(gap));
    }
}

// 0x004E8090
void CGoalEditorComponent::RemoveGoal(CEditedGoal* goal)
{
    if (goal->m_used) {
        Game::CGoalManager* manager = Game::GetGoalManager();
        if (manager->GoalExists(CreatedGoalId(goal->m_goalIndex)))
            manager->RemoveGoal(CreatedGoalId(goal->m_goalIndex));
    }
    InitEditedGoal(reinterpret_cast<uint8_t*>(goal));
    if (mp_current == goal)
        mp_current = nullptr;
}

// 0x004E7B50
void CGoalEditorComponent::UpdateCameraCollision()
{
    CEditedGoal* goal = mp_current;
    int kind = goal->m_numPoints ? goal->m_points[goal->m_currentPoint].kind : kPointPro;
    if (kind != kPointPro && kind != kPointStart) {
        Camera(mp_camera)->m_simpleCollision = true;
        return;
    }
    Camera(mp_camera)->m_simpleCollision = Mdl::Skate::Instance()->m_level == kParkEditorLevel;
}

// 0x004E7F30
void CGoalEditorComponent::MoveCursorToCurrentPoint()
{
    const EditedGoalPoint& point = mp_current->m_points[mp_current->m_currentPoint];
    Obj::CEditorCameraComponent* camera = Camera(mp_camera);
    camera->m_height = point.height;
    camera->m_heading = point.angle;
    camera->m_cursor = point.pos;
}

// 0x004E7FC0
void CGoalEditorComponent::RunCursorScript()
{
    Script::CStruct params;
    params.Init();
    Obj::CEditorCameraComponent* camera = Camera(mp_camera);
    const Mth::Vector& cursor = camera->m_cursor;
    params.AddVector(0x7F261953, cursor.x, cursor.y + camera->m_height, cursor.z);
    params.AddFloat(0xFF7EBAF6, camera->m_heading);
    Script::RunScript(0xDEA7FE56, &params, nullptr, false, nullptr);
    params.Clear();
}

// 0x004EA580
void CGoalEditorComponent::RefreshCursor()
{
    CEditedGoal* goal = mp_current;
    int kind = goal->m_numPoints ? goal->m_points[goal->m_currentPoint].kind : kPointPro;
    const char* model;
    if (kind == kPointStart) {
        model = "gameobjects\\p1_cursor\\p1_cursor.mdl";
    } else if (kind == kPointObject) {
        int letter = 0;
        if (goal->m_type == kGoalTypeCombo || goal->m_type == kGoalTypeSkate)
            letter = goal->m_currentPoint - 2;
        model = GoalObjectModel(goal->m_type, letter);
    } else {
        model = "gameobjects\\goal_cursor\\goal_cursor.mdl";
    }
    Script::CStruct params;
    params.Init();
    goal = mp_current;
    kind = goal->m_numPoints ? goal->m_points[goal->m_currentPoint].kind : kPointPro;
    if (kind == kPointPro)
        params.AddChecksum(0x7321A8D6, 0x061A741E);
    else if (kind == kPointCheckpoint)
        params.AddChecksum(0x7321A8D6, 0xF0FF0841);
    params.AddString(0x286A8D26, model);
    Script::RunScript(0x36791E3C, &params, m_object, false, nullptr);
    UpdateCameraCollision();
    params.Clear();
}

// 0x004EA6C0
void CGoalEditorComponent::NewGoal()
{
    ClearIncompleteGoals();
    uint32_t level = CurrentLevel();
    // The lowest number no complete goal of the level has.
    int index = 0;
    for (bool taken = true; taken;) {
        taken = false;
        for (CEditedGoal& goal : m_goals) {
            if (goal.m_used && goal.m_numPoints >= goal.m_minPoints && goal.m_level == level &&
                goal.m_goalIndex == index) {
                ++index;
                taken = true;
                break;
            }
        }
    }
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used)
            continue;
        mp_current = &goal;
        InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
        mp_current->m_used = true;
        mp_current->m_level = level;
        mp_current->m_goalIndex = index;
        RefreshCursor();
        return;
    }
}

// 0x004EA780
void CGoalEditorComponent::PlaceParkGoalsOnGround()
{
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == kParkEditorLevel)
            goal.PlacePointsOnGround();
    }
}

// 0x004EA470
void CGoalEditorComponent::ReadGoals(Script::CStruct* params, int parkOnly)
{
    if (parkOnly)
        ClearParkGoals();
    else
        ClearLevelGoals();
    Script::CArray* goals = nullptr;
    params->GetArray(kGoalsArray, &goals, 0);
    if (!goals)
        return;
    uint32_t count = goals->m_size;
    for (uint32_t i = 0; i < count; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(goals->GetElement(i));
        uint32_t level = 0;
        entry->GetChecksum(0x651533EC, &level, 0);
        bool park = level == kParkEditorGameplayLevel || level == kParkEditorLevel;
        if (park != (parkOnly != 0))
            continue;
        for (CEditedGoal& goal : m_goals) {
            if (!goal.m_used) {
                goal.ReadFromStructure(entry);
                break;
            }
        }
    }
    mp_current = nullptr;
}

// 0x004EA2C0
const uint8_t* CGoalEditorComponent::ReadLevelGoalsFromBuffer(uint32_t level, const uint8_t* buffer)
{
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    Script::CStruct* params = NewStructure();
    const uint8_t* end = Script::ReadFromBuffer(params, buffer);
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level == level)
            InitEditedGoal(reinterpret_cast<uint8_t*>(&goal));
    }
    mp_current = nullptr;
    Script::CArray* goals = nullptr;
    params->GetArray(kGoalsArray, &goals, 1);
    uint32_t left = goals->m_size;
    uint32_t next = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!left)
            break;
        if (goal.m_used)
            continue;
        goal.ReadFromStructure(reinterpret_cast<Script::CStruct*>(goals->GetElement(next++)));
        goal.m_level = level;
        --left;
    }
    DeleteStructure(params);
    return end;
}

// 0x004EB850
void CGoalEditorComponent::WriteGoals(Script::CStruct* out)
{
    uint32_t count = 0;
    for (CEditedGoal& goal : m_goals) {
        if (goal.m_used && goal.m_level != kParkEditorLevel)
            ++count;
    }
    if (!count)
        return;
    Script::CArray* goals = Script::NewArray();
    goals = goals ? goals->Init() : nullptr;
    goals->SetSizeAndType(count, Script::ESYMBOLTYPE_STRUCTURE);
    uint32_t index = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_used || goal.m_level == kParkEditorLevel)
            continue;
        Script::CStruct* entry = NewStructure();
        goal.WriteIntoStructure(entry);
        goals->SetElement(index++, reinterpret_cast<uint32_t>(entry));
    }
    out->AddArrayPointer(kGoalsArray, goals);
}

// 0x004EBA10
void CGoalEditorComponent::WriteLevelGoals(uint32_t level, Script::CStruct* out)
{
    // (sized by the complete goals, filled with every used one: as the original)
    uint32_t count = static_cast<uint32_t>(CountCompleteGoals(level));
    Script::CArray* goals = Script::NewArray();
    goals = goals ? goals->Init() : nullptr;
    goals->SetSizeAndType(count, Script::ESYMBOLTYPE_STRUCTURE);
    uint32_t index = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_used || goal.m_level != level)
            continue;
        Script::CStruct* entry = NewStructure();
        goal.WriteIntoStructure(entry);
        goals->SetElement(index++, reinterpret_cast<uint32_t>(entry));
    }
    out->AddArrayPointer(kGoalsArray, goals);
}

// 0x004EBB70
uint32_t CGoalEditorComponent::WriteLevelGoalsToBuffer(uint32_t level, uint8_t* buffer, uint32_t size)
{
    Script::CStruct* params = NewStructure();
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    WriteLevelGoals(level, params);
    uint32_t written = Script::WriteToBuffer(params, buffer, size, 1, nullptr);
    DeleteStructure(params);
    return written;
}

// 0x004EA7C0
bool CGoalEditorComponent::NewEditorGoal(Script::CStruct* /*params*/, Script::CScript* script)
{
    NewGoal();
    Sprintf(CreatedGoalName(), "CreatedGoal%d", mp_current->m_goalIndex);
    script->m_params->AddChecksum("goal_id", Crc::GenerateCRCFromString(CreatedGoalName()));
    return true;
}

namespace {

// Whether a point can't go at the cursor: the start too near another goal's pro, or the pro too near another
// goal's start.
bool NearOtherGoals(CGoalEditorComponent* editor, const Mth::Vector& pos, float height)
{
    CEditedGoal* goal = editor->mp_current;
    if (goal->m_numPoints && goal->m_points[goal->m_currentPoint].kind == kPointStart &&
        editor->PointNearAnyGoal(pos, height, goal->m_level, 0))
        return true;
    goal = editor->mp_current;
    if (!goal->m_numPoints || goal->m_points[goal->m_currentPoint].kind == kPointPro)
        return editor->PointNearAnyGoal(pos, height, goal->m_level, 1);
    return false;
}

Obj::CControlButton* Button(Obj::CBaseComponent* input, uint32_t offset)
{
    return reinterpret_cast<Obj::CControlButton*>(reinterpret_cast<uint8_t*>(input) + offset);
}

} // namespace

// 0x004EBD30
void CGoalEditorComponent::Update()
{
    if (!mp_current) {
        RunCursorScript();
        return;
    }
    Obj::CControlButton* place = Button(mp_input, 0x1C0);
    if (place->m_triggered) {
        place->ClearTrigger();
        Obj::CEditorCameraComponent* camera = Camera(mp_camera);
        Mth::Vector pos = camera->m_cursor;
        float height = camera->m_height, angle = camera->m_heading;
        if (NearOtherGoals(this, pos, height)) {
            Script::RunScript(0xAEBC7020, nullptr, m_object, false, nullptr);
        } else if (mp_current->PlacePoint(pos, height, angle)) {
            Script::RunScript(0x718B071B, nullptr, m_object, false, nullptr);
            RefreshCursor();
            MoveCursorToCurrentPoint();
        } else {
            Script::RunScript(0xAEBC7020, nullptr, m_object, false, nullptr);
        }
        if (mp_current->m_lastPointPlaced)
            Script::RunScript(0xC05E7E20, nullptr, m_object, false, nullptr);
    }
    Obj::CControlButton* back = Button(mp_input, 0x1A0);
    if (back->m_triggered) {
        back->ClearTrigger();
        Obj::CEditorCameraComponent* camera = Camera(mp_camera);
        Mth::Vector pos = camera->m_cursor;
        float height = camera->m_height, angle = camera->m_heading;
        if (!NearOtherGoals(this, pos, height) && mp_current->PlacePointAndStepBack(pos, height, angle)) {
            Script::RunScript(0xFA275CC5, nullptr, m_object, false, nullptr);
            RefreshCursor();
            MoveCursorToCurrentPoint();
        } else {
            Script::RunScript(0xADE100D6, nullptr, m_object, false, nullptr);
        }
    }
    RunCursorScript();
}

// 0x004EBC40
void CGoalEditorComponent::Hide(bool hide)
{
    if (hide) {
        Script::RunScript(0xFCC60D8B, nullptr, nullptr, false, nullptr);
        return;
    }
    Script::CStruct* params = NewStructure();
    params->AddInteger(0x67E6859A, 0);
    mp_input->InitFromStructure(params);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    RefreshCursor();
}

// 0x004EC010
bool CGoalEditorComponent::GetEditedGoalsInfo(Script::CStruct* params, Script::CScript* script)
{
    uint32_t level = 0;
    params->GetChecksum(0x651533EC, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    int count = CountCompleteGoals(level);
    if (!count)
        return true;
    Script::CArray* goals = Script::NewArray();
    goals = goals ? goals->Init() : nullptr;
    goals->SetSizeAndType(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_STRUCTURE);
    uint32_t index = 0;
    for (CEditedGoal& goal : m_goals) {
        if (!goal.m_used || (level && goal.m_level != level) || goal.m_numPoints < goal.m_minPoints)
            continue;
        Script::CStruct* entry = NewStructure();
        goal.WriteIntoStructure(entry);
        goals->SetElement(index++, reinterpret_cast<uint32_t>(entry));
    }
    script->m_params->AddArrayPointer("EditedGoalsInfo", goals);
    return true;
}

} // namespace Ed
