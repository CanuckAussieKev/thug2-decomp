// The network manager's message handlers about the game itself (online::Hub): new players, the crown, CTF teams,
// goal attack goals, the created rails and goals arriving, eliminations, join refusals. See gamenet_handlers.h.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "engine/core/code_address.h"
#include "game/park_editing/park.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kNetMessageNewPlayer = 0x640D3C87;     // net_message_new_player
constexpr uint32_t kDroppedCrownYou = 0xD10CDB7C;         // net_message_dropped_crown_you
constexpr uint32_t kDroppedCrownOther = 0x484CE1C7;       // net_message_dropped_crown_other
constexpr uint32_t kType = 0x7321A8D6;                    // Type
constexpr uint32_t kCrownSpawnNode = 0xAF86421B;          // (a node type: where the crown may appear)
constexpr uint32_t kChecksum = 0x21902065;                // checksum
constexpr uint32_t kLaunchFcfsNotification = 0xEB123AF2;  // launch_fcfs_notification
constexpr uint32_t kTeam = 0x3B1F59E0;                    // Team
constexpr uint32_t kString0 = 0xA4B08520;                 // String0
constexpr uint32_t kString1 = 0xD3B7B5B6;                 // String1
constexpr uint32_t kFlagReturned = 0x3B1F2478;            // flag_returned
constexpr uint32_t kDestroyCtfPanelMessage = 0x514A53A6;  // destroy_ctf_panel_message
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;            // hide_ctf_arrow
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;            // Load_MainMenu
constexpr uint32_t kShowTimeout = 0x19ECA78E;             // show_timeout
constexpr uint32_t kCancelJoinServer = 0x60B653DB;        // cancel_join_server
constexpr uint32_t kDoNotCreateGoalPeds = 0x88001327;     // DoNotCreateGoalPeds
constexpr uint32_t kInitialiseCreatedGoals = 0x6F4180D0;  // InitialiseCreatedGoals
constexpr uint32_t kGoalSummary = 0x40FC9B60;             // goal_summary
constexpr uint32_t kViewSelectedGoalsMenu = 0x8DF3084E;   // wait_and_create_view_selected_goals_menu
constexpr uint32_t kJoinedTeamYou = 0x92EFAD9E;           // joined_team_you
constexpr uint32_t kJoinedTeamOther = 0x9D62E743;         // joined_team_other
constexpr uint32_t kWarnAllSameTeam = 0x16C9B0DC;         // warn_all_same_team
constexpr uint32_t kNumGoalsLeft = 0x684A8396;            // NumGoalsLeft
constexpr uint32_t kCompletedGoal = 0xFFB3D309;           // goal_attack_completed_goal
constexpr uint32_t kCompletedGoalSameTeam = 0xF4748E47;   // goal_attack_completed_goal_other_same_team
constexpr uint32_t kStartedGoalSameTeam = 0xA77758DE;     // goal_attack_started_goal_other_same_team
constexpr uint32_t kPlayerName = 0x9196D920;              // PlayerName
constexpr uint32_t kGoalText = 0xB8A88B50;                // GoalText
constexpr uint32_t kEliminatedNone = 0x4BA23859;          // EliminatedNone
constexpr uint32_t kEliminatedYou = 0x35E06259;           // EliminatedYou
constexpr uint32_t kEliminatedOther = 0x1202DEE4;         // EliminatedOther
constexpr uint32_t kReason = 0xC44777F3;                  // reason
constexpr uint32_t kFullObservers = 0xDDE5676F;           // net_reason_full_observers
constexpr uint32_t kJustDialog = 0xF6694E96;              // just_dialog
constexpr uint32_t kCreateJoinRefusedDialog = 0x92528E98; // CreateJoinRefusedDialog

constexpr uint8_t kMsgEliminated = 0x8D;
constexpr uint8_t kMsgRequestRails = 0x8B;
constexpr uint8_t kMsgRequestGoals = 0x8A;

constexpr int kPanelTime = 2000;

// 0x005376F0: a panel message (broadcast, text, string 1, string 2, object, properties, important, time).
void panel_message(Hub* manager, uint32_t text, const char* string1, objects::Entity* object)
{
    manager->build_net_notice(false, text, string1, nullptr, object, nullptr, false, kPanelTime);
}

// The rail editor (0x004F4970) and the goal editor (0x004E8470).
void* get_rail_editor() { return park_editing::RailAuthoring::sole(); }
void* get_goal_editor() { return park_editing::ObjectiveAuthoring::get_pull(); }

// Leaving for the main menu while the created data couldn't come: the join cancelled with a time-out notice.
void cancel_join(Hub* manager, bool notify_first)
{
    script::Struct* params = nullptr;
    if (notify_first) {
        params = new_params();
        params->put_name_key(0u, kShowTimeout);
    }
    manager->join_phase_job_->drop_out();
    manager->set_entry_phase(2);
    if (!notify_first) {
        params = new_params();
        params->put_name_key(0u, kShowTimeout);
    }
    get_run(kCancelJoinServer, params);
    delete_params(params);
}

} // namespace

// 0x0052DB80
int __cdecl on_player_added_msg(net::InboundMsgCtx* context)
{
    const uint8_t* msg = get_message(context);
    Hub* manager = manager_of(context);
    alignas(4) uint8_t info[0x44];                     // a JoinerRecord
    reinterpret_cast<JoinerRecord*>(info)->make_new(); // (its constructor: a skater profile at +0x1C)
    // (strncpy: 15 characters at most, the rest of the 15 zeroed)
    size_t length = strnlen(reinterpret_cast<const char*>(msg + 0x20), 15);
    std::memcpy(info, msg + 0x20, length);
    std::memset(info + length, 0, 15 - length);
    info[0x0F] = 0;
    info[0x10] = msg[0x18];                                 // the object id
    prop<uint32_t>(info, 0x14) = 0;                         // the connection
    prop<uint32_t>(info, 0x20) = prop<uint32_t>(msg, 0x08); // flags
    prop<int>(info, 0x28) = static_cast<int8_t>(msg[0x19]); // the team
    prop<uint32_t>(info, 0x2C) = prop<uint32_t>(msg, 0x00);
    prop<uint32_t>(info, 0x30) = prop<uint32_t>(msg, 0x04);
    prop<uint32_t>(info, 0x38) = prop<uint32_t>(msg, 0x14);
    prop<uint32_t>(info, 0x40) = prop<uint32_t>(msg, 0x1C);
    prop<uint32_t>(info, 0x34) = prop<uint32_t>(msg, 0x0C);
    if (!(prop<uint32_t>(info, 0x20) & 5))
        prop<objects::RiderIdentity*>(info, 0x1C)
            ->decode_from_bytes(const_cast<uint8_t*>(msg + 0x30)); // the appearance into its profile
    int delay = 0;
    if (manager->cleared_to_start_) {
        manager->build_net_notice(false, kNetMessageNewPlayer, reinterpret_cast<const char*>(info), nullptr, nullptr,
                                  nullptr, false, kPanelTime);
        delay = 10;
    }
    manager->add_joining_player(info, delay);              // the new player (added in `delay` frames)
    reinterpret_cast<JoinerRecord*>(info)->destroy_body(); // (its destructor)
    return kContinue;
}

// 0x0052E870
int __cdecl handle_crown_spawn(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    uint8_t* king = is_crown_rule_active(manager);
    char spot = static_cast<char>(get_message(context)[0]);
    if (king) {
        as_player(king)->give_crown(false); // no longer the king
    }
    if (king && is_own_player(king)) {
        panel_message(manager, kDroppedCrownYou, nullptr, player_skater(king));
    } else if (manager->in_net_game()) {
        panel_message(manager, kDroppedCrownOther, nullptr, nullptr);
    } else if (king && player_skater(king)) {
        // the other local player (id 1 if this one's is 0, else 0)
        uint8_t* other =
            find_player_slot_by_obj(manager, static_cast<uint16_t>(get_skater_id(player_skater(king)) == 0));
        if (other)
            panel_message(manager, kDroppedCrownOther, nullptr, player_skater(other));
    }
    auto* crown = static_cast<objects::CrownObject*>(manager->get_crown_holder());
    if (!crown)
        return kContinue;
    // The crown to the spot'th crown node (counted on across up to 49 walks of the nodes).
    bool placed = false;
    for (int walks = 1; walks != 50 && !placed; ++walks) {
        level_nodes::start_node_walk(0);
        for (script::Struct* node = level_nodes::get_next_node(); node; node = level_nodes::get_next_node()) {
            uint32_t type = 0;
            node->read_hash_value(kType, &type, 0);
            if (type != kCrownSpawnNode)
                continue;
            if (spot == 0) {
                maths::Vec4 position;
                level_nodes::fetch_place(node, &position);
                crown->at_point_ = position;
                placed = true;
                break;
            }
            --spot;
        }
        level_nodes::end_node_walk();
    }
    crown->take_off();
    return kContinue;
}

// 0x005300C0
int __cdecl handle_fcfs_notice(net::InboundMsgCtx* context)
{
    uint8_t* local = find_own_player_record(manager_of(context));
    const uint8_t* msg = get_message(context);
    as_player(local)->set_host_player(); // (its flag 0x200 set)
    script::Struct* params = new_params();
    params->put_name_key(kChecksum, prop<uint32_t>(msg, 0));
    get_run(kLaunchFcfsNotification, params);
    delete_params(params);
    return kContinue;
}

// 0x00530CB0
int __cdecl handle_flag_returned(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    uint8_t* player = find_player_slot_by_obj(manager, get_message(context));
    if (!player || !as_player(player)->is_carrying_flag()) // (carrying a flag: 0 / 1)
        return kContinue;
    int team = as_player(player)->get_flag_team(); // the flag's team
    char name[64];
    team_name_key(name, team);
    script::Struct* params = new_params();
    params->put_int(kTeam, team);
    params->append_word(kString0, script::kQbvalString, script::fetch_text(name));
    get_run(kFlagReturned, params);
    delete_params(params);
    if (is_own_player(player))
        get_run(kDestroyCtfPanelMessage);
    uint8_t* local = find_own_player_record(manager);
    if (local && !is_observer(local) && player_team(local) == team)
        get_run(kHideCtfArrow);
    as_player(player)->clear_flags(); // carries no flag
    return kContinue;
}

// 0x00530E50
int __cdecl handle_rails_data(net::InboundMsgCtx* context)
{
    uint8_t* skate = get_skate();
    Hub* manager = manager_of(context);
    if (!(context->wire_options_ & net::kPacketChecksumMismatch)) {
        memory::Hub* memory = memory::Hub::get_pull();
        memory->enter_heap_scope(prop<memory::MemSource*>(memory, 0x114));
        get_call(code_address(&park_editing::RailAuthoring::set_packed), get_rail_editor(),
                 context->body_); // the rails read
        park_editing::g_rail_editor_flag = false;
        get_call(code_address(&park_editing::RailAuthoring::unpack), get_rail_editor());
        park_editing::g_rail_editor_flag = true;
        memory::Hub::get_pull()->leave_heap_scope();
        return kContinue;
    }
    net::NetMessageInfo desc;
    desc.make_new();
    if (prop<uint32_t>(skate, 0x378) == kLoadMainMenu) {
        cancel_join(manager, true);
        return kKeep;
    }
    desc.ident_ = kMsgRequestRails; // again
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    send_to_server(context->receiver_app_, &desc);
    return kDone;
}

// 0x00530FF0
int __cdecl on_goal_sync_msg(net::InboundMsgCtx* context)
{
    net::NetMessageInfo desc;
    desc.make_new();
    Hub* manager = manager_of(context);
    if (!(context->wire_options_ & net::kPacketChecksumMismatch)) {
        const char* msg = context->body_;
        get_call(code_address(&park_editing::ObjectiveAuthoring::read_level_goals_from_buffer), get_goal_editor(),
                 prop<uint32_t>(msg, 0), msg + 4); // the level's goals read
        script::Struct* params = new_params();
        params->put_name_key(0u, kDoNotCreateGoalPeds);
        get_run(kInitialiseCreatedGoals, params);
        delete_params(params);
        return kContinue;
    }
    if (prop<uint32_t>(get_skate(), 0x378) == kLoadMainMenu) {
        cancel_join(manager, false);
        return kKeep;
    }
    desc.ident_ = kMsgRequestGoals; // again
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    send_to_server(context->receiver_app_, &desc);
    return kDone;
}

// 0x0052E760
int __cdecl on_crown_change_msg(net::InboundMsgCtx* context)
{
    const uint8_t* msg = get_message(context);
    Hub* manager = manager_of(context);
    NewPlayerSearch search;
    // The crown off the old king - or off a new player waiting to be added.
    if (uint8_t* king = is_crown_rule_active(manager)) {
        as_player(king)->give_crown(false);
    } else {
        for (uint8_t* added = first_new_player(manager, &search); added; added = next_new_player(manager, &search)) {
            if (prop<uint32_t>(added, 0x20) & 0x80) {
                prop<uint32_t>(added, 0x20) &= ~0x80u;
                break;
            }
        }
    }
    // Onto the player of object id msg[0] - or the new player of that id.
    if (uint8_t* player = find_player_slot_by_obj(manager, msg)) {
        as_player(player)->give_crown(true);
        return kContinue;
    }
    for (uint8_t* added = first_new_player(manager, &search); added; added = next_new_player(manager, &search)) {
        if (added[0x10] == msg[0]) {
            prop<uint32_t>(added, 0x20) |= 0x80;
            break;
        }
    }
    return kContinue;
}

// 0x00531950
int __cdecl handle_selected_goals(net::InboundMsgCtx* context)
{
    const uint8_t* msg = get_message(context);
    uint8_t* goals = objective_board();
    get_call(code_address(&goals::GoalManager::unpick_every_objective), goals);
    uint8_t show = msg[0];
    const uint8_t* name = msg + 1;
    for (uint32_t goal = prop<uint32_t>(name, 0); goal; goal = prop<uint32_t>(name, 0)) {
        name += 4;
        uint8_t* entry = get_goal(goals, goal);
        reinterpret_cast<goals::Objective*>(entry)->un_beat(); // (the original's vtable +0x74)
        reinterpret_cast<goals::Objective*>(entry)->choose_one();
    }
    if (show == 1) {
        script::Struct* params = new_params();
        params->put_name_key(kGoalSummary, kGoalSummary);
        script::spawn_script(kViewSelectedGoalsMenu, params, 0, nullptr, 0, 0, false, false, false, false);
        delete_params(params);
    }
    return kContinue;
}

// 0x00531A80
int __cdecl handle_joined_team(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    Hub* manager = manager_of(context);
    const uint8_t* msg = get_message(context);
    uint8_t* player = find_player_slot_by_obj(manager, msg);
    if (!player)
        return kContinue;
    prop<int>(player, 0x30) = static_cast<int8_t>(msg[1]);
    char name[64];
    team_name_key(name, static_cast<int8_t>(msg[1]));
    script::Struct* params = new_params();
    if (is_own_player(player)) {
        if (is_squad_match()) {
            params->append_word(kString0, script::kQbvalString, script::fetch_text(name));
            get_run(kJoinedTeamYou, params);
        }
    } else if (is_squad_match()) {
        params->append_word(kString0, script::kQbvalString, player_name(player));
        params->append_word(kString1, script::kQbvalString, script::fetch_text(name));
        get_run(kJoinedTeamOther, params);
    }
    delete_params(params);
    // Everyone else on the same team: a warning.
    int others = 0;
    bool same_team = true;
    for (uint8_t* other = start_roster_sweep(manager, &search, false); other;
         other = step_roster_sweep(manager, &search, false)) {
        if (other == player)
            continue;
        ++others;
        if (static_cast<int8_t>(msg[1]) != player_team(other))
            same_team = false;
    }
    if (same_team && others > 0 &&
        get_call<int>(code_address(&goals::MatchRules::current_num_teams), get_game_mode()) > 0)
        get_run(kWarnAllSameTeam);
    return kContinue;
}

// 0x00531C80
int __cdecl handle_goal_completed(net::InboundMsgCtx* context)
{
    const uint8_t* msg = get_message(context);
    Hub* manager = manager_of(context);
    uint8_t* goals = objective_board();
    uint8_t* goal = get_goal(goals, prop<uint32_t>(msg, 4));
    uint8_t* current = get_call<uint8_t*>(code_address(&goals::GoalManager::get_engaged_objective), goals, 0);
    if (!goal)
        return kContinue;
    uint8_t* player = find_player_slot_by_obj(manager, msg);
    uint8_t* local = find_own_player_record(manager);
    auto* objective = reinterpret_cast<goals::Objective*>(goal);
    objective->set_net_stage_flag(player_team(player)); // (the original's slot 0x70)
    if (!player_skater(player))
        return kContinue;
    objective->set_stage_flag(static_cast<int>(get_skater_id(player_skater(player)))); // (slot 0x6C)
    if (local == player) {
        int total = get_call<int>(code_address(&goals::GoalManager::count_picked_objectives), goals);
        int done = get_call<int>(code_address(&goals::GoalManager::count_objectives_cleared), goals);
        if (total > done) {
            script::Struct* params = new_params();
            params->put_int(kNumGoalsLeft, total - done);
            get_run(kCompletedGoal, params);
            delete_params(params);
        }
        get_call(code_address(&goals::ObjectiveNpc::remove_objective_npc), prop<void*>(goal, 0x14));
        return kContinue;
    }
    if (current) {
        reinterpret_cast<goals::Objective*>(current)->get_top_objective_key();
        reinterpret_cast<goals::Objective*>(goal)->get_top_objective_key();
    }
    if (!is_squad_match() || player_team(local) != player_team(player))
        return kContinue;
    // A teammate's: the goal (or the last of its chain) ended here too.
    uint32_t* next = prop<uint32_t*>(goal, 0x28);
    if (*next) {
        uint8_t* last = goal;
        while (next && *next) {
            last = get_goal(goals, *next);
            next = prop<uint32_t*>(last, 0x28);
        }
        reinterpret_cast<goals::Objective*>(last)->get_win(); // (slot 0x3C)
        if (last != current)
            reinterpret_cast<goals::Objective*>(current)->disengage(false, true); // (slot 0x34)
    } else {
        objective->get_win();
    }
    get_call(code_address(&goals::ObjectiveNpc::remove_objective_npc), prop<void*>(current, 0x14));
    objective->mark_won(); // (slot 0x68)
    int total = get_call<int>(code_address(&goals::GoalManager::count_picked_objectives), goals);
    int done = get_call<int>(code_address(&goals::GoalManager::count_objectives_cleared), goals);
    if (total > done) {
        const char* text = nullptr;
        reinterpret_cast<goals::Objective*>(goal)->get_objective_list_caption(&text);
        script::Struct* params = new_params();
        params->put_int(kNumGoalsLeft, total - done);
        params->append_word(kPlayerName, player_name(player));
        params->append_word(kGoalText, text);
        get_run(kCompletedGoalSameTeam, params);
        delete_params(params);
    }
    return kContinue;
}

// 0x00531F10
int __cdecl handle_goal_started(net::InboundMsgCtx* context)
{
    const uint8_t* msg = get_message(context);
    Hub* manager = manager_of(context);
    uint8_t* goal = get_goal(objective_board(), prop<uint32_t>(msg, 4));
    if (!goal)
        return kContinue;
    const char* text = nullptr;
    uint8_t* player = find_player_slot_by_obj(manager, msg);
    if (player_team(find_own_player_record(manager)) != player_team(player))
        return kContinue;
    reinterpret_cast<goals::Objective*>(goal)->get_objective_list_caption(&text);
    script::Struct* params = new_params();
    params->append_word(kPlayerName, player_name(player));
    params->append_word(kGoalText, text);
    get_run(kStartedGoalSameTeam, params);
    delete_params(params);
    return kContinue;
}

// 0x00532280
int __cdecl handle_eliminated(net::InboundMsgCtx* context)
{
    if (context->type_code_ != kMsgEliminated)
        return kContinue;
    int id = static_cast<int>(message_word(context, 0));
    if (id < 0) {
        get_run(kEliminatedNone);
        return kContinue;
    }
    uint8_t* player = find_player_slot_by_obj(manager_of(context), static_cast<uint16_t>(id));
    if (!player)
        return kContinue;
    uint8_t* skate = get_skate();
    as_player(player)->mark_eliminated();
    get_call(code_address(&modules::Skate::conceal_skater), skate, player_skater(player), 1);
    if (is_own_player(player)) {
        get_run(kEliminatedYou);
        return kContinue;
    }
    script::Struct* params = new_params();
    params->append_word(kString0, player_name(player));
    get_run(kEliminatedOther, params);
    delete_params(params);
    return kContinue;
}

// 0x00532390
int __cdecl handle_observers_full(net::InboundMsgCtx*)
{
    script::Struct* params = new_params();
    params->put_name_key(kReason, kFullObservers);
    params->put_name_key(kJustDialog, 0);
    get_run(kCreateJoinRefusedDialog, params);
    delete_params(params);
    return kContinue;
}

} // namespace online
