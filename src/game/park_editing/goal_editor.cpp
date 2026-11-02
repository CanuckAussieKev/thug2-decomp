// The goal editor component (park_editing::ObjectiveAuthoring): see goal_editor.h. From the disassembly of 0x004E6FB0-
// 0x004EBC3D (the component's own code; the edited goals' is in edited_goal.cpp / edited_goal_io.cpp).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <gsl/pointers>
#include <array>

#include "engine/core/checksums.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
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

namespace park_editing {

std::array<script::MemberFunctionEntry, 36> g_goal_editor_member_functions{{
    {0x0AB710F3, 0x81F01058}, {0x5C155A29, 0x81F01058}, {0xFB279560, 0x81F01058}, {0xCCA11C78, 0x81F01058},
    {0x1D30A7B6, 0x81F01058}, {0xE5ED63B7, 0x81F01058}, {0x03EBD2C9, 0x81F01058}, {0x47208C6D, 0x81F01058},
    {0x769309E1, 0x81F01058}, {0x88D54A10, 0x81F01058}, {0xFF9E8752, 0x81F01058}, {0x259C59DD, 0x81F01058},
    {0x1F80B5AE, 0x81F01058}, {0x26848221, 0x81F01058}, {0xFDB2C514, 0x81F01058}, {0x98ABBD09, 0x81F01058},
    {0x66A33157, 0x81F01058}, {0x7AABBC09, 0x81F01058}, {0x69B872E3, 0x81F01058}, {0x96F11BD3, 0x81F01058},
    {0x96874358, 0x81F01058}, {0x5BD8050A, 0x81F01058}, {0x5408AC04, 0x81F01058}, {0xB525B113, 0x81F01058},
    {0x4A569426, 0x81F01058}, {0x410821DF, 0x81F01058}, {0x60288ABE, 0x81F01058}, {0x831ECA10, 0x81F01058},
    {0xDBA8E3FC, 0x81F01058}, {0xD7258335, 0x81F01058}, {0x9E5A634A, 0x81F01058}, {0x7D5073F6, 0x81F01058},
    {0xEC316A9C, 0x81F01058}, {0x20B566B1, 0x81F01058}, {0xD5F6EA3B, 0x81F01058}, {0, 0x81F01058}}};

namespace {

constexpr uint32_t kGoalEditor = 0x81F01058;
constexpr uint32_t kInput = 0x27D7CD28, kEditorCamera = 0xAC8946E2;
constexpr uint32_t kGoalListName = 0x38DBE1D0;
constexpr uint32_t kParkEditorGameplayLevel = 0xB664035D; // Load_Sk5Ed_gameplay

template <typename... Args>
int get_sprintf(char* out, const char* format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}
// The game's _stricmp (0x00626EFB).
int compare_no_case(const char* a, const char* b) { return crt::stricmp(a, b); }

char* created_goal_name() { return g_created_goal_name.data(); }
uint32_t created_goal_id(int index)
{
    get_sprintf(created_goal_name(), "CreatedGoal%d", index);
    return checksums::hash_from_text(created_goal_name());
}

// The current level, the park editor's gameplay level counting as the park editor's.
uint32_t current_level()
{
    const uint32_t level = modules::Skate::sole()->level_;
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
    script::register_member_function_table(g_goal_editor_member_functions);
}

// 0x004EA230
// (the goals made first by their constructors)
ObjectiveAuthoring::ObjectiveAuthoring()
{
    kind_tag_ = kGoalEditor;
    clear_level_goals();
    clear_park_goals();
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    input_ptr_ = nullptr;
    view_camera_ = nullptr;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x004E6FD0 (0x004E7B20 the deleting one)
ObjectiveAuthoring::~ObjectiveAuthoring()
{
    script::launch_script(0xFCC60D8B, nullptr, nullptr, false, nullptr);
    // (the goals' destructors are empty)
}

// 0x004E8210
void ObjectiveAuthoring::seal()
{
    input_ptr_ = entity_->fetch_module(kInput);
    view_camera_ = entity_->fetch_module(kEditorCamera);
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
        if (goal.taken_count_ && goal.level_ == level && compare_no_case(goal.label_.data(), name) == 0)
            return &goal;
    }
    return nullptr;
}

// 0x004E7C60
EditedGoal* ObjectiveAuthoring::find_goal_by_id(uint32_t id)
{
    const uint32_t level = current_level();
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count_ && goal.level_ == level && created_goal_id(goal.level_goal_number_) == id)
            return &goal;
    }
    return nullptr;
}

// 0x004E7D00
EditedGoal* ObjectiveAuthoring::get_goal_from_params(script::Struct* params)
{
    uint32_t id = 0;
    if (params->read_hash_value(0x9982E501, &id, 0))
        return find_goal_by_id(id);
    const char* name = nullptr;
    if (params->fetch_text(0xBFECC45B, &name, 0))
        return find_goal_by_name(name);
    int index = 0;
    if (params->fetch_int_value(0x474A6A7F, &index, 0)) {
        const uint32_t level = current_level();
        for (EditedGoal& goal : goals_) {
            if (goal.taken_count_ && goal.level_ == level && goal.level_goal_number_ == index)
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
        if (goal.taken_count_)
            ++count;
    }
    return count;
}

// 0x004E7E50
int ObjectiveAuthoring::count_complete_goals(uint32_t level)
{
    int count = 0;
    for (const EditedGoal& goal : goals_) {
        if (goal.taken_count_ && (!level || goal.level_ == level) && goal.vertex_total_ >= goal.min_points_)
            ++count;
    }
    return count;
}

// 0x004E8130
bool ObjectiveAuthoring::point_near_any_goal(const maths::Vec4& pos, float height, uint32_t level, int index)
{
    const float x = pos.x;
    const float y = pos.y + height;
    const float z = pos.z;
    const float radius = script::fetch_float(0x71B20276);
    for (const EditedGoal& goal : goals_) {
        if (!goal.taken_count_ || goal.level_ != level || std::cmp_less_equal(goal.vertex_total_, index))
            continue;
        const EditedGoalPoint& point = core::at(goal.path_vertices_, index);
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
        if (goal.taken_count_ && goal.level_ != kParkEditorLevel)
            init_edited_goal(&goal);
    }
    active_node_ = nullptr;
}

// 0x004E8280
void ObjectiveAuthoring::clear_park_goals()
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count_ && goal.level_ == kParkEditorLevel)
            init_edited_goal(&goal);
    }
    active_node_ = nullptr;
}

// 0x004E82C0
void ObjectiveAuthoring::clear_incomplete_goals()
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count_ && goal.vertex_total_ < goal.min_points_)
            init_edited_goal(&goal);
    }
}

// 0x004E8300
bool ObjectiveAuthoring::any_park_goal_outside(float x0, float z0, float x1, float z1)
{
    for (const EditedGoal& goal : goals_) {
        if (goal.taken_count_ && goal.level_ == kParkEditorLevel && goal.vertex_total_ > 1 &&
            get_outside(core::at(goal.path_vertices_, 1).pos.x, core::at(goal.path_vertices_, 1).pos.z, x0, z0, x1, z1))
            return true;
    }
    return false;
}

// 0x004E8390
void ObjectiveAuthoring::remove_park_goals_outside(float x0, float z0, float x1, float z1)
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count_ && goal.level_ == kParkEditorLevel && goal.vertex_total_ > 1 &&
            get_outside(core::at(goal.path_vertices_, 1).pos.x, core::at(goal.path_vertices_, 1).pos.z, x0, z0, x1, z1))
            drop_objective(&goal);
    }
}

// 0x004E8420
void ObjectiveAuthoring::remove_gap_from_park_goals(int gap)
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count_ && goal.level_ == kParkEditorLevel && goal.kind_tag_ == kGoalTypeGap)
            goal.remove_gap_and_renumber(static_cast<uint8_t>(gap));
    }
}

// 0x004E8090
void ObjectiveAuthoring::drop_objective(EditedGoal* goal)
{
    if (goal->taken_count_) {
        goals::GoalManager* manager = goals::get_objective_board();
        if (manager->goal_exists(created_goal_id(goal->level_goal_number_)))
            manager->drop_objective(created_goal_id(goal->level_goal_number_));
    }
    init_edited_goal(goal);
    if (active_node_ == goal)
        active_node_ = nullptr;
}

// 0x004E7B50
void ObjectiveAuthoring::update_camera_collision()
{
    const EditedGoal* goal = active_node_;
    const int kind = goal->vertex_total_ ? core::at(goal->path_vertices_, goal->current_point_).kind : kPointPro;
    if (kind != kPointPro && kind != kPointStart) {
        view_cam(view_camera_)->simple_collision_ = true;
        return;
    }
    view_cam(view_camera_)->simple_collision_ = modules::Skate::sole()->level_ == kParkEditorLevel;
}

// 0x004E7F30
void ObjectiveAuthoring::move_cursor_to_current_point()
{
    const EditedGoalPoint& point = core::at(active_node_->path_vertices_, active_node_->current_point_);
    objects::EditorViewModule* camera = view_cam(view_camera_);
    camera->level_y_ = point.height;
    camera->heading_ = point.angle;
    camera->cursor_ = point.pos;
}

// 0x004E7FC0
void ObjectiveAuthoring::run_cursor_script()
{
    script::Struct params;
    const objects::EditorViewModule* camera = view_cam(view_camera_);
    const maths::Vec4& cursor = camera->cursor_;
    params.put_vec3(0x7F261953, cursor.x, cursor.y + camera->level_y_, cursor.z);
    params.push_float(0xFF7EBAF6, camera->heading_);
    script::launch_script(0xDEA7FE56, &params, nullptr, false, nullptr);
    params.get_clear();
}

// 0x004EA580
void ObjectiveAuthoring::refresh_cursor()
{
    const EditedGoal* goal = active_node_;
    int kind = goal->vertex_total_ ? core::at(goal->path_vertices_, goal->current_point_).kind : kPointPro;
    const char* model = nullptr;
    if (kind == kPointStart) {
        model = "gameobjects\\p1_cursor\\p1_cursor.mdl";
    } else if (kind == kPointObject) {
        int letter = 0;
        if (goal->kind_tag_ == kGoalTypeCombo || goal->kind_tag_ == kGoalTypeSkate)
            letter = goal->current_point_ - 2;
        model = goal_object_model(goal->kind_tag_, letter);
    } else {
        model = "gameobjects\\goal_cursor\\goal_cursor.mdl";
    }
    script::Struct params;
    goal = active_node_;
    kind = goal->vertex_total_ ? core::at(goal->path_vertices_, goal->current_point_).kind : kPointPro;
    if (kind == kPointPro)
        params.put_name_key(0x7321A8D6, 0x061A741E);
    else if (kind == kPointCheckpoint)
        params.put_name_key(0x7321A8D6, 0xF0FF0841);
    params.append_word(0x286A8D26, model);
    script::launch_script(0x36791E3C, &params, entity_, false, nullptr);
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
            if (goal.taken_count_ && goal.vertex_total_ >= goal.min_points_ && goal.level_ == level &&
                goal.level_goal_number_ == index) {
                ++index;
                taken = true;
                break;
            }
        }
    }
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count_)
            continue;
        active_node_ = &goal;
        init_edited_goal(&goal);
        active_node_->taken_count_ = true;
        active_node_->level_ = level;
        active_node_->level_goal_number_ = index;
        refresh_cursor();
        return;
    }
}

// 0x004EA780
void ObjectiveAuthoring::place_park_goals_on_ground()
{
    for (EditedGoal& goal : goals_) {
        if (goal.taken_count_ && goal.level_ == kParkEditorLevel)
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
    const uint32_t count = goals->extent_;
    for (uint32_t i = 0; i < count; ++i) {
        auto* entry = goals->struct_at(i);
        uint32_t level = 0;
        entry->read_hash_value(0x651533EC, &level, 0);
        const bool park = level == kParkEditorGameplayLevel || level == kParkEditorLevel;
        if (park != (park_only != 0))
            continue;
        for (EditedGoal& goal : goals_) {
            if (!goal.taken_count_) {
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
        if (goal.taken_count_ && goal.level_ == level)
            init_edited_goal(&goal);
    }
    active_node_ = nullptr;
    script::Array* goals = nullptr;
    params->fetch_list(kGoalListName, &goals, 1);
    uint32_t left = goals ? goals->extent_ : 0; // (Not original: none - the original read through null)
    uint32_t next = 0;
    for (EditedGoal& goal : goals_) {
        if (!left)
            break;
        if (goal.taken_count_)
            continue;
        goal.decode_from_struct(goals->struct_at(next++));
        goal.level_ = level;
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
        if (goal.taken_count_ && goal.level_ != kParkEditorLevel)
            ++count;
    }
    if (!count)
        return;
    script::Array* goals = script::alloc_script_array();
    goals = goals ? new (goals) script::Array : nullptr;
    goals->configure_size_kind(count, script::kQbvalStruct);
    uint32_t index = 0;
    for (EditedGoal& goal : goals_) {
        if (!goal.taken_count_ || goal.level_ == kParkEditorLevel)
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
    script::Array* goals = script::alloc_script_array();
    goals = goals ? new (goals) script::Array : nullptr;
    goals->configure_size_kind(count, script::kQbvalStruct);
    uint32_t index = 0;
    for (EditedGoal& goal : goals_) {
        if (!goal.taken_count_ || goal.level_ != level)
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
    const uint32_t written = script::emit_into_buffer(params.get(), buffer, size, 1, nullptr);
    params.reset();
    return written;
}

// 0x004EA7C0
bool ObjectiveAuthoring::new_editor_goal(script::Struct* /*params*/, script::Script* script)
{
    begin_fresh_objective();
    get_sprintf(created_goal_name(), "CreatedGoal%d", active_node_->level_goal_number_);
    script->args_->put_name_key("goal_id", checksums::hash_from_text(created_goal_name()));
    return true;
}

namespace {

// Whether a point can't go at the cursor: the start too near another goal's pro, or the pro too near another
// goal's start.
bool near_other_goals(ObjectiveAuthoring* editor, const maths::Vec4& pos, float height)
{
    const EditedGoal* goal = editor->active_node_;
    if (goal->vertex_total_ && core::at(goal->path_vertices_, goal->current_point_).kind == kPointStart &&
        editor->point_near_any_goal(pos, height, goal->level_, 0))
        return true;
    goal = editor->active_node_;
    if (!goal->vertex_total_ || core::at(goal->path_vertices_, goal->current_point_).kind == kPointPro)
        return editor->point_near_any_goal(pos, height, goal->level_, 1);
    return false;
}

// The input component's button `offset` bytes into the component (its pad at +0x20).
objects::ControlButton* get_button(objects::ObjectModule* input, uint32_t offset)
{
    return core::down_cast<objects::PadInputModule>(input)->pad_primary_.button_at_offset(offset - 0x20);
}

} // namespace

// 0x004EBD30
void ObjectiveAuthoring::tick_frame()
{
    if (!active_node_) {
        run_cursor_script();
        return;
    }
    objects::ControlButton* place = get_button(input_ptr_, 0x1C0);
    if (place->fired_) {
        place->reset_activation();
        const objects::EditorViewModule* camera = view_cam(view_camera_);
        const maths::Vec4 pos = camera->cursor_;
        const float height = camera->level_y_;
        const float angle = camera->heading_;
        // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
        if (near_other_goals(this, pos, height)) {
            script::launch_script(0xAEBC7020, nullptr, entity_, false, nullptr);
        } else if (active_node_->place_point(pos, height, angle)) {
            script::launch_script(0x718B071B, nullptr, entity_, false, nullptr);
            refresh_cursor();
            move_cursor_to_current_point();
        } else {
            script::launch_script(0xAEBC7020, nullptr, entity_, false, nullptr);
        }
        if (active_node_->last_point_placed_)
            script::launch_script(0xC05E7E20, nullptr, entity_, false, nullptr);
    }
    objects::ControlButton* back = get_button(input_ptr_, 0x1A0);
    if (back->fired_) {
        back->reset_activation();
        const objects::EditorViewModule* camera = view_cam(view_camera_);
        const maths::Vec4 pos = camera->cursor_;
        const float height = camera->level_y_;
        const float angle = camera->heading_;
        if (!near_other_goals(this, pos, height) && active_node_->place_point_and_step_back(pos, height, angle)) {
            script::launch_script(0xFA275CC5, nullptr, entity_, false, nullptr);
            refresh_cursor();
            move_cursor_to_current_point();
        } else {
            script::launch_script(0xADE100D6, nullptr, entity_, false, nullptr);
        }
    }
    run_cursor_script();
}

// 0x004EBC40
void ObjectiveAuthoring::conceal(bool hide)
{
    if (hide) {
        script::launch_script(0xFCC60D8B, nullptr, nullptr, false, nullptr);
        return;
    }
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_int(0x67E6859A, 0);
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
    params->read_hash_value(0x651533EC, &level, 0);
    if (level == kParkEditorGameplayLevel)
        level = kParkEditorLevel;
    const int count = count_complete_goals(level);
    if (!count)
        return true;
    script::Array* goals = script::alloc_script_array();
    goals = goals ? new (goals) script::Array : nullptr;
    goals->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalStruct);
    uint32_t index = 0;
    for (EditedGoal& goal : goals_) {
        if (!goal.taken_count_ || (level && goal.level_ != level) || goal.vertex_total_ < goal.min_points_)
            continue;
        gsl::owner<script::Struct*> entry = script::new_struct();
        goal.export_to_struct(entry);
        goals->set_cell(index++, script::Cell::of(entry));
    }
    script->args_->append_array_ref("EditedGoalsInfo", goals);
    return true;
}

} // namespace park_editing
