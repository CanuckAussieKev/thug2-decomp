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
#include "engine/net/gamenet_messages.h"
#include "engine/util/byte_stream.h"

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

// The rail editor (0x004F4970); the goal editor is ObjectiveAuthoring::get_pull (0x004E8470).
void* get_rail_editor() { return park_editing::RailAuthoring::sole(); }

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
int on_player_added_msg(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    // (the message: gamenet_messages.h; NOT ORIGINAL: one too short for its header is dropped - the original read
    // past it -, and the appearance is read from a copy as long as the most a message holds)
    util::ByteReader reader(context->body_, context->body_size_);
    PlayerArrived arrived;
    if (!read_player_arrived(reader, &arrived))
        return kDone;
    JoinerRecord record; // (on the stack; its constructor: a skater profile)
    JoinerRecord* info = &record;
    // (15 characters at most, the rest zeroed)
    std::memset(info->label_, 0, sizeof(info->label_));
    std::memcpy(info->label_, arrived.name, strnlen(arrived.name, 15));
    info->entity_key_ = static_cast<uint8_t>(arrived.object_id); // the object id
    info->conn_ = nullptr;                                       // the connection
    info->flag_bits_ = arrived.flags;                            // flags
    info->team_ = arrived.team;                                  // the team
    info->profile_id_ = arrived.profile_id;
    info->skill_points_ = arrived.rating;
    info->accumulated_score_ = arrived.accumulated_score;
    info->drive_control_mode_ = arrived.drive_scheme;
    info->score_ = arrived.score;
    if (!(info->flag_bits_ & 5)) {
        static uint8_t appearance[kAppearanceSize];
        size_t size = reader.remaining() < kAppearanceSize ? reader.remaining() : kAppearanceSize;
        std::memset(appearance, 0, sizeof(appearance));
        reader.bytes(appearance, size);
        info->account_->decode_from_bytes(appearance, sizeof(appearance)); // the appearance into its profile
    }
    int delay = 0;
    if (manager->cleared_to_start_) {
        manager->build_net_notice(false, kNetMessageNewPlayer, info->label_, nullptr, nullptr, nullptr, false,
                                  kPanelTime);
        delay = 10;
    }
    manager->add_joining_player(info, delay); // the new player (added in `delay` frames)
    return kContinue;                         // (the record's destructor on the way out)
}

// 0x0052E870
int handle_crown_spawn(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    util::ByteReader reader(context->body_, context->body_size_);
    char spot = static_cast<char>(reader.u8());
    if (!reader.ok()) // NOT ORIGINAL: empty - dropped
        return kDone;
    PlayerRecord* king = is_crown_rule_active(manager);
    if (king) {
        king->give_crown(false); // no longer the king
    }
    if (king && is_own_player(king)) {
        panel_message(manager, kDroppedCrownYou, nullptr, player_skater(king));
    } else if (manager->in_net_game()) {
        panel_message(manager, kDroppedCrownOther, nullptr, nullptr);
    } else if (king && player_skater(king)) {
        // the other local player (id 1 if this one's is 0, else 0)
        PlayerRecord* other =
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
int handle_fcfs_notice(net::InboundMsgCtx* context)
{
    PlayerRecord* local = find_own_player_record(manager_of(context));
    util::ByteReader reader(context->body_, context->body_size_);
    uint32_t checksum = reader.u32();
    if (!reader.ok()) // NOT ORIGINAL: too short - dropped
        return kDone;
    local->set_host_player(); // (its flag 0x200 set)
    script::Struct* params = new_params();
    params->put_name_key(kChecksum, checksum);
    get_run(kLaunchFcfsNotification, params);
    delete_params(params);
    return kContinue;
}

// 0x00530CB0
int handle_flag_returned(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    util::ByteReader reader(context->body_, context->body_size_);
    uint8_t id = reader.u8();
    if (!reader.ok()) // NOT ORIGINAL: empty - dropped
        return kDone;
    PlayerRecord* player = find_player_slot_by_obj(manager, &id);
    if (!player || !player->is_carrying_flag()) // (carrying a flag: 0 / 1)
        return kContinue;
    int team = player->get_flag_team(); // the flag's team
    char name[64];
    team_name_key(name, team);
    script::Struct* params = new_params();
    params->put_int(kTeam, team);
    params->append_word(kString0, script::kQbvalString, script::fetch_text(name));
    get_run(kFlagReturned, params);
    delete_params(params);
    if (is_own_player(player))
        get_run(kDestroyCtfPanelMessage);
    PlayerRecord* local = find_own_player_record(manager);
    if (local && !is_observer(local) && player_team(local) == team)
        get_run(kHideCtfArrow);
    player->clear_flags(); // carries no flag
    return kContinue;
}

// 0x00530E50
int handle_rails_data(net::InboundMsgCtx* context)
{
    auto* skate = get_skate();
    Hub* manager = manager_of(context);
    if (!(context->wire_options_ & net::kPacketChecksumMismatch)) {
        memory::Hub* memory = memory::Hub::get_pull();
        memory->enter_heap_scope(memory->bottom_up_heap_);
        static_cast<park_editing::RailAuthoring*>(get_rail_editor())
            ->set_packed(context->body_, context->body_size_); // the rails read
        park_editing::g_rail_editor_flag = false;
        static_cast<park_editing::RailAuthoring*>(get_rail_editor())->unpack();
        park_editing::g_rail_editor_flag = true;
        memory::Hub::get_pull()->leave_heap_scope();
        return kContinue;
    }
    net::NetMessageInfo desc;
    if (reinterpret_cast<modules::Skate*>(skate)->level_ == kLoadMainMenu) {
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
int on_goal_sync_msg(net::InboundMsgCtx* context)
{
    net::NetMessageInfo desc;
    Hub* manager = manager_of(context);
    if (!(context->wire_options_ & net::kPacketChecksumMismatch)) {
        // {u32 level, the goals as a written structure} (NOT ORIGINAL: read within the message - a short one is
        // dropped, the structure decoded no further than its end)
        util::ByteReader reader(context->body_, context->body_size_);
        uint32_t level = reader.u32();
        if (!reader.ok())
            return kDone;
        park_editing::ObjectiveAuthoring::get_pull()->read_level_goals_from_buffer(level, reader.cursor(),
                                                                                   reader.remaining());
        script::Struct* params = new_params();
        params->put_name_key(0u, kDoNotCreateGoalPeds);
        get_run(kInitialiseCreatedGoals, params);
        delete_params(params);
        return kContinue;
    }
    if (modules::g_skate_module->level_ == kLoadMainMenu) {
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
int on_crown_change_msg(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    const uint8_t id = reader.u8();
    if (!reader.ok()) // NOT ORIGINAL: empty - dropped
        return kDone;
    const uint8_t* msg = &id; // (the new king's object id)
    Hub* manager = manager_of(context);
    NewPlayerSearch search;
    // The crown off the old king - or off a new player waiting to be added.
    if (PlayerRecord* king = is_crown_rule_active(manager)) {
        king->give_crown(false);
    } else {
        for (JoinerRecord* added = first_new_player(manager, &search); added;
             added = next_new_player(manager, &search)) {
            if (added->flag_bits_ & 0x80) {
                added->flag_bits_ &= ~0x80u;
                break;
            }
        }
    }
    // Onto the player of object id msg[0] - or the new player of that id.
    if (PlayerRecord* player = find_player_slot_by_obj(manager, msg)) {
        player->give_crown(true);
        return kContinue;
    }
    for (JoinerRecord* added = first_new_player(manager, &search); added; added = next_new_player(manager, &search)) {
        if (added->entity_key_ == msg[0]) {
            added->flag_bits_ |= 0x80;
            break;
        }
    }
    return kContinue;
}

// 0x00531950
int handle_selected_goals(net::InboundMsgCtx* context)
{
    const uint8_t* msg = get_message(context);
    auto* goals = objective_board();
    static_cast<goals::GoalManager*>(goals)->unpick_every_objective();
    // (a byte, then the goals' names up to a 0; NOT ORIGINAL: no further than the message's end - the original read
    // on until it found a 0)
    util::ByteReader reader(context->body_, context->body_size_);
    uint8_t show = reader.u8();
    for (uint32_t goal = reader.u32(); reader.ok() && goal; goal = reader.u32()) {
        goals::Objective* entry = get_goal(goals, goal);
        entry->un_beat(); // (the original's vtable +0x74)
        entry->choose_one();
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
int handle_joined_team(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    Hub* manager = manager_of(context);
    // (the player's object id, the team; NOT ORIGINAL: one too short is dropped)
    util::ByteReader reader(context->body_, context->body_size_);
    uint8_t msg[2];
    if (!reader.bytes(msg, sizeof(msg)))
        return kDone;
    PlayerRecord* player = find_player_slot_by_obj(manager, msg);
    if (!player)
        return kContinue;
    player->team_ = static_cast<int8_t>(msg[1]);
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
    for (PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
         other = step_roster_sweep(manager, &search, false)) {
        if (other == player)
            continue;
        ++others;
        if (static_cast<int8_t>(msg[1]) != player_team(other))
            same_team = false;
    }
    if (same_team && others > 0 && get_game_mode()->current_num_teams() > 0)
        get_run(kWarnAllSameTeam);
    return kContinue;
}

// 0x00531C80
int handle_goal_completed(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    GoalMessage message;
    if (!read_goal_message(reader, &message)) // NOT ORIGINAL: too short - dropped
        return kDone;
    const uint8_t* msg = &message.id; // (the player's object id)
    Hub* manager = manager_of(context);
    auto* goals = objective_board();
    goals::Objective* goal = get_goal(goals, message.goal);
    goals::Objective* current = static_cast<goals::GoalManager*>(goals)->get_engaged_objective(0);
    if (!goal)
        return kContinue;
    PlayerRecord* player = find_player_slot_by_obj(manager, msg);
    PlayerRecord* local = find_own_player_record(manager);
    auto* objective = goal;
    objective->set_net_stage_flag(player_team(player)); // (the original's slot 0x70)
    if (!player_skater(player))
        return kContinue;
    objective->set_stage_flag(static_cast<int>(get_skater_id(player_skater(player)))); // (slot 0x6C)
    if (local == player) {
        int total = static_cast<goals::GoalManager*>(goals)->count_picked_objectives();
        int done = static_cast<goals::GoalManager*>(goals)->count_objectives_cleared();
        if (total > done) {
            script::Struct* params = new_params();
            params->put_int(kNumGoalsLeft, total - done);
            get_run(kCompletedGoal, params);
            delete_params(params);
        }
        static_cast<goals::ObjectiveNpc*>(goal->ped_)->remove_objective_npc();
        return kContinue;
    }
    if (current) {
        current->get_top_objective_key();
        goal->get_top_objective_key();
    }
    if (!is_squad_match() || player_team(local) != player_team(player))
        return kContinue;
    // A teammate's: the goal (or the last of its chain) ended here too.
    goals::GoalEdge* next = goal->sub_nodes_;
    if (next->objective_key_) {
        goals::Objective* last = goal;
        while (next && next->objective_key_) {
            last = get_goal(goals, next->objective_key_);
            next = last->sub_nodes_;
        }
        last->get_win(); // (slot 0x3C)
        if (last != current)
            current->disengage(false, true); // (slot 0x34)
    } else {
        objective->get_win();
    }
    static_cast<goals::ObjectiveNpc*>(current->ped_)->remove_objective_npc();
    objective->mark_won(); // (slot 0x68)
    int total = static_cast<goals::GoalManager*>(goals)->count_picked_objectives();
    int done = static_cast<goals::GoalManager*>(goals)->count_objectives_cleared();
    if (total > done) {
        const char* text = nullptr;
        goal->get_objective_list_caption(&text);
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
int handle_goal_started(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    GoalMessage message;
    if (!read_goal_message(reader, &message)) // NOT ORIGINAL: too short - dropped
        return kDone;
    const uint8_t* msg = &message.id; // (the player's object id)
    Hub* manager = manager_of(context);
    goals::Objective* goal = get_goal(objective_board(), message.goal);
    if (!goal)
        return kContinue;
    const char* text = nullptr;
    PlayerRecord* player = find_player_slot_by_obj(manager, msg);
    if (player_team(find_own_player_record(manager)) != player_team(player))
        return kContinue;
    goal->get_objective_list_caption(&text);
    script::Struct* params = new_params();
    params->append_word(kPlayerName, player_name(player));
    params->append_word(kGoalText, text);
    get_run(kStartedGoalSameTeam, params);
    delete_params(params);
    return kContinue;
}

// 0x00532280
int handle_eliminated(net::InboundMsgCtx* context)
{
    if (context->type_code_ != kMsgEliminated)
        return kContinue;
    int id = static_cast<int>(message_word(context, 0));
    if (id < 0) {
        get_run(kEliminatedNone);
        return kContinue;
    }
    PlayerRecord* player = find_player_slot_by_obj(manager_of(context), static_cast<uint16_t>(id));
    if (!player)
        return kContinue;
    auto* skate = get_skate();
    player->mark_eliminated();
    static_cast<modules::Skate*>(skate)->conceal_skater(static_cast<objects::Rider*>(player_skater(player)), true);
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
int handle_observers_full(net::InboundMsgCtx*)
{
    script::Struct* params = new_params();
    params->put_name_key(kReason, kFullObservers);
    params->put_name_key(kJustDialog, 0);
    get_run(kCreateJoinRefusedDialog, params);
    delete_params(params);
    return kContinue;
}

} // namespace online
