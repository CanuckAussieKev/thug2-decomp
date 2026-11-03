// goals::CGoalManager: the skater's tricks landed and bails passed to the active goals, the view goals menu's info
// (0x0055C7E0-0x0055D3E2), and the goal manager's script commands 0x0055D3F0-0x0055D954 (see goal_manager.h,
// goal_manager_cfuncs.h).

#include <cstddef>
#include <cstdint>
#include <array>

#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_skate_line.h"
#include "game/goals/goal_net_types.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/trick_goals.h"
#include "game/rider/rider.h"
#include "game/skate/point_tally.h"
#include "engine/core/down_cast.h"
#include <gsl/pointers>

namespace goals {

namespace {
constexpr size_t kCaptionSize = 0x200;

using namespace goal_names;

constexpr uint32_t kHighCombo = 0xC63432A7;               // high_combo
constexpr uint32_t kLastScoreLanded = 0xB9072B65;         // last_score_landed
constexpr uint32_t kHighComboCheckScore = 0x6B755ED8;     // goal_highcombo_check_score
constexpr uint32_t kScoreGoal = 0x293B95A7;               // score_goal
constexpr uint32_t kWinningScore = 0x3DA891B6;            // winning_score
constexpr uint32_t kTrickObjects = 0xEFC35592;            // TrickObjects
constexpr uint32_t kTrickBeat = 0xD0DD047B;               // TrickBeat
constexpr uint32_t kSkateTheLine = 0xF268C278;            // SkateTheLine
constexpr uint32_t kArcadeCurrentScoreTotal = 0x655F4F3F; // arcade_current_score_total
constexpr uint32_t kGoalLoseNextFrame = 0x0FEFDA11;       // goal_lose_next_frame
constexpr uint32_t kTrickMarker = 0x60B827D5;             // trick_flag
constexpr uint32_t kViewGoalsText = 0x4BC5229D;           // view_goals_text
constexpr uint32_t kBeaten = 0x38318B67;                  // beaten
constexpr uint32_t kLocked = 0x152D376E;                  // locked
constexpr uint32_t kRewardPoints = 0xBAFFB052;            // reward_points
constexpr uint32_t kGoalDescription = 0x0C5D7E6B;         // goal_deScription
constexpr uint32_t kViewGoalsTip = 0x63981D6F;            // view_goals_tip
constexpr uint32_t kViewGoalsCameras = 0x22A555F7;        // view_goals_cameras
constexpr uint32_t kNumGoals = 0x5607E8CC;                // (name unknown: a pro's number of goals)
constexpr uint32_t kViewGoalsInfo = 0xBBC7B40D;           // view_goals_info
constexpr uint32_t kName = 0xA1DC81F9;                    // Name
constexpr uint32_t kParams = 0x7031F10C;                  // params
constexpr uint32_t kBeatenInLevelFlag = 0x8690A9CF;       // (name unknown: passed on, never read)
constexpr uint32_t kClearedCount = 0x8110DCDF;            // num_beaten
constexpr uint32_t kChosenOnly = 0x5685C1B7;              // selected_only
constexpr uint32_t kClassicOnly = 0xBF6C452E;             // classic_only
constexpr uint32_t kCreatedOnly = 0x185133BB;             // created_only
constexpr uint32_t kGoalIdKey = 0x7C1956AF;               // goalId
constexpr uint32_t kGoalList = 0xC7EBD48F;                // GoalList
constexpr uint32_t kAll = 0xC4E78E22;                     // ALL
constexpr uint32_t kText = 0xC4745838;                    // text
constexpr uint32_t kTrickText = 0x3EAFA520;               // tricktext
constexpr uint32_t kTryDetails = 0xD23AD129;              // (name unknown: get_objective_try_details's structure)
constexpr uint32_t kSkater = 0x5B8AB877;                  // skater

const char* const kNoDescription = "Define <goal_description> for this goal and it will appear here....If you'd like, "
                                   "also define <view_goals_tip>, it's optional."; // "Define <goal_description> ..."


// The skater's score: its total (0x00561B40, no source yet) plus the pending trick's points (the base +0x14 times the
// multiplier +0x60, truncated - _ftol).
int total_with_pending(void* score)
{
    auto* tally = static_cast<modules::PointTally*>(score);
    const int pending = static_cast<int>(static_cast<double>(tally->pot()) * tally->multiplier());
    return tally->get_total_score() + pending;
}

// goal_lose_next_frame spawned for `goal`.
void lose_next_frame(Objective* goal)
{
    const script::StructPtr params = script::make_struct();
    params->put_name_key(checksums::Name{kGoalIdChecksum}, goal->get_objective_key());
    script::spawn_script(kGoalLoseNextFrame, params.get());
}

} // namespace

// 0x0055C7E0
void GoalManager::land()
{
    test_tagged_props();
    const int count = goals_.num(); // (read once)
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (!goal->is_operative())
            continue;
        if (goal->is_horse()) {
            core::down_cast<LetterGameObjective>(goal)->land(); // (the rest skipped)
            return;
        }
        script::Struct* params = goal->fetch_args();
        if (params->has_bare_flag(kHighCombo)) {
            auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
            if (!skater)
                continue;
            auto* score = skater->get_points_total();
            if (score->pot_score() <= 0)
                continue;
            const script::StructPtr check = script::make_struct();
            check->put_name_key(checksums::Name{kGoalIdChecksum}, goal->get_objective_key());
            check->put_int(checksums::Name{kLastScoreLanded}, score->pot_score());
            script::launch_script(kHighComboCheckScore, check.get(), nullptr, false, nullptr);
        } else if (goal->fetch_args()->has_bare_flag(kScoreGoal)) {
            auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
            if (!skater)
                continue;
            void* score = skater->get_points_total();
            int needed = 0;
            goal->fetch_args()->fetch_int_value(kScore, &needed, 1);
            if (total_with_pending(score) < needed)
                continue;
            const int total = total_with_pending(score);
            goal->fetch_args()->put_int(checksums::Name{kWinningScore}, total);
            complete_objective(goal->get_objective_key());
            goal->fetch_args()->erase_module(kWinningScore);
        } else if (goal->fetch_args()->has_bare_flag(kTrickObjects)) {
            core::down_cast<TrickObjectsGoal>(goal)->evaluate_points();
        } else if (goal->fetch_args()->has_bare_flag(kTrickBeat)) {
            core::down_cast<TrickBeatGoal>(goal)->remove_landed_tricks();
        } else if (goal->fetch_args()->has_bare_flag(kSkateTheLine)) {
            core::down_cast<SkateLineGoal>(goal)->land();
        } else if (goal->fetch_args()->has_entry_named(kArcadeCurrentScoreTotal)) {
            auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
            if (!skater)
                continue;
            void* score = skater->get_points_total();
            int current = 0;
            goal->fetch_args()->fetch_int_value(kArcadeCurrentScoreTotal, &current, 0);
            if (total_with_pending(score) <= current)
                continue;
            const int total = total_with_pending(score);
            goal->fetch_args()->put_int(checksums::Name{kArcadeCurrentScoreTotal}, total);
        } else if (goal->requires_combo() && goal->fetch_args()->has_bare_flag(kChainActiveFlag)) {
            lose_next_frame(goal);
        }
    }
}

// 0x0055CBB0
void GoalManager::get_bail()
{
    const int count = goals_.num(); // (read once)
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (!goal->is_operative())
            continue;
        if (goal->requires_combo() && goal->fetch_args()->has_bare_flag(kChainActiveFlag))
            lose_next_frame(goal);
        if (goal->is_tetris()) {
            int flag = 0;
            if (goal->fetch_args()->fetch_int_value(kTrickMarker, &flag, 0) && flag)
                goal->fetch_args()->put_int(checksums::Name{kTrickMarker}, 0);
        } else if (goal->fetch_args()->has_bare_flag(kTrickBeat)) {
            core::down_cast<TrickBeatGoal>(goal)->clear_landed();
        }
    }
}

// 0x0055CD70
void GoalManager::get_view_goals_info(script::Struct* out, uint32_t skater)
{
    gsl::owner<script::Struct*> by_pro = script::new_struct(); // each pro: its goals' info by id, and their number
    gsl::owner<script::Struct*> info = script::new_struct();
    const int count = goals_.num(); // (read once)
    for (int i = 0; i < count; ++i) {
        uint32_t key = 0;
        Objective* goal = goals_.get_by_index(i, &key);
        const uint32_t pro = goal->skater();
        if (skater && pro != skater)
            continue;
        const uint32_t id = goal->get_objective_key();
        const int beaten = goal->has_won() ? 1 : 0;
        const int locked = goal->is_latched() ? 1 : 0;
        script::Struct* params = goal->fetch_args();
        const char* text = nullptr;
        params->fetch_text(kViewGoalsText, &text, 1);
        script::StructPtr entry = script::make_struct();
        entry->put_name_key(checksums::Name{kGoalIdChecksum}, id);
        entry->put_int(checksums::Name{kBeaten}, beaten);
        entry->put_int(checksums::Name{kLocked}, locked);
        entry->put_int(checksums::Name{kRewardPoints}, goal->get_reward_points());
        entry->append_word(kViewGoalsText, text);
        const char* description = nullptr;
        script::Array* descriptions = nullptr;
        if (params->fetch_text(kGoalDescription, &description, 0))
            entry->append_word(kGoalDescription, description);
        else if (params->fetch_list(kGoalDescription, &descriptions, 0))
            entry->append_array_value(kGoalDescription, descriptions);
        else
            entry->append_word(kGoalDescription, kNoDescription);
        const char* tip = nullptr;
        params->fetch_text(kViewGoalsTip, &tip, 0);
        if (tip)
            entry->append_word(kViewGoalsTip, tip);
        script::Array* cameras = nullptr;
        params->fetch_list(kViewGoalsCameras, &cameras, 0);
        if (cameras)
            entry->append_array_value(kViewGoalsCameras, cameras);
        if (!by_pro->has_entry_named(pro)) {
            const script::StructPtr empty = script::make_struct();
            by_pro->put_sub_struct(pro, empty.get());
        }
        script::Struct* goals = nullptr;
        by_pro->read_sub_struct(pro, &goals, 1);
        goals->put_sub_struct(id, entry.get());
        entry.reset();
        int number = 0;
        goals->fetch_int_value(kNumGoals, &number, 0);
        goals->put_int(checksums::Name{kNumGoals}, ++number);
    }
    // Each pro's goals made an array of their structures: all as info's arrays by pro, or - for one skater - that
    // pro's array alone (the first pro's).
    for (script::Component* component = by_pro->step_to_entry(nullptr); component;) {
        script::Component* next_pro = by_pro->step_to_entry(component);
        script::Struct* goals = component->value.structure();
        int number = 0;
        goals->fetch_int_value(kNumGoals, &number, 1);
        goals->erase_module(kNumGoals);
        gsl::owner<script::Array*> list = script::new_array();
        list->configure_size_kind(static_cast<uint32_t>(number), script::QbValueKind::kStruct);
        uint32_t index = 0;
        for (script::Component* element = goals->step_to_entry(nullptr); element;) {
            script::Component* next = goals->step_to_entry(element);
            gsl::owner<script::Struct*> copy = script::new_struct();
            copy->append_structure_if_any(element->value.structure());
            list->set_cell(index++, script::Cell::of(copy));
            element = next;
        }
        if (skater) {
            out->append_array_ref(kViewGoalsInfo, list);
            script::free_struct(by_pro);
            script::free_struct(info);
            return;
        }
        info->append_array_value(component->name, list);
        free_array(list);
        component = next_pro;
    }
    script::free_struct(by_pro);
    out->append_struct_ref(kViewGoalsInfo, info);
}

// 0x0055D3F0
bool cfunc_goal_manager_add_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    script::Struct* goal_params = nullptr;
    params->read_sub_struct(kParams, &goal_params, 1);
    return manager->enrol_objective(name, goal_params);
}

// 0x0055D450 (drop_objective, inlined)
bool cfunc_goal_manager_remove_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    return manager->drop_objective(name);
}

// 0x0055D4D0
bool cfunc_goal_manager_remove_all_goals(script::Struct*, script::Script*)
{
    get_objective_board()->wipe_objectives();
    return true;
}

// 0x0055D4F0
bool cfunc_goal_manager_restart_last_goal(script::Struct*, script::Script*)
{
    get_objective_board()->retry_previous_objective();
    return true;
}

// 0x0055D510
bool cfunc_goal_manager_level_unload(script::Struct*, script::Script*)
{
    get_objective_board()->level_unload();
    return true;
}

// 0x0055D530: the beaten goals of the level the unnamed integer names, as the script's num_beaten.
bool cfunc_goal_manager_num_goals_beaten_in_level(script::Struct* params, script::Script* script)
{
    int level = 0;
    params->fetch_int_value(0u, &level, 1);
    GoalManager* manager = get_objective_board();
    const int flag = params->has_bare_flag(kBeatenInLevelFlag);
    script->args()->put_int(checksums::Name{kClearedCount}, manager->count_cleared_in_stage(level, flag));
    return true;
}

// 0x0055D590
bool cfunc_goal_manager_add_goal_choices(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const bool selected = params->has_bare_flag(kChosenOnly);
    const bool classic = params->has_bare_flag(kClassicOnly);
    const bool created = params->has_bare_flag(kCreatedOnly);
    manager->append_objective_options(selected, classic, created);
    return true;
}

// 0x0055D600: every goal's goalId and hasBeaten as the script's GoalList.
bool cfunc_goal_manager_get_goal_list(script::Struct*, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    script::ArrayPtr list = script::make_array();
    list->configure_size_kind(static_cast<uint32_t>(manager->goals().num()), script::QbValueKind::kStruct);
    for (int i = 0; i < manager->goals().num(); ++i) {
        Objective* goal = manager->goals().get_by_index(i);
        gsl::owner<script::Struct*> entry = script::new_struct();
        entry->put_name_key(checksums::Name{kGoalIdKey}, goal->get_objective_key());
        entry->put_int(checksums::Name{kAlreadyWon}, goal->has_won() ? 1 : 0);
        list->set_cell(static_cast<uint32_t>(i), script::Cell::of(entry));
    }
    script->args()->append_array_value(kGoalList, list.get());
    list.reset();
    return true;
}

// 0x0055D7D0: with ALL every goal's texts; else goal Name's - `text` with its tricks as the script's tricktext,
// without `text` the goal's own texts (their answer). True without Name.
bool cfunc_goal_manager_replace_trick_text(script::Struct* params, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    if (params->has_bare_flag(kAll)) {
        manager->replace_trick_text_all();
        return true;
    }
    uint32_t name = 0;
    if (!params->read_hash_value(kName, &name, 0))
        return true;
    const char* text = nullptr;
    if (!params->fetch_text(kText, &text, 0))
        return manager->goals().get_pull(name)->swap_move_caption();
    std::array<char, kCaptionSize> replaced{};
    if (manager->swap_move_caption(name, text, replaced.data(), sizeof(replaced))) {
        script->args()->erase_module(kTrickText);
        script->args()->append_word(kTrickText, replaced.data());
    }
    return true;
}

// 0x0055D8E0
bool cfunc_goal_manager_get_goal_attempt_info(script::Struct*, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    script::Struct* results = script->args().get();
    results->append_struct_ref(kTryDetails, manager->get_objective_try_details());
    return true;
}

// 0x0055D910: the view goals menu's info (of `skater`'s goals only, if given).
bool cfunc_goal_manager_get_view_goals_info(script::Struct* params, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    uint32_t skater = 0;
    params->read_hash_value(kSkater, &skater, 0);
    manager->get_view_goals_info(script->args().get(), skater);
    return true;
}

} // namespace goals
