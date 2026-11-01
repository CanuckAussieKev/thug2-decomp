// goals::CGoalManager: its goals made from the level's scripts, restarted, saved at the level's end, listed for the
// menus (0x0055BA10-0x0055BE8F, 0x0055BF40-0x0055C7D6; see goal_manager.h).

#include <cstdint>

#include "engine/core/mem.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_skate_line.h"
#include "game/goals/goal_net_types.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "engine/net/gamenet_commands.h"
#include "engine/net/gamenet_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/trick_goals.h"
#include <gsl/pointers>

namespace goals {

namespace {

using namespace goal_names;

constexpr uint32_t kIsFreeskate = 0x337E7779;                    // is_freeskate
constexpr uint32_t kIsCreateGoals = 0x353961D7;                  // is_creategoals
constexpr uint32_t kCareerOnly = 0xC18FEF36;                     // career_only
constexpr uint32_t kCompetitionFlag = 0x4AF5D34E;                // competition
constexpr uint32_t kNetFlag = 0x0D15EA00;                        // net
constexpr uint32_t kRaceFlag = 0x25904450;                       // race
constexpr uint32_t kTetrisFlag = 0x4147922B;                     // tetris
constexpr uint32_t kHorseFlag = 0x9D65D0E7;                      // Horse
constexpr uint32_t kFindGapsFlag = 0xC5EC08E6;                   // findGaps
constexpr uint32_t kFilmFlag = 0x7DBB41DD;                       // Film
constexpr uint32_t kTrickObjectsFlag = 0xEFC35592;               // TrickObjects
constexpr uint32_t kSkateTheLine = 0xF268C278;                   // SkateTheLine
constexpr uint32_t kTimedScoreRun = 0xD3C2899D;                  // TimedScoreRun
constexpr uint32_t kTrickBeat = 0xD0DD047B;                      // TrickBeat
constexpr uint32_t kChildren = 0x5E684E45;                       // children
constexpr uint32_t kChild = 0xDD4CABD6;                          // child
constexpr uint32_t kPro = 0x944B2900;                            // pro
constexpr uint32_t kFullName = 0x243B9C3B;                       // full_name
constexpr uint32_t kJudgeFullName = 0xA500BC5F;                  // judge_full_name
constexpr uint32_t kSk3KillSkater = 0xDEA3057B;                  // sk3_killskater
constexpr uint32_t kSk3KillSkaterFinish = 0x0B38ED6B;            // SK3_Killskater_Finish
constexpr uint32_t kKillBlur = 0xBDA66E7D;                       // kill_blur
constexpr uint32_t kDontKillTriggerOnSuccess = 0x6BEBDBCC;       // dont_kill_trigger_on_success
constexpr uint32_t kResetGoalTrigger = 0x17C2E09F;               // GoalManager_ResetGoalTrigger
constexpr uint32_t kGoalManagerParams = 0x23D4170A;              // GoalManager_Params
constexpr uint32_t kSkillTiers = 0xB13D98D3;                     // difficulty_levels
constexpr uint32_t kNullGoal = 0x3D1CAB0B;                       // null_goal
constexpr uint32_t kParent = 0xC2719FB0;                         // parent
constexpr uint32_t kCurrentMenu = 0x4D49AC0A;                    // current_menu
constexpr uint32_t kText = 0xC4745838;                           // text
constexpr uint32_t kFont = 0x2F6BF72D;                           // font
constexpr uint32_t kSmall = 0x8ABA15EC;                          // small
constexpr uint32_t kScale = 0x13B9DA7B;                          // scale
constexpr uint32_t kViewSelectedGoalsMenuSetEvents = 0xE704C86C; // view_selected_goals_menu_set_events
constexpr uint32_t kChooseGoalsMenuSetEvents = 0x6790AC11;       // choose_goals_menu_set_events
constexpr uint32_t kTrickName = 0x5B077CE1;                      // trickname
constexpr uint32_t kButtonSequences = 0x79704516;                // key_combos
constexpr uint32_t kAttempts = 0xD023ADBE; // (name unknown: the times the beaten goals were started)

const char* const kNetJudge = "NetJudge"; // "NetJudge"

MatchRules* get_game_mode() { return g_game_mode; }
online::Hub* net_session_slot() { return online::g_online_hub; }
bool in_multiplayer_game() { return static_cast<uint8_t>(commands::cfunc_in_multiplayer_game(nullptr, nullptr)) != 0; }

script::Struct* spawn_param_table()
{
    script::Struct* made = script::spawn_param_bag();
    return made ? memory::construct_in<script::Struct>(made) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}
script::Array* alloc_script_array()
{
    script::Array* made = script::alloc_script_array();
    return made ? memory::construct_in<script::Array>(made) : nullptr;
}
void free_array(script::Array* array)
{
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
}

// The goal's type by its flags (made zeroed: Objective is a HeapObject; the original allocated the sizes noted with
// the types).
gsl::owner<Objective*> make_goal(script::Struct* params)
{
    if (params->has_bare_flag(kCompetitionFlag))
        return new ContestObjective(params);
    if (params->has_bare_flag(kNetFlag))
        return new OnlineObjective(params);
    if (params->has_bare_flag(kRaceFlag))
        return new RaceObjective(params);
    if (params->has_bare_flag(kTetrisFlag))
        return new FallingTricksGoal(params);
    if (params->has_bare_flag(kHorseFlag))
        return new LetterGameObjective(params);
    if (params->has_bare_flag(kFindGapsFlag))
        return new HuntGapsObjective(params);
    if (params->has_bare_flag(kFilmFlag))
        return new ShootVideoObjective(params);
    if (params->has_bare_flag(kTrickObjectsFlag))
        return new TrickObjectsGoal(params);
    if (params->has_bare_flag(kSkateTheLine))
        return new SkateLineGoal(params);
    if (params->has_bare_flag(kTimedScoreRun))
        return new TimedScoreRunGoal(params);
    if (params->has_bare_flag(kTrickBeat))
        return new TrickBeatGoal(params);
    return new Objective(params);
}

} // namespace

// 0x0055BA10
bool GoalManager::enrol_objective(uint32_t id, script::Struct* params)
{
    const bool free_skate = get_game_mode()->is_true(kIsFreeskate);
    if (!in_multiplayer_game() && free_skate)
        return false;
    if (get_game_mode()->is_true(kIsCreateGoals) && !params->has_bare_flag(kEditedGoal))
        return false;
    params->put_name_key(kGoalIdChecksum, id);
    if (params->has_bare_flag(kCareerOnly) && in_multiplayer_game())
        return false;
    Objective* goal = make_goal(params);
    goals_.place_in(id, goal);
    goal->merge_skill_tier_args(); // (a failed allocation is used through null, as the original)
    script::Struct* own = goal->fetch_args();
    script::Array* children = nullptr;
    if (own->fetch_list(kChildren, &children, 0)) {
        const int count = static_cast<int>(children->extent_);
        for (int i = 0; i < count; ++i) {
            children->fetch_part(i); // (read twice, as the original)
            goal->attach_child(children->fetch_part(i));
        }
    } else {
        uint32_t child = 0;
        if (own->read_hash_value(kChild, &child, 0))
            goal->attach_child(child);
    }
    if (in_multiplayer_game()) {
        goal->fetch_args()->append_word(kPro, kNetJudge);
        const char* judge = script::fetch_text(kJudgeFullName);
        goal->fetch_args()->append_word(kFullName, judge);
    }
    return goal != nullptr;
}

// 0x0055BF40
void GoalManager::retry_previous_objective()
{
    if (!previous_objective_)
        return;
    Objective* goal = goals_.get_pull(previous_objective_);
    goal->set_quick_start();
    disengage_every_objective(false);
    script::stop_scripts_targeting(kSk3KillSkater);
    script::stop_scripts_targeting(kSk3KillSkaterFinish);
    script::launch_script(kKillBlur, nullptr, nullptr, false, nullptr);
    if (!goal->has_won() || goal->fetch_args()->has_bare_flag(kDontKillTriggerOnSuccess)) {
        script::Struct* params = spawn_param_table();
        params->put_name_key("goal_id", goal->get_objective_key()); // "goal_id"
        script::launch_script(kResetGoalTrigger, params, nullptr, false, nullptr);
        delete_structure(params);
    }
    goal->activate();
    goal->unset_quick_start();
}

// 0x0055C090
void GoalManager::level_unload()
{
    if (net_session_slot()->in_net_game())
        return;
    script::Struct* own = spawn_param_table();
    own->put_sub_struct(kSkillTiers, args_);
    goal_data_->put_sub_struct(kGoalManagerParams, own);
    delete_structure(own);
    for (int i = 0; i < goals_.num_; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (goal->fetch_args()->has_bare_flag(kNullGoal) || goal->is_user_made_objective())
            continue;
        script::Struct* data = spawn_param_table();
        goal->capture_save_block(data);
        goal_data_->put_sub_struct(goal->get_objective_key(), data);
        delete_structure(data);
    }
}

// 0x0055C250
int GoalManager::count_cleared_in_stage(int level, int)
{
    level_unload();
    int count = 0;
    script::Component* component = goal_data_->step_to_entry(nullptr);
    while (component) {
        script::Component* next = goal_data_->step_to_entry(component);
        int goal_level = 0;
        if (component->value.structure()->fetch_int_value(kLevelNum, &goal_level, 0)) {
            int beaten = 0;
            component->value.structure()->fetch_int_value(kAlreadyWon, &beaten, 0);
            if (goal_level == level && beaten)
                ++count;
        }
        component = next;
    }
    return count;
}

// 0x0055C2F0
void GoalManager::append_objective_options(bool selected_only, bool skip_edited, bool edited_only)
{
    for (int i = 0; i < goals_.num_; ++i) {
        Objective* goal = goals_.get_by_index(i);
        goal->swap_move_caption();
        const char* text = nullptr;
        goal->get_objective_list_caption(&text);
        if (!text || goal->is_online_objective() || goal->parents_->objective_key_)
            continue;
        online::cfunc_is_host(nullptr, nullptr); // (its answer isn't used)
        if (selected_only && !goal->is_picked())
            continue;
        if (skip_edited && goal->is_edited_goal2())
            continue;
        if (edited_only && !goal->is_edited_goal2())
            continue;
        script::Struct* choice = spawn_param_table();
        choice->put_name_key(kGoalIdChecksum, goal->get_objective_key());
        choice->put_name_key(kParent, kCurrentMenu);
        choice->append_word(kText, text);
        choice->put_name_key(kFont, kSmall);
        choice->put_int(kScale, 0);
        script::launch_script(selected_only ? kViewSelectedGoalsMenuSetEvents : kChooseGoalsMenuSetEvents, choice,
                              nullptr, false, nullptr);
        delete_structure(choice);
    }
}

// 0x0055C4D0
bool GoalManager::swap_move_caption(uint32_t id, const char* text, char* out, int size)
{
    script::Struct* params = goals_.get_pull(id)->fetch_args();
    script::Array* key_combos = nullptr;
    params->fetch_list(kButtonSequences, &key_combos, 1);
    script::Array* key_texts = alloc_script_array();
    script::Array* trick_texts = alloc_script_array();
    const uint32_t count = key_combos->extent_;
    key_texts->configure_size_kind(count, script::kQbvalString);
    trick_texts->configure_size_kind(count, script::kQbvalString);
    int premade = -1; // (the created trick's number, with create_a_trick)
    if (params->has_bare_flag(kCreateATrick))
        params->fetch_int_value(kTrickName, &premade, 1);
    const bool done = Objective::build_trick_texts(key_combos, key_texts, trick_texts, premade);
    if (done)
        Objective::replace_trick_tokens(text, out, key_texts, trick_texts, size);
    script::release_array_storage(key_texts);
    script::release_array_storage(trick_texts);
    free_array(key_texts);
    free_array(trick_texts);
    return done;
}

// 0x0055C6E0
script::Struct* GoalManager::get_objective_try_details()
{
    const int count = goals_.num_; // (read once)
    script::Struct* info = spawn_param_table();
    int attempts = 0;
    for (int i = 0; i < count; ++i) {
        Objective* goal = goals_.get_by_index(i);
        if (!goal->is_online_objective() && goal->has_won())
            attempts += goal->get_times_started();
    }
    info->put_int(kAttempts, attempts);
    return info;
}

} // namespace goals
