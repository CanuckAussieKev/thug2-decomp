// goals::CGoalManager's own methods: 0x00557E30-0x005591BF, its construction and its goal table and active list
// (0x0055B990-0x0055BF3F, 0x0055D960-0x0055DD02); see goal_manager.h.

#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"

#include <cstdint>
#include <initializer_list>
#include <array>

#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/lists/lookup_table.h"
#include "game/goals/objective.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "engine/lists/chain_start.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/trick_goals.h"
#include "engine/objects/trick_objects.h"
#include "game/skate/profile_manager.h"
#include "game/rider/rider.h"
#include "game/skate/point_tally.h"
#include "game/rider/trick_component.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include <gsl/pointers>

namespace goals {

namespace {

using namespace goal_names;

constexpr uint32_t kGoalManagerShowGoalPoints = 0x888EF05E; // GoalManager_ShowGoalPoints
constexpr uint32_t kGoalManagerParams = 0x23D4170A;         // GoalManager_Params
constexpr uint32_t kSkillTiers = 0xB13D98D3;                // difficulty_levels
constexpr uint32_t kGoalsClassic = 0xE8E7DBAD;              // goals_classic
constexpr uint32_t kAlwaysInitializeGoal = 0x4E863E93;      // always_initialize_goal
constexpr uint32_t kGoalCreateProsetGeom = 0xB3108A05;      // goal_create_proset_geom
constexpr uint32_t kName = 0xA1DC81F9;                      // Name
constexpr uint32_t kVo = 0x1A4E4FB9;                        // VO
constexpr uint32_t kType = 0x7321A8D6;                      // type
constexpr uint32_t kTrick = 0x270F56E1;                     // trick (the component)
constexpr uint32_t kCreateGeometry = 0xF4EB5C60;            // create_geometry
constexpr uint32_t kTrickObjects = 0xEFC35592;              // TrickObjects

constexpr uint8_t kMsgGoalWon = 0x6E; // to the server: a goal (the chain's first) won
constexpr uint8_t kGroupGoals = 8;    // (its message group)

const char* const kSpecialGap = "special_gap"; // "special_gap"

online::Hub* net_session_slot() { return online::net_game_state().online_hub(); }

void remove_objective_npc(ObjectiveNpc* ped) { ped->remove_objective_npc(); }
void hide_goal_ped(ObjectiveNpc* ped, bool hide) { ped->conceal(hide); }

// The first goal of the chain `goal` is in (its parents' first links, up to one without a parent): its id - `id`'s
// if it has no parent. (A parent that isn't in the table is read through null, as the original.)
template <typename F>
Objective* walk_to_root(GoalManager* manager, Objective* goal, const F& on_parent)
{
    for (const GoalEdge* link = goal->parents(); link && link->objective_key_;) {
        on_parent(link->objective_key_);
        goal = manager->goals().get_pull(link->objective_key_);
        link = goal->parents();
    }
    return goal;
}

bool has_no_parent(Objective* goal) { return goal->parents()->objective_key_ == 0; }

} // namespace

// 0x0055B9F0
void GoalListHead::destroy_body() { list_link_.uncouple(); }

// 0x0055D9C0
GoalManager::GoalManager()
{
    goals_.set_num(0);
    goals_.set_first(nullptr);
    goals_.set_cursor(nullptr);
    active_goals_.list_link_.make_head();
    for (script::Struct** structure : {&team_, &goal_data_, &args_}) {
        gsl::owner<script::Struct*> made = script::spawn_param_bag();
        *structure = made ? memory::construct_in<script::Struct>(made) : nullptr;
    }
    for (int& unlocked : group_unlocked_)
        unlocked = 0;
}

// 0x0055DB50
GoalManager::~GoalManager()
{
    wipe_objectives();
    if (script::Struct* data = goal_data_) {
        data->get_clear();
        script::delete_struct(data);
    }
    lists::unlink_all(active_goals_.list_link_);
    active_goals_.destroy_body();
    goals_.free_nodes();
}

// 0x00557E30
bool GoalManager::may_begin_objective() { return start_allowed_; }

// 0x00557E40
void GoalManager::show_goal_points()
{
    ++objective_score_;
    ++total_goal_points_;
    script::launch_script(kGoalManagerShowGoalPoints, nullptr, nullptr, false, nullptr);
}

// 0x00557E70
void GoalManager::reset_goal_data(uint32_t flag)
{
    if (flag)
        args_->erase_module(flag);
    else
        args_->get_clear();
    script::Component* component = goal_data_->step_to_entry(nullptr);
    while (component) {
        script::Component* next = goal_data_->step_to_entry(component);
        // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
        if (!flag) {
            goal_data_->erase_module(component->name);
        } else if (component->name == kGoalManagerParams) {
            component->value.structure()->get_clear();
            component->value.structure()->put_sub_struct(kSkillTiers, args_);
        } else if (component->value.structure() && component->value.structure()->has_bare_flag(flag)) {
            goal_data_->erase_module(component->name);
        }
        component = next;
    }
    objective_score_ = 0;
    total_goal_points_ = 0;
    goals_beaten_ = 0;
    all_unlocked_ = false;
}

// 0x00557F90
Objective* GoalManager::get_goal(uint32_t id, int) { return goals_.get_pull(id); }

// 0x00557FB0
Objective* GoalManager::get_objective_at(int index) { return goals_.get_by_index(index); }

// 0x00557FC0
bool GoalManager::engage_objective(uint32_t id, bool)
{
    Objective* goal = goals_.get_pull(id);
    if (!goal || goal->is_latched())
        return false;
    const bool started = goal->activate();
    if (started && goal->may_try_again() && has_no_parent(goal))
        previous_objective_ = id;
    return started;
}

// 0x00558030
bool GoalManager::complete_objective(uint32_t id)
{
    online::Hub* net = net_session_slot();
    Objective* goal = goals_.get_pull(id);
    if (!goal)
        return false;
    uint32_t root = id;
    walk_to_root(this, goal, [&](uint32_t parent) { root = parent; });
    if (root == previous_objective_ && goal->sub_nodes()->objective_key_ == 0)
        previous_objective_ = 0;
    if (!goal->has_won()) {
        if (goal->sub_nodes()->objective_key_ == 0 && net->in_net_game()) {
            net::NetMessageInfo desc;
            net::AppHost* client = net->fetch_patron(0);
            // The message (wire/gamenet_messages.h): the game's id and the chain's first goal (the id's three
            // padding bytes, as the original's stack had them - the network manager's address -, zeros here).
            online::GoalMessage message;
            message.id = net->get_game_id();
            message.goal = goal->get_top_objective_key();
            std::array<uint8_t, online::kGoalMessageSize> bytes{};
            util::ByteWriter writer(bytes.data(), sizeof(bytes));
            online::write_goal_message(writer, message);
            desc.byte_count_ = sizeof(bytes);
            desc.group_id_ = kGroupGoals;
            desc.content_ = bytes.data();
            desc.ident_ = kMsgGoalWon;
            desc.queue_ = net::NetMessageInfo::kLaneOrdered;
            core::down_cast<net::Guest>(client)->send_to_host(&desc);
        } else if (goal->sub_nodes()->objective_key_ == 0 && !goal->fetch_args()->has_bare_flag(kEditedGoal)) {
            ++goals_beaten_;
        }
    }
    goal->get_win();
    return true;
}

// 0x00558180
bool GoalManager::fail_objective(uint32_t id)
{
    Objective* goal = goals_.get_pull(id);
    if (!goal)
        return false;
    goal->lose();
    if (game_mode()->get_name_key() == kClassic ||
        (game_mode()->get_name_key() == kNetGoalAttack && game_mode()->get_goals_mode() == kGoalsClassic))
        engage_objective(id, false);
    return true;
}

// 0x00558200
int GoalManager::count_picked_objectives()
{
    int selected = 0;
    for (int i = 0; i < goals_.num(); ++i)
        if (goals_.get_by_index(i)->is_picked())
            ++selected;
    return selected;
}

// 0x00558250
void GoalManager::disengage_every_objective(bool unused)
{
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->is_operative())
            goal->disengage(unused, true);
    }
}

// 0x005582B0
void GoalManager::activate_all_goals()
{
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (!goal->is_operative() && !goal->has_won() && !goal->is_online_objective())
            goal->activate();
    }
}

// 0x00558320
void GoalManager::release_objective_tree(uint32_t id)
{
    Objective* root = walk_to_root(this, goals_.get_pull(id), [](uint32_t) {});
    root->uninitialize(true, 0);
}

// 0x00558380
void GoalManager::release_every_objective()
{
    for (int i = 0; i < goals_.num(); ++i)
        goals_.get_by_index(i)->uninitialize(false, 0);
}

// 0x005583C0
void GoalManager::prepare_every_objective()
{
    online::Hub* net = net_session_slot();
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (net->in_net_game() && !goal->is_online_objective())
            continue;
        goal->merge_skill_tier_args();
        if (goal->is_latched())
            continue;
        if (goal->has_won() && !goal->fetch_args()->has_bare_flag(kAlwaysInitializeGoal))
            continue;
        const GoalEdge* parents = goal->parents();
        script::Struct* params = goal->fetch_args();
        if (parents->objective_key_)
            continue;
        if (game_mode()->get_name_key() == kClassic || !goal->has_won() ||
            params->has_bare_flag(kAlwaysInitializeGoal) || params->has_bare_flag(kEditedGoal))
            goal->prepare_all();
    }
}

// 0x005584C0
void GoalManager::prepare_picked_objectives()
{
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (!goal->is_picked())
            continue;
        script::launch_script(kGoalCreateProsetGeom, goal->fetch_args(), nullptr, false, nullptr);
        goal->prepare_all();
    }
}

// 0x00558530
void GoalManager::activate_all_selected_goals()
{
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->is_picked())
            goal->activate();
    }
}

// 0x00558580
void GoalManager::unpick_every_objective()
{
    for (int i = 0; i < goals_.num(); ++i)
        goals_.get_by_index(i)->unpick();
}

// 0x005585C0
void GoalManager::freeze_every_objective()
{
    for (int i = 0; i < goals_.num(); ++i)
        goals_.get_by_index(i)->get_freeze();
}

// 0x00558600
void GoalManager::thaw_every_objective()
{
    for (int i = 0; i < goals_.num(); ++i)
        goals_.get_by_index(i)->un_pause();
}

// 0x00558640
bool GoalManager::begin_objective_at_once(uint32_t id, bool check_exists)
{
    Objective* goal = goals_.get_pull(id);
    if (!goal && check_exists)
        return false;
    goal->set_quick_start(); // (no such goal and not check_exists: written through null, as the original)
    const bool started = goal->activate();
    if (started && goal->may_try_again())
        previous_objective_ = id;
    goal->unset_quick_start();
    return started;
}

// 0x005586A0
bool GoalManager::raise_bit(uint32_t id, uint32_t flag, int value, uint32_t sub)
{
    Objective* goal = goals_.get_pull(id);
    if (!goal)
        return false;
    return goal->set_flag(flag, value, sub);
}

// 0x005586E0
bool GoalManager::register_gap_hit(uint32_t gap)
{
    bool got = false;
    for (int i = 0; i < goals_.num(); ++i)
        got = goals_.get_by_index(i)->register_gap_hit(gap);
    return got;
}

// 0x00558730
script::Struct* GoalManager::get_objective_args(uint32_t id) { return goals_.get_pull(id)->fetch_args(); }

// 0x00558750
bool GoalManager::step_to_next_waypoint(uint32_t id) { return goals_.get_pull(id)->step_to_next_waypoint(); }

// 0x00558770
void GoalManager::kill_cluster(uint32_t cluster, int score)
{
    const int count = goals_.num(); // (read once)
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (!goal->is_operative() || goal->is_online_objective())
            continue;
        if (goal->is_graffiti())
            goal->kill_cluster(cluster, score);
        else
            stubs::ignore_argument(static_cast<int>(cluster));
    }
}

// 0x005587F0
void GoalManager::check_trick_goals()
{
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (!goal->is_operative() || goal->is_online_objective())
            continue;
        script::Struct* params = goal->fetch_args();
        if (goal->is_special() || params->has_bare_flag(kCreateATrick)) {
            if (!params->has_bare_flag(kSpecialGap) && goal->did_trick_string())
                complete_objective(goal->get_objective_key());
        } else if (goal->is_tetris()) {
            core::down_cast<FallingTricksGoal>(goal)->check_tricks();
        } else if (goal->is_find_gaps()) {
            core::down_cast<HuntGapsObjective>(goal)->scan_for_gaps();
        }
    }
}

// 0x005588D0
void GoalManager::open_up_objective(uint32_t id) { goals_.get_pull(id)->get_unlock(); }

// 0x005588F0
int GoalManager::has_beaten_goal(uint32_t id)
{
    if (Objective* goal = goals_.get_pull(id))
        return goal->has_won();
    int beaten = 0;
    script::Struct* data = nullptr;
    if (goal_data_->read_sub_struct(id, &data, 0))
        data->fetch_int_value(kAlreadyWon, &beaten, 1);
    return beaten != 0;
}

// 0x00558960
void GoalManager::flip_objective_pick(uint32_t id)
{
    Objective* goal = goals_.get_pull(id);
    if (goal->is_picked())
        goal->unpick();
    else
        goal->choose_one();
}

// 0x005589A0
bool GoalManager::has_won_proset_goal(const char* prefix)
{
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->has_won() && goal->has_proset_prefix(prefix))
            return true;
    }
    return false;
}

// 0x00558A00
void GoalManager::restore_goal_data()
{
    script::Component* component = goal_data_->step_to_entry(nullptr);
    while (component) {
        script::Component* next = goal_data_->step_to_entry(component);
        if (component->name == kGoalManagerParams) {
            script::Struct* levels = nullptr;
            component->value.structure()->read_sub_struct(kSkillTiers, &levels, 1);
            args_->get_clear();
            args_->append_structure_if_any(levels);
        } else if (Objective* goal = goals_.get_pull(component->name)) {
            goal->restore_save_block(component->value.structure());
        }
        component = next;
    }
}

// 0x00558AB0
int GoalManager::count_cleared_by_player(int bit)
{
    int count = 0;
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (has_no_parent(goal) && goal->stage_flag_set(bit))
            ++count;
    }
    return count;
}

// 0x00558B10
int GoalManager::num_goals_net_beaten_by(int bit)
{
    int count = 0;
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (has_no_parent(goal) && goal->net_stage_flag_set(bit))
            ++count;
    }
    return count;
}

// 0x00558B70
int GoalManager::count_objectives_cleared()
{
    int count = 0;
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (has_no_parent(goal) && goal->has_won())
            ++count;
    }
    return count;
}

// 0x00558BC0
void GoalManager::open_up_every_objective()
{
    all_unlocked_ = true;
    for (int i = 0; i < goals_.num(); ++i)
        goals_.get_by_index(i)->mark_viewed();
}

// 0x00558C00
void GoalManager::unlock_all_pro_goals()
{
    all_unlocked_ = true;
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->is_pro_challenge() && goal->is_latched())
            goal->get_unlock();
    }
}

// 0x00558C70
void GoalManager::play_objective_voice_line(script::Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    const Objective* goal = goals_.get_pull(name);
    uint32_t stream = 0;
    params->read_hash_value(kVo, &stream, 0);
    if (stream) {
        goal->ped()->play_stream(stream, true, 0, false);
        return;
    }
    const char* type = nullptr;
    if (params->fetch_text(kType, &type, 0))
        goal->ped()->play_objective_voice_line(type, params);
}

// 0x00558D20
int GoalManager::is_goal_blocked(uint32_t id)
{
    if (Objective* goal = goals_.get_pull(id))
        return goal->is_latched();
    int locked = 0;
    script::Struct* data = nullptr;
    if (goal_data_->read_sub_struct(id, &data, 0))
        data->fetch_int_value(kIsLocked, &locked, 1);
    return locked != 0;
}

// 0x00558D90
Objective* GoalManager::get_active_competition()
{
    for (int i = 0; i < goals_.num(); ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->is_contest_objective() && goal->is_operative())
            return goal;
    }
    return nullptr;
}

// 0x00558DF0
void GoalManager::replace_trick_text_all()
{
    const int count = goals_.num(); // (read once)
    for (int i = 0; i < count; ++i)
        goals_.get_by_index(i)->swap_move_caption();
}

// 0x00558E30
bool GoalManager::goal_exists(uint32_t id) { return goals_.get_pull(id) != nullptr; }

// 0x00558E50
void GoalManager::test_tagged_props()
{
    auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
    const int player = skater->get_points_total()->pot_score();
    auto* trick = core::down_cast<objects::MoveExecutor>(skater->fetch_module(kTrick));
    // The combo's trick objects: up to 512 clusters, then their count (copied first, as the original).
    const objects::HitClusterLog objects = trick->trick_objects();
    const int count = objects.num() > 512 ? 512 : objects.num();
    for (int i = 0; i < count; ++i)
        kill_cluster(core::at(objects.items(), i), player);
}

// 0x00558ED0
void GoalManager::build_goal_tree()
{
    const int count = goals_.num(); // (read once)
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_by_index(i);
        const uint32_t id = goal->get_objective_key();
        for (const GoalEdge* link = goal->sub_nodes(); link && link->objective_key_; link = link->succ_) {
            Objective* child = goals_.get_pull(link->objective_key_); // (one not in the table is read through null)
            bool linked = false;
            for (const GoalEdge* parent = child->parents(); parent && parent->objective_key_; parent = parent->succ_) {
                if (parent->objective_key_ == id) {
                    linked = true;
                    break;
                }
            }
            if (!linked)
                child->attach_upper_link(id);
        }
    }
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (has_no_parent(goal))
            goal->pass_inherited_flags(0);
    }
}

// 0x00558FD0
void GoalManager::spawn_objective_props()
{
    for (int i = 0; i < goals_.num(); ++i) {
        script::Array* nodes = nullptr;
        if (!goals_.get_by_index(i)->fetch_args()->fetch_list(kCreateGeometry, &nodes, 0))
            continue;
        for (uint32_t n = 0; n < nodes->extent(); ++n)
            commands::create_from_node_name(nodes->fetch_part(n));
    }
}

// 0x00559050
int GoalManager::get_skill_tier(bool career)
{
    const uint32_t mode = career ? kCareer : game_mode()->get_name_key();
    int level = 0;
    if (!args_->fetch_int_value(mode, &level, 0))
        level = 1;
    switch (level) {
    case 0:
        return 0;
    case 2:
        return 2;
    default:
        return 1;
    }
}

// 0x005590C0
void GoalManager::conceal_every_objective_npc(bool hide)
{
    for (int i = 0; i < goals_.num(); ++i)
        hide_goal_ped(goals_.get_by_index(i)->ped(), hide);
}

// 0x00559110
bool GoalManager::is_group_locked(int group, int level)
{
    if (group == -1)
        return false;
    if (level == -1)
        level = skate_calls::get_career()->fetch_stage();
    return core::at(group_unlocked_, level * kGroupsPerLevel + group - 1) == 0;
}

// 0x00559150
void GoalManager::trick_object_hit(uint32_t cluster)
{
    const int count = goals_.num(); // (read once)
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->fetch_args()->has_bare_flag(kTrickObjects))
            core::down_cast<TrickObjectsGoal>(goal)->get_object_hit(cluster);
    }
}

// 0x0055BEA0
bool GoalManager::drop_objective(uint32_t id)
{
    gsl::owner<const Objective*> goal = goals_.get_pull(id);
    remove_objective_npc(goal->ped());
    goals_.drop_out(id);
    if (previous_objective_ == id)
        previous_objective_ = 0;
    delete goal;
    return true;
}

// 0x0055BEF0
void GoalManager::wipe_objectives()
{
    previous_objective_ = 0;
    for (int i = 0; i < goals_.num(); ++i)
        remove_objective_npc(goals_.get_by_index(i)->ped());
    lists::TableItemEraser destroyer = {&goals_};
    destroyer.empty_table_items([](void* goal) {
        gsl::owner<Objective*> objective = memory::adopt<Objective*>(goal); // (the table's)
        delete objective;
    });
}

// 0x0055DBE0
void GoalManager::add_active_goal(Objective* goal)
{
    lists::append_vertex(active_goals_.list_link_, goal->list_node());
}

namespace {

// The active list's nodes (the walk checks each node's data for -1 before the node for null, as the original).
int count_active(memory::ListNode* head)
{
    const memory::ListNode* node = head->next();
    if (node->data() == memory::ListNode::head_marker() || !node)
        return 0;
    int count = 0;
    for (bool again = true; again; again = node->data() != memory::ListNode::head_marker() && node) {
        node = node->next();
        ++count;
    }
    return count;
}

} // namespace

memory::ListNode* GoalListHead::get_node_at(int index) { return lists::node_at(list_link_, index); }

// 0x0055DBF0
Objective* GoalManager::get_engaged_objective(int index)
{
    if (!count_active(&active_goals_.list_link_))
        return nullptr;
    return static_cast<Objective*>(active_goals_.get_node_at(index)->data());
}

// 0x0055DC30
int GoalManager::count_live_goals(bool all)
{
    int counted = 0;
    const int count = count_active(&active_goals_.list_link_);
    for (int i = 0; i < count; ++i) {
        Objective* goal = count_active(&active_goals_.list_link_)
                              ? static_cast<Objective*>(active_goals_.get_node_at(i)->data())
                              : nullptr;
        const bool counts = goal->is_online_objective() ? all : all || goal->is_operative();
        if (counts)
            ++counted;
    }
    return counted;
}

// 0x005591B0
GoalManager* get_objective_board() { return skate_calls::get_skate()->get_objective_board(); }

} // namespace goals
