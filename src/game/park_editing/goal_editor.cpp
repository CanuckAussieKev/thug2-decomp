// The goal editor component (park_editing::ObjectiveAuthoring): see goal_editor.h. From the disassembly of 0x004E6FB0-
// 0x004EBC3D (the component's own code; the edited goals' is in edited_goal.cpp / edited_goal_io.cpp).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <gsl/pointers>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/format.h"
#include "engine/maths/vec4.h"
#include "engine/script/cell.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/goal_editor_util.h"
#include "game/goals/goal_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/editor_camera.h"
#include "engine/objects/input_component.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "game/skate/skate_module.h"
#include "platform/crt.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/park_editing_state.h"

namespace park_editing {

namespace {

constexpr std::uint32_t kAddEditedGoalToGoalManager = 0xB525B113u; // AddEditedGoalToGoalManager
constexpr std::uint32_t kAddKeyComboSet = 0xCCA11C78u; // AddKeyComboSet
constexpr std::uint32_t kAngle = 0xFF7EBAF6u; // Angle
constexpr std::uint32_t kCheckpoint = 0xF0FF0841u; // Checkpoint
constexpr std::uint32_t kEditGoal = 0x831ECA10u; // EditGoal
constexpr std::uint32_t kEditedGoalAddGap = 0x0AB710F3u; // EditedGoalAddGap
constexpr std::uint32_t kEditedGoalGotGap = 0xFB279560u; // EditedGoalGotGap
constexpr std::uint32_t kEditedGoalRemoveGap = 0x5C155A29u; // EditedGoalRemoveGap
constexpr std::uint32_t kFindUnfinishedGoal = 0x88D54A10u; // FindUnfinishedGoal
constexpr std::uint32_t kFlagGoalAsWon = 0x769309E1u; // FlagGoalAsWon
constexpr std::uint32_t kGoalEditor = 0x81F01058u; // GoalEditor
constexpr std::uint32_t kGetCurrentEditedGoalId = 0x5BD8050Au; // GetCurrentEditedGoalId
constexpr std::uint32_t kGetCurrentEditedGoalInfo = 0x69B872E3u; // GetCurrentEditedGoalInfo
constexpr std::uint32_t kGetEditedGoalsInfo = 0x96F11BD3u; // GetEditedGoalsInfo
constexpr std::uint32_t kGetKeyComboSet = 0xE5ED63B7u; // GetKeyComboSet
constexpr std::uint32_t kGetMaxGoalsPerLevel = 0xD7258335u; // GetMaxGoalsPerLevel
constexpr std::uint32_t kGetNumEditedGoals = 0x96874358u; // GetNumEditedGoals
constexpr std::uint32_t kGoalEditorCreateCursor = 0x36791E3Cu; // goal_editor_create_cursor
constexpr std::uint32_t kGoalEditorDestroyCursor = 0xFCC60D8Bu; // goal_editor_destroy_cursor
constexpr std::uint32_t kGoalEditorFinishedPlacingObjects = 0xC05E7E20u; // goal_editor_finished_placing_objects
constexpr std::uint32_t kGoalEditorPlayBackupFailSound = 0xADE100D6u; // goal_editor_play_backup_fail_sound
constexpr std::uint32_t kGoalEditorPlayBackupSuccessSound = 0xFA275CC5u; // goal_editor_play_backup_success_sound
constexpr std::uint32_t kGoalEditorPlayPlacementFailSound = 0xAEBC7020u; // goal_editor_play_placement_fail_sound
constexpr std::uint32_t kGoalEditorPlayPlacementSuccessSound = 0x718B071Bu; // goal_editor_play_placement_success_sound
constexpr std::uint32_t kGoalEditorUpdateCursorPosition = 0xDEA7FE56u; // goal_editor_update_cursor_position
constexpr std::uint32_t kGoalExists = 0xFF9E8752u; // GoalExists
constexpr std::uint32_t kGoalHasAllPositionsSet = 0x26848221u; // GoalHasAllPositionsSet
constexpr std::uint32_t kGoalId = 0x9982E501u; // Goal_Id
constexpr std::uint32_t kGoalIndex = 0x474A6A7Fu; // goal_index
constexpr std::uint32_t kGoalName = 0xBFECC45Bu; // goal_name
constexpr std::uint32_t kLevel = 0x651533ECu; // Level
constexpr std::uint32_t kMaxEditedGoalsReached = 0x60288ABEu; // MaxEditedGoalsReached
constexpr std::uint32_t kModel = 0x286A8D26u; // Model
constexpr std::uint32_t kNewEditorGoal = 0x259C59DDu; // NewEditorGoal
constexpr std::uint32_t kNukeAllGoals = 0xDBA8E3FCu; // NukeAllGoals
constexpr std::uint32_t kPed = 0x061A741Eu; // Ped
constexpr std::uint32_t kPlayer = 0x67E6859Au; // Player
constexpr std::uint32_t kPos = 0x7F261953u; // Pos
constexpr std::uint32_t kRefreshGoalCursorPosition = 0x20B566B1u; // RefreshGoalCursorPosition
constexpr std::uint32_t kRemoveGoalSpecificFlag = 0x03EBD2C9u; // RemoveGoalSpecificFlag
constexpr std::uint32_t kRemoveKeyComboSet = 0x1D30A7B6u; // RemoveKeyComboSet
constexpr std::uint32_t kRemovedCreatedGoal = 0x1F80B5AEu; // RemovedCreatedGoal
constexpr std::uint32_t kSetCurrentEditorGoal = 0xFDB2C514u; // SetCurrentEditorGoal
constexpr std::uint32_t kSetEditorGoalDescription = 0x66A33157u; // SetEditorGoalDescription
constexpr std::uint32_t kSetEditorGoalName = 0x4A569426u; // SetEditorGoalName
constexpr std::uint32_t kSetEditorGoalType = 0x98ABBD09u; // SetEditorGoalType
constexpr std::uint32_t kSetEditorGoalWinMessage = 0x7AABBC09u; // SetEditorGoalWinMessage
constexpr std::uint32_t kSetEditorPedName = 0x410821DFu; // SetEditorPedName
constexpr std::uint32_t kSetGoalControlType = 0xEC316A9Cu; // SetGoalControlType
constexpr std::uint32_t kSetGoalScore = 0x9E5A634Au; // SetGoalScore
constexpr std::uint32_t kSetGoalSpecificParams = 0x47208C6Du; // SetGoalSpecificParams
constexpr std::uint32_t kSetGoalTimeLimit = 0x7D5073F6u; // SetGoalTimeLimit
constexpr std::uint32_t kType = 0x7321A8D6u; // Type
constexpr std::uint32_t kWriteEditedGoalNodePositions = 0x5408AC04u; // WriteEditedGoalNodePositions
constexpr std::uint32_t kMemberFunction36 = 0xD5F6EA3Bu; // the table's 35th member function (text unknown)
constexpr int kPadOffsetInModule = 0x20;     // the pad inside the input component
constexpr int kPlaceButtonOffset = 0x1C0;    // the place button, as an offset into the input component
constexpr int kBackButtonOffset = 0x1A0;     // the back button

} // namespace

std::array<script::MemberFunctionEntry, kMemberFunctionCount>& goal_editor_member_functions()
{
    static std::array<script::MemberFunctionEntry, kMemberFunctionCount> goal_editor_member_functions{{
        {.callsign_ = kEditedGoalAddGap, .owner_kind_ = kGoalEditor}, {.callsign_ = kEditedGoalRemoveGap, .owner_kind_ = kGoalEditor}, {.callsign_ = kEditedGoalGotGap, .owner_kind_ = kGoalEditor}, {.callsign_ = kAddKeyComboSet, .owner_kind_ = kGoalEditor},
        {.callsign_ = kRemoveKeyComboSet, .owner_kind_ = kGoalEditor}, {.callsign_ = kGetKeyComboSet, .owner_kind_ = kGoalEditor}, {.callsign_ = kRemoveGoalSpecificFlag, .owner_kind_ = kGoalEditor}, {.callsign_ = kSetGoalSpecificParams, .owner_kind_ = kGoalEditor},
        {.callsign_ = kFlagGoalAsWon, .owner_kind_ = kGoalEditor}, {.callsign_ = kFindUnfinishedGoal, .owner_kind_ = kGoalEditor}, {.callsign_ = kGoalExists, .owner_kind_ = kGoalEditor}, {.callsign_ = kNewEditorGoal, .owner_kind_ = kGoalEditor},
        {.callsign_ = kRemovedCreatedGoal, .owner_kind_ = kGoalEditor}, {.callsign_ = kGoalHasAllPositionsSet, .owner_kind_ = kGoalEditor}, {.callsign_ = kSetCurrentEditorGoal, .owner_kind_ = kGoalEditor}, {.callsign_ = kSetEditorGoalType, .owner_kind_ = kGoalEditor},
        {.callsign_ = kSetEditorGoalDescription, .owner_kind_ = kGoalEditor}, {.callsign_ = kSetEditorGoalWinMessage, .owner_kind_ = kGoalEditor}, {.callsign_ = kGetCurrentEditedGoalInfo, .owner_kind_ = kGoalEditor}, {.callsign_ = kGetEditedGoalsInfo, .owner_kind_ = kGoalEditor},
        {.callsign_ = kGetNumEditedGoals, .owner_kind_ = kGoalEditor}, {.callsign_ = kGetCurrentEditedGoalId, .owner_kind_ = kGoalEditor}, {.callsign_ = kWriteEditedGoalNodePositions, .owner_kind_ = kGoalEditor}, {.callsign_ = kAddEditedGoalToGoalManager, .owner_kind_ = kGoalEditor},
        {.callsign_ = kSetEditorGoalName, .owner_kind_ = kGoalEditor}, {.callsign_ = kSetEditorPedName, .owner_kind_ = kGoalEditor}, {.callsign_ = kMaxEditedGoalsReached, .owner_kind_ = kGoalEditor}, {.callsign_ = kEditGoal, .owner_kind_ = kGoalEditor},
        {.callsign_ = kNukeAllGoals, .owner_kind_ = kGoalEditor}, {.callsign_ = kGetMaxGoalsPerLevel, .owner_kind_ = kGoalEditor}, {.callsign_ = kSetGoalScore, .owner_kind_ = kGoalEditor}, {.callsign_ = kSetGoalTimeLimit, .owner_kind_ = kGoalEditor},
        {.callsign_ = kSetGoalControlType, .owner_kind_ = kGoalEditor}, {.callsign_ = kRefreshGoalCursorPosition, .owner_kind_ = kGoalEditor}, {.callsign_ = kMemberFunction36, .owner_kind_ = kGoalEditor}, {.callsign_ = 0, .owner_kind_ = kGoalEditor}}};
    return goal_editor_member_functions;
}

namespace {

constexpr uint32_t kInput = 0x27D7CD28, kEditorCamera = 0xAC8946E2;
constexpr uint32_t kGoalListName = 0x38DBE1D0;
constexpr uint32_t kParkEditorGameplayLevel = 0xB664035D; // Load_Sk5Ed_gameplay

template <typename... Args>
int get_sprintf(char* out, text::FormatFor<Args...> format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}
// The game's _stricmp (0x00626EFB).
int compare_no_case(const char* a, const char* b) { return crt::stricmp(a, b); }

uint32_t created_goal_id(int index)
{
    get_sprintf(park_editing_state().created_goal_name().data(), "CreatedGoal%d", index);
    return checksums::hash_from_text(park_editing_state().created_goal_name().data());
}

// The current level, the park editor's gameplay level counting as the park editor's.
uint32_t current_level()
{
    const uint32_t level = modules::Skate::sole()->level();
    return level == kParkEditorGameplayLevel ? kParkEditorLevel : level;
}

objects::EditorViewModule* view_cam(objects::ObjectModule* camera)
{
    return core::down_cast<objects::EditorViewModule>(camera);
}

// Whether `x`, `z` lies outside x0..x1, z0..z1 (not a number: inside, as the original's comparisons).
bool get_outside(float x, float z, float x0, float z0, float x1, float z1)
{
    return x < x0 || x > x1 || z < z0 || z > z1;
}

} // namespace

// 0x004EB7F0
gsl::owner<objects::ObjectModule*> ObjectiveAuthoring::fabricate() { return new ObjectiveAuthoring; }

// 0x004E6FB0
void ObjectiveAuthoring::publish_method_table()
{
    script::register_member_function_table(goal_editor_member_functions());
}

// 0x004EA230
// (the goals made first by their constructors)
ObjectiveAuthoring::ObjectiveAuthoring()
{
    set_kind_tag(kGoalEditor);
    clear_level_goals();
    clear_park_goals();
}

// 0x004E6FD0 (0x004E7B20 the deleting one)
ObjectiveAuthoring::~ObjectiveAuthoring()
{
    script::launch_script(kGoalEditorDestroyCursor, nullptr, nullptr, false, nullptr);
    // (the goals' destructors are empty)
}

// 0x004E8210
void ObjectiveAuthoring::seal()
{
    input_ptr_ = entity()->fetch_module(kInput);
    view_camera_ = entity()->fetch_module(kEditorCamera);
}

// 0x004E8470
ObjectiveAuthoring* ObjectiveAuthoring::get_pull()
{
    auto* manager = static_cast<objects::ObjectHubBase*>(objects::object_hub_slot().get());
    auto* object = core::down_cast<objects::GameEntity>(manager->look_up_entity(kGoalEditor));
    return core::down_cast<ObjectiveAuthoring>(object->fetch_module(kGoalEditor));
}

// 0x004E7BE0
EditedGoal* ObjectiveAuthoring::find_goal_by_name(const char* name)
{
    const uint32_t level = current_level();
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == level && compare_no_case(goal.label().data(), name) == 0)
            return &goal;
    }
    return nullptr;
}

// 0x004E7C60
EditedGoal* ObjectiveAuthoring::find_goal_by_id(uint32_t id)
{
    const uint32_t level = current_level();
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == level && created_goal_id(goal.level_goal_number()) == id)
            return &goal;
    }
    return nullptr;
}

// 0x004E7D00
EditedGoal* ObjectiveAuthoring::get_goal_from_params(script::Struct* params)
{
    uint32_t id = 0;
    if (params->read_hash_value(kGoalId, &id, 0))
        return find_goal_by_id(id);
    const char* name = nullptr;
    if (params->fetch_text(kGoalName, &name, 0))
        return find_goal_by_name(name);
    int index = 0;
    if (params->fetch_int_value(kGoalIndex, &index, 0)) {
        const uint32_t level = current_level();
        for (EditedGoal& goal : goals_) {
            if (goal.taken_count() && goal.level() == level && goal.level_goal_number() == index)
                return &goal;
        }
        return nullptr;
    }
    return active_node_;
}

// 0x004E7E00
int ObjectiveAuthoring::count_used_goals()
{
    int count = 0;
    for (const EditedGoal& goal : goals_) {
        if (goal.taken_count())
            ++count;
    }
    return count;
}

// 0x004E7E50
int ObjectiveAuthoring::count_complete_goals(uint32_t level)
{
    int count = 0;
    for (const EditedGoal& goal : goals_) {
        if (goal.taken_count() && (!level || goal.level() == level) && goal.vertex_total() >= goal.min_points())
            ++count;
    }
    return count;
}

// 0x004E8130
bool ObjectiveAuthoring::point_near_any_goal(const maths::Vec4& pos, float height, checksums::Name level_name, int index)
{
    const uint32_t level = level_name.value();
    const float x = pos.x;
    const float y = pos.y + height;
    const float z = pos.z;
    const float radius = script::fetch_float(0x71B20276);
    for (const EditedGoal& goal : goals_) {
        if (!goal.taken_count() || goal.level() != level || std::cmp_less_equal(goal.vertex_total(), index))
            continue;
        const EditedGoalPoint& point = core::at(goal.path_vertices(), index);
        // (x's difference rounded to a float; y's and z's not, as the original's x87 sums)
        auto dx = static_cast<float>(static_cast<double>(point.pos.x) - x);
        const double dy = (static_cast<double>(point.pos.y) + point.height) - y;
        const double dz = static_cast<double>(point.pos.z) - z;
        if (std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx) < radius)
            return true;
    }
    return false;
}

// 0x004E8240
void ObjectiveAuthoring::clear_level_goals()
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() != kParkEditorLevel)
            init_edited_goal(&goal);
    }
    active_node_ = nullptr;
}

// 0x004E8280
void ObjectiveAuthoring::clear_park_goals()
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == kParkEditorLevel)
            init_edited_goal(&goal);
    }
    active_node_ = nullptr;
}

// 0x004E82C0
void ObjectiveAuthoring::clear_incomplete_goals()
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.vertex_total() < goal.min_points())
            init_edited_goal(&goal);
    }
}

// 0x004E8300
bool ObjectiveAuthoring::any_park_goal_outside(float x0, float z0, float x1, float z1)
{
    for (const EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == kParkEditorLevel && goal.vertex_total() > 1 &&
            get_outside(core::at(goal.path_vertices(), 1).pos.x, core::at(goal.path_vertices(), 1).pos.z, x0, z0, x1, z1))
            return true;
    }
    return false;
}

// 0x004E8390
void ObjectiveAuthoring::remove_park_goals_outside(float x0, float z0, float x1, float z1)
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == kParkEditorLevel && goal.vertex_total() > 1 &&
            get_outside(core::at(goal.path_vertices(), 1).pos.x, core::at(goal.path_vertices(), 1).pos.z, x0, z0, x1, z1))
            drop_objective(&goal);
    }
}

// 0x004E8420
void ObjectiveAuthoring::remove_gap_from_park_goals(int gap)
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == kParkEditorLevel && goal.kind_tag() == kGoalTypeGap)
            goal.remove_gap_and_renumber(static_cast<uint8_t>(gap));
    }
}

// 0x004E8090
void ObjectiveAuthoring::drop_objective(EditedGoal* goal)
{
    if (goal->taken_count()) {
        goals::GoalManager* manager = goals::get_objective_board();
        if (manager->goal_exists(created_goal_id(goal->level_goal_number())))
            manager->drop_objective(created_goal_id(goal->level_goal_number()));
    }
    init_edited_goal(goal);
    if (active_node_ == goal)
        active_node_ = nullptr;
}

// 0x004E7B50
void ObjectiveAuthoring::update_camera_collision()
{
    const EditedGoal* goal = active_node_;
    const int kind = goal->vertex_total() ? core::at(goal->path_vertices(), goal->current_point()).kind : kPointPro;
    if (kind != kPointPro && kind != kPointStart) {
        view_cam(view_camera_)->set_simple_collision(true);
        return;
    }
    view_cam(view_camera_)->set_simple_collision(modules::Skate::sole()->level() == kParkEditorLevel);
}

// 0x004E7F30
void ObjectiveAuthoring::move_cursor_to_current_point()
{
    const EditedGoalPoint& point = core::at(active_node_->path_vertices(), active_node_->current_point());
    objects::EditorViewModule* camera = view_cam(view_camera_);
    camera->set_level_y(point.height);
    camera->set_heading(point.angle);
    camera->cursor() = point.pos;
}

// 0x004E7FC0
void ObjectiveAuthoring::run_cursor_script()
{
    script::Struct params;
    const objects::EditorViewModule* camera = view_cam(view_camera_);
    const maths::Vec4& cursor = camera->cursor();
    params.put_vec3(checksums::Name{kPos}, {.x = cursor.x, .y = cursor.y + camera->level_y(), .z = cursor.z});
    params.push_float(checksums::Name{kAngle}, camera->heading());
    script::launch_script(kGoalEditorUpdateCursorPosition, &params, nullptr, false, nullptr);
    params.get_clear();
}

// 0x004EA580
void ObjectiveAuthoring::refresh_cursor()
{
    const EditedGoal* goal = active_node_;
    int kind = goal->vertex_total() ? core::at(goal->path_vertices(), goal->current_point()).kind : kPointPro;
    const char* model = nullptr;
    if (kind == kPointStart) {
        model = "gameobjects\\p1_cursor\\p1_cursor.mdl";
    } else if (kind == kPointObject) {
        int letter = 0;
        if (goal->kind_tag() == kGoalTypeCombo || goal->kind_tag() == kGoalTypeSkate)
            letter = goal->current_point() - 2;
        model = goal_object_model(checksums::Name{goal->kind_tag()}, letter);
    } else {
        model = "gameobjects\\goal_cursor\\goal_cursor.mdl";
    }
    script::Struct params;
    goal = active_node_;
    kind = goal->vertex_total() ? core::at(goal->path_vertices(), goal->current_point()).kind : kPointPro;
    if (kind == kPointPro)
        params.put_name_key(checksums::Name{kType}, kPed);
    else if (kind == kPointCheckpoint)
        params.put_name_key(checksums::Name{kType}, kCheckpoint);
    params.append_word(kModel, model);
    script::launch_script(kGoalEditorCreateCursor, &params, entity(), false, nullptr);
    update_camera_collision();
    params.get_clear();
}

// 0x004EA6C0
void ObjectiveAuthoring::begin_fresh_objective()
{
    clear_incomplete_goals();
    const uint32_t level = current_level();
    // The lowest number no complete goal of the level has.
    int index = 0;
    for (bool taken = true; taken;) {
        taken = false;
        for (const EditedGoal& goal : goals_) {
            if (goal.taken_count() && goal.vertex_total() >= goal.min_points() && goal.level() == level &&
                goal.level_goal_number() == index) {
                ++index;
                taken = true;
                break;
            }
        }
    }
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count())
            continue;
        active_node_ = &goal;
        init_edited_goal(&goal);
        active_node_->set_taken_count(true);
        active_node_->set_level(level);
        active_node_->set_level_goal_number(index);
        refresh_cursor();
        return;
    }
}

// 0x004EA780
void ObjectiveAuthoring::place_park_goals_on_ground()
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == kParkEditorLevel)
            goal.place_points_on_ground();
    }
}

// 0x004EA470
void ObjectiveAuthoring::read_goals(script::Struct* params, int park_only)
{
    if (park_only)
        clear_park_goals();
    else
        clear_level_goals();
    script::Array* goals = nullptr;
    params->fetch_list(kGoalListName, &goals, 0);
    if (!goals)
        return;
    const uint32_t count = goals->extent();
    for (uint32_t i = 0; i < count; ++i) {
        auto* entry = goals->struct_at(i);
        uint32_t level = 0;
        entry->read_hash_value(kLevel, &level, 0);
        const bool park = level == kParkEditorGameplayLevel || level == kParkEditorLevel;
        if (park != (park_only != 0))
            continue;
        for (EditedGoal& goal : goals_) {
            if (!goal.taken_count()) {
                goal.decode_from_struct(entry);
                break;
            }
        }
    }
    active_node_ = nullptr;
}

// 0x004EA2C0
const uint8_t* ObjectiveAuthoring::read_level_goals_from_buffer(uint32_t level, const uint8_t* buffer, size_t size)
{
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    gsl::owner<script::Struct*> params = script::new_struct();
    const uint8_t* end = script::decode_from_bytes(params, buffer, size);
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() == level)
            init_edited_goal(&goal);
    }
    active_node_ = nullptr;
    script::Array* goals = nullptr;
    params->fetch_list(kGoalListName, &goals, 1);
    uint32_t left = goals ? goals->extent() : 0; // (Not original: none - the original read through null)
    uint32_t next = 0;
    for (EditedGoal& goal : goals_) {
        if (!left)
            break;
        if (goal.taken_count())
            continue;
        goal.decode_from_struct(goals->struct_at(next++));
        goal.set_level(level);
        --left;
    }
    script::free_struct(params);
    return end;
}

// 0x004EB850
void ObjectiveAuthoring::write_goals(script::Struct* out)
{
    uint32_t count = 0;
    for (const EditedGoal& goal : goals_) {
        if (goal.taken_count() && goal.level() != kParkEditorLevel)
            ++count;
    }
    if (!count)
        return;
    gsl::owner<script::Array*> goals = script::new_array();
    goals->configure_size_kind(count, script::kQbvalStruct);
    uint32_t index = 0;
    for (EditedGoal& goal : goals_) {
        if (!goal.taken_count() || goal.level() == kParkEditorLevel)
            continue;
        gsl::owner<script::Struct*> entry = script::new_struct();
        goal.export_to_struct(entry);
        goals->set_cell(index++, script::Cell::of(entry));
    }
    out->append_array_ref(kGoalListName, goals);
}

// 0x004EBA10
void ObjectiveAuthoring::write_level_goals(uint32_t level, script::Struct* out)
{
    // (sized by the complete goals, filled with every used one: as the original)
    auto count = static_cast<uint32_t>(count_complete_goals(level));
    gsl::owner<script::Array*> goals = script::new_array();
    goals->configure_size_kind(count, script::kQbvalStruct);
    uint32_t index = 0;
    for (EditedGoal& goal : goals_) {
        if (!goal.taken_count() || goal.level() != level)
            continue;
        gsl::owner<script::Struct*> entry = script::new_struct();
        goal.export_to_struct(entry);
        goals->set_cell(index++, script::Cell::of(entry));
    }
    out->append_array_ref(kGoalListName, goals);
}

// 0x004EBB70
uint32_t ObjectiveAuthoring::write_level_goals_to_buffer(uint32_t level, uint8_t* buffer, uint32_t size)
{
    script::StructPtr params = script::make_struct();
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    write_level_goals(level, params.get());
    const uint32_t written = script::emit_into_buffer(params.get(), buffer, size, true, nullptr);
    params.reset();
    return written;
}

// 0x004EA7C0
bool ObjectiveAuthoring::new_editor_goal(script::Struct* /*params*/, script::Script* script)
{
    begin_fresh_objective();
    get_sprintf(park_editing_state().created_goal_name().data(), "CreatedGoal%d", active_node_->level_goal_number());
    script->args()->put_name_key("goal_id", checksums::hash_from_text(park_editing_state().created_goal_name().data()));
    return true;
}

namespace {

// Whether a point can't go at the cursor: the start too near another goal's pro, or the pro too near another
// goal's start.
bool near_other_goals(ObjectiveAuthoring* editor, const maths::Vec4& pos, float height)
{
    const EditedGoal* goal = editor->active_node();
    if (goal->vertex_total() && core::at(goal->path_vertices(), goal->current_point()).kind == kPointStart &&
        editor->point_near_any_goal(pos, height, checksums::Name{goal->level()}, 0))
        return true;
    goal = editor->active_node();
    if (!goal->vertex_total() || core::at(goal->path_vertices(), goal->current_point()).kind == kPointPro)
        return editor->point_near_any_goal(pos, height, checksums::Name{goal->level()}, 1);
    return false;
}

// The input component's button `offset` bytes into the component (its pad at +0x20).
objects::ControlButton* get_button(objects::ObjectModule* input, uint32_t offset)
{
    return core::down_cast<objects::PadInputModule>(input)->pad_primary().button_at_offset(offset - kPadOffsetInModule);
}

} // namespace

// 0x004EBD30
void ObjectiveAuthoring::tick_frame()
{
    if (!active_node_) {
        run_cursor_script();
        return;
    }
    objects::ControlButton* place = get_button(input_ptr_, kPlaceButtonOffset);
    if (place->fired()) {
        place->reset_activation();
        const objects::EditorViewModule* camera = view_cam(view_camera_);
        const maths::Vec4 pos = camera->cursor();
        const float height = camera->level_y();
        const float angle = camera->heading();
        // (placed only away from the other goals)
        if (!near_other_goals(this, pos, height) && active_node_->place_point(pos, {.height = height, .angle = angle})) {
            script::launch_script(kGoalEditorPlayPlacementSuccessSound, nullptr, entity(), false, nullptr);
            refresh_cursor();
            move_cursor_to_current_point();
        } else {
            script::launch_script(kGoalEditorPlayPlacementFailSound, nullptr, entity(), false, nullptr);
        }
        if (active_node_->last_point_placed())
            script::launch_script(kGoalEditorFinishedPlacingObjects, nullptr, entity(), false, nullptr);
    }
    objects::ControlButton* back = get_button(input_ptr_, kBackButtonOffset);
    if (back->fired()) {
        back->reset_activation();
        const objects::EditorViewModule* camera = view_cam(view_camera_);
        const maths::Vec4 pos = camera->cursor();
        const float height = camera->level_y();
        const float angle = camera->heading();
        if (!near_other_goals(this, pos, height) && active_node_->place_point_and_step_back(pos, {.height = height, .angle = angle})) {
            script::launch_script(kGoalEditorPlayBackupSuccessSound, nullptr, entity(), false, nullptr);
            refresh_cursor();
            move_cursor_to_current_point();
        } else {
            script::launch_script(kGoalEditorPlayBackupFailSound, nullptr, entity(), false, nullptr);
        }
    }
    run_cursor_script();
}

// 0x004EBC40
void ObjectiveAuthoring::conceal(bool hide)
{
    if (hide) {
        script::launch_script(kGoalEditorDestroyCursor, nullptr, nullptr, false, nullptr);
        return;
    }
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_int(checksums::Name{kPlayer}, 0);
    input_ptr_->configure_from_data(params);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    refresh_cursor();
}

// 0x004EC010
bool ObjectiveAuthoring::get_edited_goals_info(script::Struct* params, script::Script* script)
{
    uint32_t level = 0;
    params->read_hash_value(kLevel, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    const int count = count_complete_goals(level);
    if (!count)
        return true;
    gsl::owner<script::Array*> goals = script::new_array();
    goals->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalStruct);
    uint32_t index = 0;
    for (EditedGoal& goal : goals_) {
        if (!goal.taken_count() || (level && goal.level() != level) || goal.vertex_total() < goal.min_points())
            continue;
        gsl::owner<script::Struct*> entry = script::new_struct();
        goal.export_to_struct(entry);
        goals->set_cell(index++, script::Cell::of(entry));
    }
    script->args()->append_array_ref("EditedGoalsInfo", goals);
    return true;
}

} // namespace park_editing
