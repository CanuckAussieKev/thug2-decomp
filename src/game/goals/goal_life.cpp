// goals::Objective's life: made, initialized, started, stopped, won, unlocked, rewarded, destroyed; its goal flags set
// (0x00555DF0-0x00556109, 0x00556110-0x0055632F, 0x005566A0-0x005568AF, 0x00556F90-0x00557ABF; see objective.h).

#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/enum_ops.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/goals/objective.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/end_run.h"
#include "game/skate/game_mode.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "engine/net/player_info.h"
#include "game/rider/skater_score.h"
#include "game/rider/rider.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/net/net_game_state.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace goals {

namespace {
constexpr int kMillisecondsPerSecond = 1000;

using namespace goal_calls;
using namespace goal_names;

constexpr uint32_t kGroupNumber = 0xBAE9387B;             // group_number
constexpr uint32_t kSingleTimer = 0x04AB5F14;             // single_timer
constexpr uint32_t kRetryStage = 0xC6B4258F;              // retry_stage
constexpr uint32_t kSkater = 0x5B8AB877;                  // skater
constexpr uint32_t kNone = 0x806FFF30;                    // none
constexpr uint32_t kUnlockedByAnother = 0x8A3A324E;       // unlocked_by_another
constexpr uint32_t kDestroy = 0x83B57984;                 // destroy
constexpr uint32_t kDifficultyLevelParams = 0x85104AB3;   // difficulty_level_params
constexpr uint32_t kInit = 0x398B1B8B;                    // init
constexpr uint32_t kDontDeactivateOnExpiry = 0x79BBFFCB;  // dont_deactivate_upon_expiration
constexpr uint32_t kResetScore = 0x4599B58C;              // reset_score
constexpr uint32_t kActivate = 0x4999FB52;                // activate
constexpr uint32_t kButtonSequences = 0x79704516;         // key_combos
constexpr uint32_t kDeactivate = 0x01C92CAC;              // deactivate
constexpr uint32_t kTrialRunFlag = 0x54D3CAC1;            // testing_goal
constexpr uint32_t kJustWonGoal = 0xC309CAD1;             // just_won_goal
constexpr uint32_t kReadySkaterForEarlyEnd = 0x719AE783;  // ready_skater_for_early_end_current_goal
constexpr uint32_t kCustomScoreRecord = 0xFC9630AB;       // custom_score_record
constexpr uint32_t kHighCombo = 0xC63432A7;               // high_combo
constexpr uint32_t kNewRecord = 0xE7B8254F;               // (name unknown: a new record set)
constexpr uint32_t kRewardParams = 0x128B3088;            // reward_params
constexpr uint32_t kSuccess = 0x90FF204D;                 // success
constexpr uint32_t kIsCareer = 0x1DED1EA4;                // is_career
constexpr uint32_t kIsClassic = 0x5A2C589A;               // is_classic
constexpr uint32_t kRewardPointsName = 0x35A1C616;        // RewardPoints
constexpr uint32_t kCheckChapterAdvancement = 0x7CF09270; // CheckChapterAdvancementStatus
constexpr uint32_t kAlwaysInitializeGoal = 0x4E863E93;    // always_initialize_goal
constexpr uint32_t kUnlockScript = 0x951B4F10;            // unlock
constexpr uint32_t kRewardsGiven = 0xC7FEF529;            // (name unknown: the rewards structure's flag)
constexpr uint32_t kRewardTrickslot = 0xB6432D23;         // reward_trickslot
constexpr uint32_t kMaxTrickslots = 0x20297D6F;           // max_number_of_special_trickslots
constexpr uint32_t kShouldRemoveTrick = 0xBC678826;       // should_remove_trick
constexpr uint32_t kAlreadyAddedTrickslot = 0x65CA5FFC;   // already_added_trickslot
constexpr uint32_t kJustAddedTrickslot = 0xB45FD352;      // just_added_trickslot
constexpr uint32_t kGoalRewards = 0x67F6E334;             // goal_rewards
constexpr uint32_t kRewardStat = 0xDE569045;              // reward_stat
constexpr uint32_t kRewardGoal = 0x0232844A;              // Reward_Goal
constexpr uint32_t kRewardPoints = 0xBAFFB052;            // reward_points
constexpr uint32_t kSilentActivation = 0x2D4C5829;        // silent_activation
constexpr uint32_t kNoFlagSound = 0x524566E3;             // (name unknown: no flag sound)
constexpr uint32_t kGoalGotFlagSound = 0x5ED34DEB;        // goal_got_flag_sound
constexpr uint32_t kTrickObjects = 0xEFC35592;            // TrickObjects
constexpr uint32_t kGaps = 0xD76C173E;                    // Gaps
constexpr uint32_t kNoGapMessages = 0x5BC07EAA;           // No_Gap_Messages
constexpr uint32_t kGoalGapsGotGap = 0x96DE9A0C;          // goal_gaps_got_gap

// The x87 code's float to int conversion (_ftol): truncation.
int truncate(double value) { return static_cast<int>(value); }

uint32_t active_ruleset_label()
{
    return static_cast<goals::MatchRules*>(skate_calls::get_game_mode())->get_name_key();
}

GoalEdge* alloc_chain_link()
{
    auto* link = static_cast<GoalEdge*>(memory::alloc(sizeof(GoalEdge)));
    if (link) {
        link->objective_key_ = 0;
        link->succ_ = nullptr;
        link->won_ = false;
    }
    return link;
}
// A goal's list node out of its list (linked to itself).
void uncouple(memory::ListNode* node)
{
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

// The goal's pro (goals::ObjectiveNpc, 0x14 bytes): 0x0055EB40 the constructor (its goal's parameters); its destructor
// is empty (0x005480A0).
gsl::owner<ObjectiveNpc*> new_goal_ped(script::Struct* params)
{
    void* memory = memory::alloc(sizeof(ObjectiveNpc));
    return memory ? memory::construct_in<ObjectiveNpc>(memory, params) : nullptr;
}
inline void destroy_goal_ped_body(ObjectiveNpc* /*unused*/) {}

// lists::Hunt<PlayerRecord> (vtable 0x0064C1A8) for the network manager's begin_player_walk / step_player_walk.
using PlayerSearch = online::PlayerSearch;
objects::GameEntity* player_skater(void* player) { return static_cast<online::PlayerRecord*>(player)->skater(); }
// A skater's score (its SkaterScore component's modules::PointTally) reset (0x00564A10) / its total (0x00561B40).
void* get_score_of(objects::GameEntity* skater)
{
    return core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore))->points_held();
}
void reset_score(void* score) { static_cast<modules::PointTally*>(score)->get_reset(); }
int total_score(void* score) { return static_cast<modules::PointTally*>(score)->get_total_score(); }
// The root of a goal's parents (the last one whose first parent link has a goal), or null if it has none.
Objective* root_of(void* manager, GoalEdge* parents)
{
    Objective* root = nullptr;
    for (const GoalEdge* link = parents; link && link->objective_key_; link = root->parents()) {
        root = get_goal(manager, link->objective_key_, 1);
    }
    return root;
}

} // namespace

void Objective::make_args_ped_and_flags(script::Struct* params)
{
    args_ = script::make_struct();
    args_->append_structure_if_any(params);
    ped_ = new_goal_ped(args_.get());
    goal_flags_ = script::new_struct();
}

// 0x00556FE0
Objective::Objective(script::Struct* params)
    : sub_nodes_(alloc_chain_link()), parents_(alloc_chain_link()), level_(skate_calls::get_career()->fetch_stage())
{
    list_link_.make_node(this);
    cur_phase_ = (cur_phase_ & ~(Objective::State::kActive | Objective::State::kPaused)) | Objective::State::kNoTimeLimit;
    flag_bits_ &= ~(Objective::Flags::kSeen | Objective::Flags::kWon | Objective::Flags::kWonInNetGame);
    flag_bits_ = (flag_bits_ & ~(Objective::Flags::kInitialized | Objective::Flags::kSelected)) | Objective::Flags::kDeactivateOnExpire;
    make_args_ped_and_flags(params);
    params->fetch_int_value(kGroupNumber, &bundle_, 0);
    alloc_objective_bits(params);
    if (params->has_bare_flag(kSingleTimer)) {
        inherited_ |= 2;
    }
    if (params->has_bare_flag(kRetryStage)) {
        inherited_ |= 4;
    }
    skater_ = kNone;
    params->read_hash_value(kSkater, &skater_, 0);
    if (params->has_bare_flag(kUnlockedByAnother) || params->has_bare_flag(kProGoal)) {
        flag_bits_ |= Objective::Flags::kLocked;
    } else {
        flag_bits_ &= ~Objective::Flags::kLocked;
    }
}

// 0x00556110
Objective::~Objective()
{
    uint32_t script = 0;
    if (args_->read_hash_value(kDestroy, &script, 0)) {
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    }
    if (ped_) {
        destroy_goal_ped_body(ped_);
        memory::dealloc(ped_);
    }
    memory::destroy_then_clear(args_);
    script::free_struct(goal_flags_);
    for (GoalEdge* link = sub_nodes_; link;) {
        GoalEdge* next = link->succ_;
        memory::dealloc(link);
        link = next;
    }
    for (GoalEdge* link = parents_; link;) {
        GoalEdge* next = link->succ_;
        memory::dealloc(link);
        link = next;
    }
    uncouple(list_node()); // (the node's destructor, inlined)
}

// 0x00556F90
void Objective::merge_skill_tier_args()
{
    script::Array* levels = nullptr;
    if (!args_->fetch_list(kDifficultyLevelParams, &levels, 0)) {
        return;
    }
    const int level = get_skill_tier(objective_board(), false);
    args_->append_structure_if_any(levels->struct_at(static_cast<uint32_t>(level)));
    alloc_objective_bits(args_.get());
}

// 0x00557270
void Objective::prepare_all()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap());
    // (merge_skill_tier_args, inlined)
    script::Array* levels = nullptr;
    if (args_->fetch_list(kDifficultyLevelParams, &levels, 0)) {
        const int level = get_skill_tier(objective_board(), false);
        args_->append_structure_if_any(
            levels->struct_at(static_cast<uint32_t>(level)));
        alloc_objective_bits(args_.get());
    }
    uint32_t script = 0;
    if (args_->read_hash_value(kInit, &script, 0)) {
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    }
    swap_move_caption();
    flag_bits_ |= Objective::Flags::kInitialized;
    int unlimited = 0;
    args_->fetch_int_value(kUnlimitedTime, &unlimited, 0);
    if (unlimited) {
        cur_phase_ |= Objective::State::kNoTimeLimit;
    }
    if (args_->has_entry_named(kDontDeactivateOnExpiry)) {
        flag_bits_ &= ~Objective::Flags::kDeactivateOnExpire;
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00557360
bool Objective::activate()
{
    if (is_operative()) {
        return false;
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->bottom_up_heap());
    void* manager = objective_board();
    ++times_started_;
    elapsed_ = 0;
    online::Hub* net = online::online_hub();
    PlayerSearch search;
    if (args_->has_bare_flag(kResetScore)) {
        for (void* player = net->begin_player_walk(&search, false); player;
             player = net->step_player_walk(&search, false)) {
            reset_score(get_score_of(player_skater(player)));
        }
    }
    // A child goal: the timer passed down (single_timer: its root's, in whole seconds), the root's state for it.
    Objective* root = parents_->objective_key_ ? root_of(manager, parents_) : nullptr;
    if (root) {
        if (inherited_ & 2) {
            seconds_remaining_ =
                truncate(root->seconds_remaining_ * static_cast<double>(kGoalLifeSecondsPerMillisecond)) * kMillisecondsPerSecond;
        } else {
            reset_timer();
        }
        if (root->is_prepared()) {
            root->uninitialize(false, 0);
            if (is_prepared()) {
                uninitialize(false, 0);
                prepare_all();
            }
        }
    } else {
        reset_timer();
    }
    if (!is_prepared()) {
        prepare_all();
    }
    flag_bits_ &= ~Objective::Flags::kEndOfRunStarted;
    args_->remove_flag(kChainActiveFlag);
    uint32_t script = 0;
    if (args_->read_hash_value(kActivate, &script, 0)) {
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    }
    if (!net->in_net_game()) {
        flag_bits_ |= Objective::Flags::kSeen;
    }
    change_live();
    add_active_goal(manager, this);
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x00557560
bool Objective::disengage(bool /*unused*/, bool reset_tree)
{
    if (!is_operative()) {
        return false;
    }
    if (args_->has_bare_flag(kSpecial)) {
        args_->erase_module(kButtonSequences);
    }
    clear_engaged_bit();
    if (!args_->has_bare_flag(kSaveGoalFlags)) {
        zero_objective_bits();
    }
    clear_child_wins();
    uint32_t script = 0;
    if (args_->read_hash_value(kDeactivate, &script, 0)) {
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    }
    if (!args_->has_bare_flag(kQuickStart)) {
        args_->remove_flag(kTrialRunFlag);
    }
    if (!args_->has_bare_flag(kJustWonGoal) && !args_->has_bare_flag(kQuickStart)) {
        script::launch_script(kReadySkaterForEarlyEnd, args_.get(), nullptr, false, nullptr);
        if (reset_tree && parents_ && parents_->objective_key_) {
            uninitialize(false, 0);
            void* manager = objective_board();
            Objective* root = root_of(manager, parents_);
            if (root && !root->has_won()) {
                root->prepare_all();
            }
        }
    }
    uncouple(list_node());
    return true;
}

// 0x005576D0
bool Objective::get_win()
{
    args_->put_int(checksums::Name{kWinRecord}, 0);
    GoalEdge* children = sub_nodes_;
    if (children->objective_key_) {
        // A goal of stages: the next one started.
        void* manager = objective_board();
        disengage(false, true);
        uninitialize(false, 0);
        get_goal(manager, children->objective_key_, 1)->prepare_all();
        engage_objective(manager, children->objective_key_, false);
        children->won_ = true;
        return true;
    }
    args_->put_name_key(checksums::Name{kNoName}, kJustWonGoal);
    online::Hub* net = online::online_hub();
    bool new_record = false;
    uint32_t type = 0;
    if (!net->in_net_game() && args_->read_hash_value(kRecordType, &type, 0)) {
        int record = 0;
        bool better = false;
        if (type == kScore) {
            int custom = 0;
            if (args_->fetch_int_value(kCustomScoreRecord, &custom, 0)) {
                record = custom;
                better = record > record_points_;
            } else {
                void* score = get_score_of(skate_calls::get_home_rider());
                record = args_->has_bare_flag(kHighCombo) ? static_cast<modules::PointTally*>(score)->pot_score()
                                                          : total_score(score);
                better = record > record_points_;
            }
            if (better) {
                record_points_ = record;
            }
        } else if (type == kTime) {
            record = elapsed_;
            better = static_cast<uint32_t>(record) < static_cast<uint32_t>(best_time_);
            if (better) {
                best_time_ = record;
            }
        }
        if (better) {
            args_->put_int(checksums::Name{kWinRecord}, record);
            new_record = true;
        }
    }
    bool rewarded = false;
    if (!has_won() && !net->in_net_game()) {
        gsl::owner<script::Struct*> rewards = script::new_struct();
        get_prize_struct(rewards);
        if (new_record) {
            rewards->put_name_key(checksums::Name{kNoName}, kNewRecord);
        }
        args_->append_struct_ref(kRewardParams, rewards);
        open_up_prize_objectives();
        rewarded = true;
    } else {
        args_->erase_module(kRewardParams);
    }
    if (new_record) {
        args_->put_name_key(checksums::Name{kNoName}, kNewRecord);
    } else {
        args_->remove_flag(kNewRecord);
    }
    uint32_t script = 0;
    if (args_->read_hash_value(kSuccess, &script, 0)) {
        script::launch_script(script, args_.get(), nullptr, false, nullptr);
    }
    const bool testing = args_->has_bare_flag(kTrialRunFlag);
    if (is_operative()) {
        disengage(false, true);
    }
    if (!testing) {
        mark_won();
        if (rewarded) {
            objects::RiderStoryProgress* career = skate_calls::get_career();
            if (skate_calls::game_mode_is_true(kIsCareer)) {
                career->deposit_money(get_reward_points());
                const script::StructPtr params = script::make_struct();
                params->put_int(checksums::Name{kRewardPointsName}, get_reward_points());
                script::launch_script(kCheckChapterAdvancement, params.get(), nullptr, false, nullptr);
            } else if (skate_calls::game_mode_is_true(kIsClassic)) {
                career->increment_counter();
            }
        }
    }
    if (net->in_net_game()) {
        uninitialize(false, 0);
    }
    args_->remove_flag(kJustWonGoal);
    return true;
}

// 0x00557A40
void Objective::get_unlock()
{
    if (!core::any(flag_bits_ & Objective::Flags::kLocked)) {
        return;
    }
    flag_bits_ &= ~Objective::Flags::kLocked;
    if (online::online_hub()->in_net_game()) {
        return;
    }
    if (!has_won() || active_ruleset_label() == kClassic || args_->has_bare_flag(kAlwaysInitializeGoal) ||
        args_->has_bare_flag(kEditedGoal)) {
        prepare_all();
    }
    invoke_hook_script(kUnlockScript);
}

// 0x00555DF0
void Objective::get_prize_struct(script::Struct* rewards)
{
    void* manager = objective_board();
    online::Hub* net = online::online_hub();
    if (!args_->has_bare_flag(kEditedGoal)) {
        show_goal_points(manager);
    }
    rewards->put_name_key(checksums::Name{kNoName}, kRewardsGiven);
    if (net->in_net_game()) {
        return;
    }
    // A special trick slot as the reward, while there's room for one.
    if (args_->has_bare_flag(kRewardTrickslot) && !args_->has_bare_flag(kCreateATrick)) {
        objects::RiderIdentity* profile = skate_calls::get_profile_from_params(nullptr);
        const int slots = profile->get_max_specials();
        if (slots <= script::fetch_int_value(kMaxTrickslots)) {
            int remove = 0;
            args_->fetch_int_value(kShouldRemoveTrick, &remove, 0);
            if (remove == 1) {
                args_->put_int(checksums::Name{kShouldRemoveTrick}, 0);
                args_->put_name_key(checksums::Name{kNoName}, kAlreadyAddedTrickslot);
                args_->put_int(checksums::Name{kJustAddedTrickslot}, 1);
            }
        }
    }
    script::Struct* all = script::read_sub_struct(kGoalRewards);
    uint32_t id = 0;
    args_->read_hash_value(kGoalIdName, &id, 1);
    script::Struct* own = nullptr;
    all->read_sub_struct(id, &own, 0);
    if (own) {
        if (own->has_bare_flag(kRewardStat)) {
            own->erase_module(kRewardStat);
            own->put_int(checksums::Name{kRewardStat}, 1);
        }
        rewards->append_structure_if_any(own);
        return;
    }
    if (args_->has_bare_flag(kRewardStat)) {
        rewards->put_int(checksums::Name{kRewardStat}, 1);
        return;
    }
    int stat = 0;
    if (args_->fetch_int_value(kRewardStat, &stat, 0)) {
        rewards->put_int(checksums::Name{kRewardStat}, stat);
    }
}

// 0x00555FB0
void Objective::open_up_prize_objectives()
{
    void* manager = objective_board();
    uint32_t goal = 0;
    if (args_->read_hash_value(kRewardGoal, &goal, 0)) {
        open_up_objective(manager, goal);
        return;
    }
    script::Array* goals = nullptr;
    if (!args_->fetch_list(kRewardGoal, &goals, 0)) {
        return;
    }
    const int count = static_cast<int>(goals->extent());
    for (int i = 0; i < count; ++i) {
        const uint32_t id = goals->fetch_part(static_cast<uint32_t>(i));
        if (id) {
            open_up_objective(manager, id);
        }
    }
}

// 0x00556050
int Objective::get_reward_points()
{
    int points = 0;
    if (args_->fetch_int_value(kRewardPoints, &points, 0)) {
        return points;
    }
    script::Array* by_level = nullptr;
    if (args_->fetch_list(kRewardPoints, &by_level, 0)) {
        const int level = get_skill_tier(objective_board(), false);
        if (level >= 0 && level < ints::as_signed(by_level->extent())) {
            return static_cast<int>(by_level->fetch_part(static_cast<uint32_t>(level)));
        }
    }
    return 0;
}

// 0x00556330
bool Objective::set_flag(checksums::Name flag_name, int value, checksums::Name sub_goal)
{
    const uint32_t sub = sub_goal.value();
    const uint32_t flag = flag_name.value();
    // A silently activated goal (not won yet) takes flags while inactive, and is won by them.
    const bool silent = args_->has_bare_flag(kSilentActivation) && !has_won();
    if (!is_operative() && !silent) {
        return false;
    }
    void* manager = objective_board();
    if (core::any(flag_bits_ & Objective::Flags::kLocked) || is_group_locked(manager, bundle_, level_)) {
        return false;
    }
    script::Struct* flags = goal_flags_;
    if (sub) {
        goal_flags_->read_sub_struct(sub, &flags, 1);
    }
    bool changed = false;
    if (flags->has_entry_named(flag)) {
        int old = 0;
        flags->fetch_int_value(flag, &old, 1);
        flags->put_int(checksums::Name{flag}, value);
        if (old != value) {
            bool counted = false;
            if (sub) {
                int set = 0;
                flags->fetch_int_value(kNumFlagsSet, &set, 1);
                counted = value != 0;
                set += counted ? 1 : -1;
                flags->put_int(checksums::Name{kNumFlagsSet}, set);
            } else {
                counted = value != 0;
                num_flags_set_ += counted ? 1 : -1;
            }
            if (counted && !args_->has_bare_flag(kNoFlagSound)) {
                script::launch_script(kGoalGotFlagSound, nullptr, nullptr, false, nullptr);
            }
            if (args_->has_bare_flag(kUseHudCounter)) {
                script::launch_script(kGoalUpdateCounter, args_.get(), nullptr, false, nullptr);
            }
            args_->put_int(checksums::Name{kNumFlagsSet}, num_flags_set_);
        }
        changed = true;
    }
    if (!silent) {
        return changed;
    }
    if (args_->has_bare_flag(kTrickObjects)) {
        return changed;
    }
    if (has_every_bit()) {
        void* goals = objective_board();
        const uint32_t id = get_objective_key();
        complete_objective(goals, id);
    } else if (!sub && args_->has_bare_flag(kGaps) && !args_->has_bare_flag(kNoGapMessages)) {
        const script::StructPtr params = script::make_struct();
        params->append_structure_if_any(args_.get());
        script::launch_script(kGoalGapsGotGap, params.get(), nullptr, false, nullptr);
    }
    return changed;
}

// 0x005566D0
void Objective::alloc_objective_bits(script::Struct* params)
{
    goal_flags_->get_clear();
    num_flags_ = 0;
    script::Array* flags = nullptr;
    if (params->fetch_list(kGoalFlags, &flags, 0)) {
        if (flags->kind_tag() == script::QbValueKind::kStruct) {
            // One structure per skater: its own flags and counts.
            goal_flags_->put_name_key(checksums::Name{kNoName}, kSkaterTraits);
            const int skaters = static_cast<int>(flags->extent());
            for (int i = 0; i < skaters; ++i) {
                auto* entry = flags->struct_at(static_cast<uint32_t>(i));
                uint32_t skater = 0;
                entry->read_hash_value(kSkater, &skater, 1);
                const script::StructPtr own = script::make_struct();
                script::Array* names = nullptr;
                entry->fetch_list(kGoalFlags, &names, 1);
                const int count = static_cast<int>(names->extent());
                for (int k = 0; k < count; ++k) {
                    own->put_int(checksums::Name{names->fetch_part(static_cast<uint32_t>(k))}, 0);
                }
                own->put_int(checksums::Name{kNumFlags}, count);
                own->put_int(checksums::Name{kNumFlagsSet}, 0);
                goal_flags_->put_sub_struct(skater, own.get());
            }
        } else {
            for (uint32_t i = 0; i < flags->extent(); ++i) {
                goal_flags_->put_int(checksums::Name{flags->fetch_part(i)}, 0);
                ++num_flags_;
            }
        }
    }
    args_->put_int(checksums::Name{kNumFlags}, num_flags_);
}

// 0x00556240
bool Objective::all_skaters_run_done() const
{
    online::Hub* net = online::online_hub();
    PlayerSearch search;
    for (void* player = net->begin_player_walk(&search, false); player;
         player = net->step_player_walk(&search, false)) {
        objects::GameEntity* skater = player_skater(player);
        auto* end_run = core::down_cast<objects::RiderSessionEnd>(skater->fetch_module(kSkaterEndRun));
        // (in horse, a skater not taking the turn doesn't count)
        if (active_ruleset_label() == kHorse &&
            (core::down_cast<objects::Rider>(skater)->is_halted() || (skater->composite_flags() >> 2) & 1)) {
            continue;
        }
        bool done = false;
        if (run_ending_ == 0) {
            done = end_run->goal_run_done();
        } else if (run_ending_ == 1) {
            done = end_run->run_done();
        } else {
            continue;
        }
        if (!done) {
            return false;
        }
    }
    return true;
}

} // namespace goals
