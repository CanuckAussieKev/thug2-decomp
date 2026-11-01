// The goal manager's script commands, 0x0055A700-0x0055B98F (see goal_manager_cfuncs.h): trick lookups, starting,
// expiring and timing goals, their counters and flags, the end of run types, network announcements, the career
// reset, the difficulty level, the stubbed stage commands, filming, the sponsor and the team, the trick beat goals,
// the reward points, the collect goals.

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/dbg.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/trick_goals.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/down_cast.h"

namespace goals {

namespace {

using namespace goal_names;

constexpr uint32_t kName = 0xA1DC81F9;               // Name
constexpr uint32_t kKeyCombo = 0xACFDB27A;           // key_combo
constexpr uint32_t kProfile = 0x7EA855F0;            // profile
constexpr uint32_t kTrickMapping = 0xD544AA2D;       // trick_mapping
constexpr uint32_t kSpecialFlag = 0xB394C01C;        // special
constexpr uint32_t kCatNum = 0xA75B8581;             // cat_num
constexpr uint32_t kUnassigned = 0xF60C9090;         // Unassigned
constexpr uint32_t kParams = 0x7031F10C;             // params
constexpr uint32_t kTrickNameKey = 0x4443A4DA;       // trick_checksum
constexpr uint32_t kExtraTricks = 0x6E855102;        // ExtraTricks
constexpr uint32_t kExtraTrickString = 0x3DAAF58B;   // extra_trick_string
constexpr uint32_t kExtraTrickChecksum = 0xD0BCFC62; // (name unknown: the extra trick's checksum)
constexpr uint32_t kCurrentIndex = 0xDB92EFFA;       // current_index
constexpr uint32_t kDontAssert = 0x9AC8D54F;         // DontAssert
constexpr uint32_t kNumberOfFlags = 0xCB9D1224;      // number_of_flags
constexpr uint32_t kClear = 0x1A4E0EF9;              // clear
constexpr uint32_t kSkater = 0x5B8AB877;             // skater
constexpr uint32_t kTimesStarted = 0xFF61BA23;       // times_started
constexpr uint32_t kGoalEndOfRun = 0x54585419;       // Goal_EndOfRun
constexpr uint32_t kEndOfRun = 0xE022C12C;           // EndOfRun
constexpr uint32_t kNone = 0x806FFF30;               // none
constexpr uint32_t kSuspend = 0xCE0CA665;            // suspend
constexpr uint32_t kSkillTier = 0x3671F748;          // difficulty_level
constexpr uint32_t kStage = 0x5D82C486;              // (name unknown: AdvanceStage's stage)
constexpr uint32_t kForce = 0x68977CD7;              // force
constexpr uint32_t kSponsor = 0x7E73362B;            // sponsor
constexpr uint32_t kTeam = 0x3B1F59E0;               // Team
constexpr uint32_t kNumTeamMembers = 0xEB8DD330;     // num_team_members
constexpr uint32_t kPro = 0x944B2900;                // pro
constexpr uint32_t kRemove = 0x977FE2CF;             // remove
constexpr uint32_t kFlagAsUsed = 0x531FF702;         // mark_used
constexpr uint32_t kTeamName = 0x703D7582;           // team_name
constexpr uint32_t kTempo = 0x8B4A1490;              // tempo
constexpr uint32_t kBeatPattern = 0x407CCE70;        // beat_pattern
constexpr uint32_t kGoalManagerParams = 0xAC7C2B62;  // GoalManagerParams
constexpr uint32_t kTotalCheatPoints = 0x35F1301A;   // total_cheat_points
constexpr uint32_t kActivePoints = 0x477CA8C1;       // active_points
constexpr uint32_t kTotalPoints = 0x105152C0;        // total_points
constexpr uint32_t kRewardPoints = 0xBAFFB052;       // reward_points

constexpr uint8_t kMsgGoalStarted = 0x6F; // to the server: a goal (the first of its chain) started

MatchRules* get_game_mode() { return g_game_mode; }
online::Hub* net_session_slot() { return online::g_online_hub; }
objects::ProfileRoster* roster_handle() { return objects::g_profile_roster; }

uint32_t get_goal_name(script::Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    return name;
}
Objective* name_then_goal(script::Struct* params)
{
    const uint32_t name = get_goal_name(params);
    return get_objective_board()->goals_.get_pull(name);
}
// The goal Name names - none without Name (0x00476EC0, the assert passed as a bool).
Objective* optional_goal(GoalManager* manager, script::Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, true);
    return name ? manager->goals_.get_pull(name) : nullptr;
}

} // namespace

// 0x0055A700: the trick on `key_combo` in the skater's trick mapping (`profile`'s template, else the current
// profile): a create-a-trick's number (cat_num), else the trick's name and checksum (trick_string, trick_checksum)
// and its first ExtraTricks entry's (extra_trick_string, 0xD0BCFC62). With `special`, the special slot with those
// keys: its trick's name and checksum (an integer for a created trick) and its index. False if there's none.
bool cfunc_goal_manager_get_trick_from_key_combo(script::Struct* params, script::Script* script)
{
    uint32_t key_combo = 0;
    params->read_hash_value(kKeyCombo, &key_combo, 1);
    objects::RiderIdentity* profile = nullptr;
    if (params->has_entry_named(kProfile)) {
        uint32_t name = 0;
        params->read_hash_value(kProfile, &name, 1);
        profile = roster_handle()->get_template(name);
    } else {
        profile = skate_calls::get_profile_from_params(nullptr);
    }
    script::Struct* mapping = profile->fetch_details_params(kTrickMapping);
    if (!params->has_bare_flag(kSpecialFlag)) {
        int created = 0;
        if (mapping->fetch_int_value(key_combo, &created, 0)) {
            script->args_->put_int(kCatNum, created);
            return true;
        }
        uint32_t trick = 0;
        if (!mapping->read_hash_value(key_combo, &trick, 0) || trick == kUnassigned)
            return false;
        script::Struct* trick_params = nullptr;
        script::read_sub_struct(trick)->read_sub_struct(kParams, &trick_params, 1);
        const char* name = nullptr;
        trick_params->fetch_text(kName, &name, 1);
        script->args_->append_word(kTrickString, name);
        script->args_->put_name_key(kTrickNameKey, trick);
        uint32_t extra = 0;
        if (!trick_params->read_hash_value(kExtraTricks, &extra, 0)) {
            script::Array* extras = nullptr;
            if (trick_params->fetch_list(kExtraTricks, &extras, 0))
                extra = extras->fetch_part(0);
        }
        if (extra) {
            auto* entry = script::fetch_list(extra)->struct_at(0);
            script::Struct* extra_params = nullptr;
            entry->read_sub_struct(kParams, &extra_params, 1);
            const char* extra_name = nullptr;
            extra_params->fetch_text(kName, &extra_name, 1);
            script->args_->append_word(kExtraTrickString, extra_name);
            script->args_->put_name_key(kExtraTrickChecksum, extra);
        }
        return true;
    }
    if (!params->has_bare_flag(kSpecialFlag))
        return false;
    const int slots = profile->get_max_specials();
    for (int i = 0; i < slots; ++i) {
        objects::RiderIdentity::SpecialTrick special;
        profile->get_special_trick(&special, i);
        if (special.keys != key_combo)
            continue;
        if (special.trick == kUnassigned)
            return false;
        if (script::Struct* structure = script::read_sub_struct(special.trick)) {
            script::Struct* trick_params = nullptr;
            structure->read_sub_struct(kParams, &trick_params, 1);
            const char* name = nullptr;
            trick_params->fetch_text(kName, &name, 1);
            script->args_->append_word(kTrickString, name);
        }
        if (special.flag)
            script->args_->put_int(kTrickNameKey, static_cast<int>(special.trick));
        else
            script->args_->put_name_key(kTrickNameKey, special.trick);
        script->args_->put_int(kCurrentIndex, i);
        return true;
    }
    return false;
}

// 0x0055A9F0
bool cfunc_goal_manager_unlock_goal(script::Struct* params, script::Script*)
{
    name_then_goal(params)->get_unlock();
    return true;
}

// 0x0055AA40
bool cfunc_goal_manager_quick_start_goal(script::Struct* params, script::Script*)
{
    const uint32_t name = get_goal_name(params);
    const bool dont_assert = params->has_bare_flag(kDontAssert);
    return get_objective_board()->begin_objective_at_once(name, dont_assert);
}

// 0x0055AAA0
bool cfunc_goal_manager_initialize_goal(script::Struct* params, script::Script*)
{
    name_then_goal(params)->prepare_all();
    return true;
}

// 0x0055AAF0
bool cfunc_goal_manager_goal_is_expired(script::Struct* params, script::Script*)
{
    return (name_then_goal(params))->wants_to_end();
}

// 0x0055AB40
bool cfunc_goal_manager_expire_goal(script::Struct* params, script::Script*)
{
    name_then_goal(params)->expire();
    return true;
}

// 0x0055AB90: the unnamed integer's seconds added to the goal's time.
bool cfunc_goal_manager_add_time(script::Struct* params, script::Script*)
{
    const uint32_t name = get_goal_name(params);
    int seconds = 0;
    params->fetch_int_value(0u, &seconds, 1);
    get_objective_board()->goals_.get_pull(name)->advance_clock(seconds);
    return true;
}

// 0x0055ABF0
bool cfunc_goal_manager_get_number_collected(script::Struct* params, script::Script* script)
{
    const int collected = name_then_goal(params)->count_picked_up();
    script->args_->put_int(kNumberCollected, collected);
    return true;
}

// 0x0055AC50: false if the goal has no flags to win (-1).
bool cfunc_goal_manager_get_number_of_flags(script::Struct* params, script::Script* script)
{
    const int flags = name_then_goal(params)->get_num_flags_to_win();
    if (flags == -1)
        return false;
    script->args_->put_int(kNumberOfFlags, flags);
    return true;
}

// 0x0055ACB0: the cheat flag (TurnPro / open_up_every_objective).
bool cfunc_goal_manager_is_pro(script::Struct*, script::Script*) { return get_objective_board()->all_unlocked_; }

// 0x0055ACC0
bool cfunc_goal_manager_reset_goal_flags(script::Struct* params, script::Script*)
{
    name_then_goal(params)->zero_objective_bits();
    return true;
}

// 0x0055AD10: the goal's trick objects in `skater`'s colour (its player number + 1) - or cleared.
bool cfunc_goal_manager_color_trick_objects(script::Struct* params, script::Script*)
{
    const uint32_t name = get_goal_name(params);
    const bool clear = params->has_bare_flag(kClear);
    int skater = 0;
    params->fetch_int_value(kSkater, &skater, 0);
    get_objective_board()->goals_.get_pull(name)->tint_tagged_props(skater + 1, clear);
    return true;
}

// 0x0055AD90
bool cfunc_goal_manager_get_number_of_times_goal_started(script::Struct* params, script::Script* script)
{
    const int started = name_then_goal(params)->get_times_started();
    script->args_->put_int(kTimesStarted, started);
    return true;
}

// 0x0055ADF0
bool cfunc_goal_manager_goal_exists(script::Struct* params, script::Script*)
{
    return name_then_goal(params) != nullptr;
}

// 0x0055AE30: the unnamed integer (1 without one).
bool cfunc_goal_manager_set_can_start_goal(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    int can = 1;
    params->fetch_int_value(0u, &can, 0);
    manager->start_allowed_ = can != 0;
    return true;
}

// 0x0055AE70: the last goal as the script's goal_id. False if there's none.
bool cfunc_goal_manager_get_last_goal_id(script::Struct*, script::Script* script)
{
    const uint32_t last = get_objective_board()->previous_objective_;
    if (!last)
        return false;
    script->args_->put_name_key(kGoalIdChecksum, last);
    return true;
}

// 0x0055AEA0 (the goal taken to be a tetris goal)
bool cfunc_goal_manager_clear_tetris_tricks(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    core::down_cast<FallingTricksGoal>(manager->goals_.get_pull(get_goal_name(params)))->clear_tricks_and_menu();
    return true;
}

// 0x0055AEF0: a goal (not a network goal; the first of its chain) started told to the server (message 0x6F: the
// game's id and the goal's). (The local player is looked up and not used; the id word's three padding bytes are the
// original's stack garbage.)
bool cfunc_goal_manager_announce_goal_started(script::Struct* params, script::Script*)
{
    online::Hub* net = net_session_slot();
    GoalManager* manager = get_objective_board();
    Objective* goal = manager->goals_.get_pull(get_goal_name(params));
    if (goal->is_online_objective() || goal->parents_->objective_key_)
        return true;
    net::AppHost* client = net->fetch_patron(0);
    net::NetMessageInfo desc;
    net->get_own_player();
    online::GoalMessage message; // (wire/gamenet_messages.h)
    message.id = net->get_game_id();
    message.goal = goal->get_objective_key();
    std::array<uint8_t, online::kGoalMessageSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    online::write_goal_message(writer, message);
    desc.content_ = bytes.data();
    desc.byte_count_ = sizeof(bytes);
    desc.ident_ = kMsgGoalStarted;
    core::down_cast<net::Guest>(client)->send_to_host(&desc);
    return true;
}

// 0x0055AFB0: Goal_EndOfRun (the goal's own: 0), EndOfRun (1, also without any) or none (2).
bool cfunc_goal_manager_set_end_run_type(script::Struct* params, script::Script*)
{
    const uint32_t name = get_goal_name(params);
    int type = 1;
    if (params->has_bare_flag(kGoalEndOfRun))
        type = 0;
    else if (params->has_bare_flag(kEndOfRun))
        type = 1;
    else if (params->has_bare_flag(kNone))
        type = 2;
    get_objective_board()->goals_.get_pull(name)->choose_run_ending(type);
    return true;
}

// 0x0055B040: the unnamed integer.
bool cfunc_goal_manager_set_should_deactivate_on_expire(script::Struct* params, script::Script*)
{
    const uint32_t name = get_goal_name(params);
    int deactivate = 0;
    params->fetch_int_value(0u, &deactivate, 1);
    GoalManager* manager = get_objective_board();
    const bool on = deactivate != 0;
    manager->goals_.get_pull(name)->change_ends_on_timeout(on);
    return true;
}

// 0x0055B0B0: the saved state reset (everything, or the goals with the unnamed checksum's flag).
bool cfunc_goal_manager_reset_career(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    uint32_t flag = 0;
    params->read_hash_value(0u, &flag, 0);
    manager->reset_goal_data(flag);
    return true;
}

// 0x0055B0F0
bool cfunc_goal_manager_update_family_trees(script::Struct*, script::Script*)
{
    get_objective_board()->build_goal_tree();
    return true;
}

// 0x0055B110: the goal's pro's logic suspended (suspend, 1 without one) - or resumed. (Without Name the pro is read
// through null, as the original.)
bool cfunc_goal_manager_suspend_goal_ped_logic(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    int suspend = 1;
    params->fetch_int_value(kSuspend, &suspend, 0);
    Objective* goal = optional_goal(manager, params);
    goal->ped_->halt_objective_updates(suspend != 0);
    return true;
}

// 0x0055B1A0: the game mode's difficulty level set to the unnamed integer. Whether it isn't 0.
bool cfunc_goal_manager_set_difficulty_level(script::Struct* params, script::Script*)
{
    int level = 0;
    params->fetch_int_value(0u, &level, 1);
    GoalManager* manager = get_objective_board();
    manager->args_->put_int(get_game_mode()->get_name_key(), level);
    return level != 0;
}

// 0x0055B1F0: the game mode's difficulty level (0, 1 or 2; 1 if none, or another value) as the script's
// difficulty_level.
bool cfunc_goal_manager_get_difficulty_level(script::Struct*, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    int level = 0;
    if (!manager->args_->fetch_int_value(get_game_mode()->get_name_key(), &level, 0))
        level = 1;
    int result = 0;
    switch (level) {
    case 0:
        result = 0;
        break;
    case 2:
        result = 2;
        break;
    default:
        result = 1;
        break;
    }
    script->args_->put_int(kSkillTier, result);
    return true;
}

// 0x0055B260 / 0x0055B280 / 0x0055B2A0: stubbed in the game (a message only).
bool cfunc_goal_manager_restart_stage(script::Struct*, script::Script*)
{
    get_objective_board();
    debug::printf("STUBBED\n"); // "STUBBED\n"
    return true;
}
bool cfunc_goal_manager_can_restart_stage(script::Struct*, script::Script*)
{
    get_objective_board();
    debug::printf("STUBBED\n"); // "STUBBED\n"
    return false;
}
bool cfunc_goal_manager_set_goal_chapters_and_stages(script::Struct*, script::Script*)
{
    get_objective_board();
    debug::printf(
        "CGoalManager::SetGoalChaptersAndStages() Stubbed\n"); // "CGoalManager::SetGoalChaptersAndStages() Stubbed\n"
    return true;
}

// 0x0055B2C0: stubbed too (its parameters read, a message). False.
bool cfunc_goal_manager_advance_stage(script::Struct* params, script::Script*)
{
    get_objective_board();
    uint32_t stage = 0;
    params->read_hash_value(kStage, &stage, 0);
    params->has_bare_flag(kForce);
    debug::printf("STUBBED!\n"); // "STUBBED!\n"
    return false;
}

// 0x0055B310: the film goal's checkpoint passed (without Name, on null, as the original).
bool cfunc_goal_manager_film_goal_checkpoint(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    core::down_cast<ShootVideoObjective>(optional_goal(manager, params))->checkpoint();
    return true;
}

// 0x0055B370: the film goal filming (+0x70 set: the code is the competition's freeze_contest, 0x00551880).
bool cfunc_goal_manager_start_filming(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    core::down_cast<ContestObjective>(optional_goal(manager, params))->freeze_contest();
    return true;
}

// 0x0055B3D0: the unnamed checksum.
bool cfunc_goal_manager_set_sponsor(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    uint32_t sponsor = 0;
    params->read_hash_value(0u, &sponsor, 1);
    manager->backer_ = sponsor;
    return true;
}

// 0x0055B400
bool cfunc_goal_manager_get_sponsor(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(kSponsor, get_objective_board()->backer_);
    return true;
}

// 0x0055B430: the team (a structure: each member a component) and its size.
bool cfunc_goal_manager_get_team(script::Struct*, script::Script* script)
{
    script::Struct* team = get_objective_board()->team_;
    script->args_->put_sub_struct(kTeam, team);
    int members = 0;
    for (script::Component* member = team->step_to_entry(nullptr); member; member = team->step_to_entry(member))
        ++members;
    script->args_->put_int(kNumTeamMembers, members);
    return true;
}

// 0x0055B490: `pro` added to the team (1 with mark_used, else 0) - or removed.
bool cfunc_goal_manager_set_team_member(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    uint32_t pro = 0;
    params->read_hash_value(kPro, &pro, 1);
    const bool remove = params->has_bare_flag(kRemove);
    const bool mark_used = params->has_bare_flag(kFlagAsUsed);
    if (remove)
        manager->team_->erase_module(pro);
    else
        manager->team_->put_int(pro, mark_used ? 1 : 0);
    return true;
}

// 0x0055B520
bool cfunc_goal_manager_kill_team_members(script::Struct*, script::Script*)
{
    get_objective_board()->team_->get_clear();
    return true;
}

// 0x0055B540: the unnamed string as the team's team_name.
bool cfunc_goal_manager_set_team_name(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    const char* name = nullptr;
    params->fetch_text(0u, &name, 1);
    manager->team_->append_word(kTeamName, name);
    return true;
}

// 0x0055B580: nothing left in it.
bool cfunc_goal_manager_run_last_stage_script(script::Struct*, script::Script*)
{
    get_objective_board();
    return true;
}

// 0x0055B590
bool cfunc_goal_manager_goal_initialized(script::Struct* params, script::Script*)
{
    return (name_then_goal(params))->is_prepared();
}

// 0x0055B5E0 / 0x0055B630: the goal's pro's last animations unloaded / last stream stopped.
bool cfunc_goal_manager_unload_last_fam(script::Struct* params, script::Script*)
{
    name_then_goal(params)->ped_->unload_anim_bank();
    return true;
}
bool cfunc_goal_manager_stop_last_stream(script::Struct* params, script::Script*)
{
    name_then_goal(params)->ped_->cut_previous_voice_line();
    return true;
}

// 0x0055B680: the unnamed integer.
bool cfunc_goal_manager_hide_all_goal_peds(script::Struct* params, script::Script*)
{
    GoalManager* manager = get_objective_board();
    int hide = 0;
    params->fetch_int_value(0u, &hide, 1);
    manager->conceal_every_objective_npc(hide != 0);
    return true;
}

// 0x0055B6C0 (the goal taken to be a trick beat goal)
bool cfunc_goal_manager_set_trick_beat_pattern(script::Struct* params, script::Script*)
{
    auto* goal = core::down_cast<TrickBeatGoal>(name_then_goal(params));
    int tempo = 0;
    params->fetch_int_value(kTempo, &tempo, 1);
    script::Array* pattern = nullptr;
    params->fetch_list(kBeatPattern, &pattern, 1);
    goal->set_beat_pattern(pattern, tempo);
    return true;
}

// 0x0055B740
bool cfunc_goal_manager_reset_trick_beat_timer(script::Struct* params, script::Script*)
{
    core::down_cast<TrickBeatGoal>(name_then_goal(params))->reset_clock();
    return true;
}

// 0x0055B790: the saved goals' structure as the script's GoalManagerParams.
bool cfunc_goal_manager_get_goal_manager_params(script::Struct*, script::Script* script)
{
    script->args_->put_sub_struct(kGoalManagerParams, get_objective_board()->goal_data_);
    return true;
}

// 0x0055B7C0: the reward points of every goal: of those beaten (plus total_cheat_points) as active_points, of all as
// total_points.
bool cfunc_goal_manager_get_active_reward_points(script::Struct*, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    int won = 0;
    int total = 0;
    for (int i = 0; i < manager->goals_.num_; ++i) {
        Objective* goal = manager->goals_.get_by_index(i);
        const int points = goal->get_reward_points();
        if (goal->has_won())
            won += points;
        total += points;
    }
    script->args_->put_int(kActivePoints, script::fetch_int_value(kTotalCheatPoints) + won);
    script->args_->put_int(kTotalPoints, total);
    return true;
}

// 0x0055B8B0: the goal's reward points as the script's reward_points. False if it has none.
bool cfunc_goal_manager_get_goal_reward_points(script::Struct* params, script::Script* script)
{
    GoalManager* manager = get_objective_board();
    Objective* goal = manager->goals_.get_pull(get_goal_name(params));
    if (!goal->get_reward_points())
        return false;
    script->args_->put_int(kRewardPoints, goal->get_reward_points());
    return true;
}

// 0x0055B930 (Goal_Collect_Active, Goal_Gaps_Active, SkateLetters_Active): the goal goal_id won once all its flags
// are set.
bool cfunc_goal_collect_active(script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    params->read_hash_value(kGoalIdChecksum, &id, 1);
    GoalManager* manager = get_objective_board();
    if (manager->goals_.get_pull(id)->has_every_bit())
        manager->complete_objective(id);
    return true;
}

} // namespace goals
