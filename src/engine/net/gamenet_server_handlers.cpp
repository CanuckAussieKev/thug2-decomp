// The network manager's server-side message handlers (online::Hub, registered by 0x0053C380): observers, the
// game's data for a joining player, the end of a game, team changes, goal attack, leaving, cheating. See
// gamenet_handlers.h.

#include <cstdint>
#include <array>

#include "engine/core/manager.h"
#include "game/park_editing/park_editor.h"
#include "engine/maths/random.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;               // netlobby
constexpr uint32_t kNetCtf = 0x6C5FF266;                 // netctf
constexpr uint32_t kNetKing = 0x6EF8FDA0;                // netking
constexpr uint32_t kKing = 0x5D32129C;                   // king
constexpr uint32_t kTeam = 0x3B1F59E0;                   // Team
constexpr uint32_t kString0 = 0xA4B08520;                // String0
constexpr uint32_t kFlagReturned = 0x3B1F2478;           // flag_returned
constexpr uint32_t kDestroyCtfPanelMessage = 0x514A53A6; // destroy_ctf_panel_message
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;           // hide_ctf_arrow
constexpr uint32_t kNotifyClientCheating = 0x62DDBE0A;   // notify_client_cheating

// Messages.
constexpr uint8_t kMsgAckQuery = 0x25; // (the ready query)
constexpr uint8_t kMsgObserve = 0x4D;
constexpr uint8_t kMsgObserversFull = 0x4E;
constexpr uint8_t kMsgCrownSpawn = 0x58;
constexpr uint8_t kMsgGameOver = 0x5A;
constexpr uint8_t kMsgJoinedTeam = 0x68;
constexpr uint8_t kMsgParkMap = 0x6C;
constexpr uint8_t kMsgFlagReturned = 0x75;
constexpr uint8_t kMsgGoalsData = 0x87;
constexpr uint8_t kMsgRailsData = 0x88;

constexpr int kParkMapSize = 15000;
constexpr uint16_t kRailDataSize = 0x1132;

constexpr uint32_t kFlagReady = 0x100; // a player's: its game has ended (message 0x59)
constexpr uint32_t kFlag8000 = 0x8000;

// A goal's stage flags by the skater's id (the original's vtable +0x50 / +0x6C): has `id` done it, done by `id`.
bool goal_done_by(goals::Objective* goal, uint32_t id) { return goal->stage_flag_set(static_cast<int>(id)) != 0; }
void mark_goal_done_by(goals::Objective* goal, uint32_t id) { goal->set_stage_flag(static_cast<int>(id)); }

// The goal attack message to other players: the skater's id, then the goal's name (8 bytes; bytes 1-3 left as the
// original leaves them - zeroed here).

} // namespace

// 0x0052E060
int handle_observe_request(net::InboundMsgCtx* context)
{
    auto* skate = get_skate();
    Hub* manager = manager_of(context);
    PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_);
    if (!player || is_observer(player))
        return kDone;
    net::NetMessageInfo desc;
    if (!is_own_player(player) &&
        manager->count_observers() >= manager->get_spectator_limit()) { // the observers so far
        desc = net::NetMessageInfo();
        desc.ident_ = kMsgObserversFull;
        manager->host()->put_on_outbox(context->sender_conn_->hdl_, &desc);
        return kContinue;
    }
    desc = net::NetMessageInfo();
    if (!is_own_player(player)) {
        objects::Entity* skater = player_skater(player);
        manager->evict_player(player, 2); // the player out of the game (2: to observe)
        static_cast<modules::Skate*>(skate)->drop_skater_from_roster(
            core::down_cast<objects::Rider>(skater)); // its skater gone
        player->flag_bits_ |= 4;                   // an observer now
        player->skater_ = nullptr;
        if (player->is_host_player()) {
            player->clear_server_player();
            manager->assign_first_come_first_served();
        }
        if (player == manager->viewed_player()) { // the camera onto the next player (none if that's this one again)
            manager->set_viewed_player(manager->next_cam_player());
            if (player == manager->viewed_player())
                manager->set_viewed_player(nullptr);
            manager->spectate_player(manager->viewed_player());
        }
    }
    desc.ident_ = kMsgObserve;
    manager->host()->put_on_outbox(context->sender_conn_->hdl_, &desc);
    return kContinue;
}

// 0x0052E9E0
int handle_request_game_data(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_);
    // (the message: gamenet_messages.h - the level, 'F' for the level change too; NOT ORIGINAL: one too short is
    // dropped)
    util::ByteReader reader(context->body_, context->body_size_);
    LevelSwitch request;
    if (!read_level_switch(reader, &request))
        return kDone;
    net::AppHost* app = context->receiver_app_;
    int handle = context->sender_conn_->hdl_;
    void* park_map = park_editing::ParkBuilderMode::sole()->get_packed_park_data(false)->bytes().data();
    app->send_bulk_message(handle, kMsgParkMap, kParkMapSize, park_map, "level data", kGroupGame, false, true);
    handle = context->sender_conn_->hdl_;
    auto rail_editor = [] { return park_editing::RailAuthoring::sole(); };
    rail_editor(); // (looked up once for nothing)
    void* rails = static_cast<park_editing::RailAuthoring*>(rail_editor())->get_packed();
    app->send_bulk_message(handle, kMsgRailsData, kRailDataSize, rails, "rail data", kGroupGame, false, true);
    if (manager->has_created_goals()) {
        manager->prepare_created_goals(request.level);
        handle = context->sender_conn_->hdl_;
        void* goals = manager->get_created_goals_message();
        auto size = static_cast<uint16_t>(manager->get_created_goals_size());
        app->send_bulk_message(handle, kMsgGoalsData, size, goals, "goals data", kGroupGame, false, true);
    }
    if (request.flag != 'F')
        return kContinue;
    // The level change ('F', the level; 8 bytes - bytes 5-7 left as the original leaves them, zeroed here), then a
    // ready query (the time) the player owes the answer to.
    net::NetMessageInfo desc;
    LevelSwitch change;
    change.level = request.level;
    std::array<uint8_t, kLevelSwitchSize> level{};
    util::ByteWriter writer(level.data(), sizeof(level));
    write_level_switch(writer, change);
    uint32_t query = frame_clock::get_time();
    desc.content_ = level.data();
    desc.byte_count_ = sizeof(level);
    desc.ident_ = kMsgLevelSwitch;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    app->put_on_outbox(player_handle(player), &desc);
    desc.content_ = &query;
    desc.byte_count_ = 4;
    desc.ident_ = kMsgAckQuery;
    app->put_on_outbox(player_handle(player), &desc);
    player->await_ready_response(query); // (the query it owes)
    app->transmit(false);
    return kContinue;
}

// 0x0052EC40
int handle_game_ended_report(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_);
    util::ByteReader reader(context->body_, context->body_size_);
    const uint8_t game = reader.u8();
    if (!reader.ok()) // NOT ORIGINAL: empty - dropped
        return kDone;
    if (!player)
        return kContinue;
    PlayerSearch search;
    if ((player->flag_bits_ & kFlagReady) || game != manager->get_game_id())
        return kDone;
    player->flag_bits_ |= kFlagReady;
    for (const PlayerRecord* other = start_roster_sweep(manager, &search, false); other;
         other = step_roster_sweep(manager, &search, false)) {
        if (!(other->flag_bits_ & kFlagReady))
            return kContinue; // someone is still playing
    }
    // Everyone's game has ended: game over, to all (observers too).
    net::NetMessageInfo desc;
    net::AppHost* server = manager->host();
    desc.ident_ = kMsgGameOver;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    for (PlayerRecord* other = start_roster_sweep(manager, &search, true); other;
         other = step_roster_sweep(manager, &search, true))
        server->put_on_outbox(player_handle(other), &desc);
    return kContinue;
}

// 0x0052EE10
int on_switch_squad_msg(net::InboundMsgCtx* context)
{
    net::NetMessageInfo desc;
    PlayerSearch search;
    Hub* manager = manager_of(context);
    // (the player's object id, the team; NOT ORIGINAL: one too short is dropped)
    util::ByteReader reader(context->body_, context->body_size_);
    std::array<uint8_t, 2> msg{};
    if (!reader.bytes(msg.data(), sizeof(msg)))
        return kDone;
    const PlayerRecord* player = find_player_slot_by_obj(manager, msg.data());
    if (!player || !player_skater(player))
        return kDone;
    if (get_game_type() != kNetLobby && !manager->has_match_ended())
        return kDone;
    std::array<uint8_t, 2> change = {{core::at(msg, 0), core::at(msg, 1)}};
    net::AppHost* server = manager->host();
    desc.content_ = change.data();
    desc.byte_count_ = 2;
    desc.ident_ = kMsgJoinedTeam;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    for (PlayerRecord* other = start_roster_sweep(manager, &search, true); other;
         other = step_roster_sweep(manager, &search, true))
        server->put_on_outbox(player_handle(other), &desc);
    return kContinue;
}

// 0x0052EF40
int handle_goal_completed_report(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    GoalMessage report;                      // (the game id, the goal)
    if (!read_goal_message(reader, &report)) // NOT ORIGINAL: too short - dropped
        return kDone;
    Hub* manager = manager_of(context);
    if (report.id != manager->get_game_id())
        return kDone;
    goals::Objective* goal = get_goal(objective_board(), report.goal);
    if (!goal)
        return kContinue;
    PlayerRecord* sender = find_player_by_sender(manager, context->sender_conn_);
    if (!player_skater(sender))
        return kContinue;
    PlayerSearch search;
    std::array<uint8_t, kGoalMessageSize> bytes{};
    net::NetMessageInfo desc;
    auto prepare = [&] {
        GoalMessage message; // (the sender's object id, the goal)
        message.goal = report.goal;
        message.id = static_cast<uint8_t>(get_skater_id(player_skater(sender)));
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        write_goal_message(writer, message);
        desc.content_ = bytes.data();
        desc.byte_count_ = sizeof(bytes);
        desc.ident_ = kMsgGoalCompleted;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
    };
    if (is_squad_match()) {
        // Already done by the sender's team: nothing.
        for (const PlayerRecord* p = start_roster_sweep(manager, &search, false); p;
             p = step_roster_sweep(manager, &search, false)) {
            if (player_team(sender) == player_team(p) && goal_done_by(goal, get_skater_id(player_skater(p))))
                return kContinue;
        }
        mark_goal_done_by(goal, get_skater_id(player_skater(sender)));
        for (PlayerRecord* p = start_roster_sweep(manager, &search, false); p;
             p = step_roster_sweep(manager, &search, false)) {
            desc = net::NetMessageInfo();
            prepare();
            context->receiver_app_->put_on_outbox(player_handle(p), &desc);
        }
        return kContinue;
    }
    if (goal_done_by(goal, get_skater_id(player_skater(sender))))
        return kContinue;
    desc = net::NetMessageInfo();
    mark_goal_done_by(goal, get_skater_id(player_skater(sender)));
    prepare();
    for (PlayerRecord* p = start_roster_sweep(manager, &search, false); p;
         p = step_roster_sweep(manager, &search, false))
        context->receiver_app_->put_on_outbox(player_handle(p), &desc);
    return kContinue;
}

// 0x0052F1A0
int handle_goal_started_report(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    util::ByteReader reader(context->body_, context->body_size_);
    GoalMessage report;                      // (the game id, the goal)
    if (!read_goal_message(reader, &report)) // NOT ORIGINAL: too short - dropped
        return kDone;
    if (report.id != manager->get_game_id())
        return kDone;
    if (!get_goal(objective_board(), report.goal) || !is_squad_match())
        return kContinue;
    const PlayerRecord* sender = find_player_by_sender(manager, context->sender_conn_);
    if (!player_skater(sender))
        return kContinue;
    PlayerSearch search;
    net::NetMessageInfo desc;
    GoalMessage message; // (the sender's object id, the goal)
    message.goal = report.goal;
    message.id = static_cast<uint8_t>(get_skater_id(player_skater(sender)));
    std::array<uint8_t, kGoalMessageSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    write_goal_message(writer, message);
    desc.content_ = bytes.data();
    desc.byte_count_ = sizeof(bytes);
    desc.ident_ = kMsgGoalStarted;
    for (PlayerRecord* p = start_roster_sweep(manager, &search, false); p;
         p = step_roster_sweep(manager, &search, false)) {
        if (p != sender && player_team(sender) == player_team(p))
            context->receiver_app_->put_on_outbox(player_handle(p), &desc);
    }
    return kContinue;
}

// 0x0052FCC0
int on_peer_left_msg(net::InboundMsgCtx* context)
{
    auto* skate = get_skate();
    Hub* manager = manager_of(context);
    if (PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_)) {
        if (!is_own_player(player)) {
            objects::Entity* skater = player_skater(player);
            const bool observer = is_observer(player);
            manager->evict_player(player, 0); // the player out of the game
            if (!observer)
                static_cast<modules::Skate*>(skate)->drop_skater_from_roster(core::down_cast<objects::Rider>(skater));
        }
        return kKeep;
    }
    // Not a player yet: one of the new players waiting to be added - dropped with its connection.
    auto* node = manager->joiners().next; // (the new players' list, its first node)
    if (node->data == memory::ListNode::head_marker())
        return kHalt;
    for (; node;) {
        memory::ListNode* next = node->next->data == memory::ListNode::head_marker() ? nullptr : node->next;
        auto* info = static_cast<JoinerRecord*>(node->data);
        if (info->conn_ == context->sender_conn_) {
            context->receiver_app_->sever_link(info->conn_);
            memory::ListNode::destroy(node); // the node deleted
            info->~JoinerRecord();
            memory::Hub::get_pull()->dealloc(info);
            return kKeep;
        }
        node = next;
    }
    return kHalt;
}

// 0x0052FDA0
int handle_drop_carried(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_);
    if (player)
        player->flag_bits_ &= ~kFlag8000;
    PlayerSearch search;
    net::NetMessageInfo desc;
    if (get_game_type() == kNetCtf) {
        // Its flag returned.
        if (!player || !player->is_carrying_flag())
            return kContinue;
        desc = net::NetMessageInfo();
        const int team = player->get_flag_team();
        std::array<char, 64> name{};
        team_name_key(name, team);
        script::StructPtr params = script::make_struct();
        params->put_int(kTeam, team);
        params->append_word(kString0, script::kQbvalString, script::fetch_text(name.data()));
        get_run(kFlagReturned, params.get());
        params.reset();
        std::array<uint8_t, 2> returned = {{static_cast<uint8_t>(get_skater_id(player_skater(player))), static_cast<uint8_t>(team)}};
        if (is_own_player(player))
            get_run(kDestroyCtfPanelMessage);
        PlayerRecord* local = find_own_player_record(manager);
        if (local && !is_observer(local) && player_team(local) == team)
            get_run(kHideCtfArrow);
        player->clear_flags(); // carries no flag
        desc.content_ = returned.data();
        desc.byte_count_ = 2;
        desc.ident_ = kMsgFlagReturned;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = kGroupGame;
        for (PlayerRecord* p = start_roster_sweep(manager, &search, true); p;
             p = step_roster_sweep(manager, &search, true)) {
            if (!is_own_player(p))
                context->receiver_app_->put_on_outbox(player_handle(p), &desc);
        }
        return kContinue;
    }
    if (get_game_type() != kNetKing && get_game_type() != kKing)
        return kContinue;
    // The crown dropped: a new spot for it.
    if (!player || !player->holds_crown()) // (has the crown)
        return kContinue;
    desc = net::NetMessageInfo();
    player->give_crown(false);
    const int spot = maths::rnd(10);
    manager->set_crown_start(spot);
    auto message = static_cast<uint8_t>(spot);
    desc.content_ = &message;
    desc.byte_count_ = 1;
    desc.ident_ = kMsgCrownSpawn;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    for (PlayerRecord* p = start_roster_sweep(manager, &search, true); p; p = step_roster_sweep(manager, &search, true))
        context->receiver_app_->put_on_outbox(player_handle(p), &desc);
    return kContinue;
}

// 0x00532170
int handle_checksum_response(net::InboundMsgCtx* context)
{
    Hub* manager = manager_of(context);
    const PlayerRecord* player = find_player_by_sender(manager, context->sender_conn_);
    if ((message_word(context, 0) ^ 0xDEADFACE) == message_word(context, 4) || manager->cheating_noted())
        return kContinue;
    script::StructPtr params = script::make_struct();
    params->append_word(kString0, player_name(player));
    get_run(kNotifyClientCheating, params.get());
    params.reset();
    manager->note_cheating();
    return kContinue;
}

} // namespace online
