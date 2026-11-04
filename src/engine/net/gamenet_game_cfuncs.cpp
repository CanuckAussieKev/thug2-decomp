// The network game's script commands: capture the flag, the teams, surveyor mode, the host mode, kick / ban, cancelling
// a join (0x0053B450-0x0053D900). See gamenet_game_cfuncs.h.

#include "engine/net/gamenet_game_cfuncs.h"

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/enum_ops.h"
#include "engine/memory/manager.h"
#include "engine/memory/task_list.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_observer.h"
#include "engine/net/gamenet_qr2.h"
#include "engine/net/net_app.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "engine/sys/mlp.h"
#include "engine/assets/resource_registry.h"
#include "game/goals/objective.h"
#include "engine/script/fwd.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "game/rider/skater_score.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "platform/compiler.h"
#include "engine/core/down_cast.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copy of the player's name)

namespace online {

using namespace detail;

namespace {


constexpr uint32_t kPlayer = 0x67E6859A;      // player
constexpr uint32_t kFlagTeam = 0xD61CFF21;    // flag_team
constexpr uint32_t kObjId = 0x0830ECAF;       // objId
constexpr uint32_t kSkaterScore = 0xD262802F; // the skater's score component

// PlayerRecord flags (+0x1D0).
// (PlayerFlag::kSurveying, 0x10000: player_info.h)

// Why a player is dropped (0x0053CB70).
constexpr int kDropBanned = 3;
constexpr int kDropKicked = 4;

tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

// A flag and who has it: the flag's id, the player and the team.
struct FlagHolder {
    MessageId id{};
    int player{};
    int team{};
};

// The flag message (took / captured): the player's object id and the team, to every remote player through the server;
// `local` for this machine's.
template <typename Local>
void send_flag_message(Hub* manager, FlagHolder flag_holder, Local local)
{
    const auto id = flag_holder.id;
    const auto player = flag_holder.player;
    const auto team = flag_holder.team;
    std::array<uint8_t, 2> msg = {{static_cast<uint8_t>(player), static_cast<uint8_t>(team)}};
    net::NetMessageInfo desc;
    desc.set_byte_count(sizeof(msg));
    desc.set_content(msg.data());
    desc.set_ident(id);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    net::AppHost* server = manager->host();
    PlayerSearch search;
    for (PlayerRecord* other = start_roster_sweep(manager, &search, true); other;
         other = step_roster_sweep(manager, &search, true)) {
        if (is_own_player(other)) {
            local();
        } else {
            server->put_on_outbox(player_handle(other), &desc);
        }
    }
}

// Whether the player is dropped (kicked) and the request byte sent.
struct KickChoice {
    int drop{};
    uint8_t request{};
};

// Kick or ban: the first player marked for it (counting every player before it).
void kick_or_ban(Hub* manager, KickChoice kick_choice)
{
    const auto drop = kick_choice.drop;
    const auto request = kick_choice.request;
    PlayerSearch search;
    int index = 0;
    PlayerRecord* player = start_roster_sweep(manager, &search, true);
    for (; player && !core::any(player->flag_bits() & PlayerFlag::kMarked);
         player = step_roster_sweep(manager, &search, true)) {
        ++index;
    }
    if (!player) {
        return;
    }
    if (manager->get_on_server()) {
        objects::Entity* skater = player_skater(player);
        const bool observer = is_observer(player);
        manager->evict_player(player, drop);
        if (!observer) {
            modules::Skate::drop_skater_from_roster(core::down_cast<objects::Rider>(skater)); // its skater removed
        }
        return;
    }
    KickRequest msg; // (a client asks the server to kick or ban: gamenet_messages.h)
    net::NetMessageInfo desc;
    msg.kick = request;
    msg.index = static_cast<int8_t>(index);
    std::strncpy(msg.name.data(), player_name(player), kNameSize - 1);
    std::array<uint8_t, kKickRequestSize> bytes{};
    util::ByteWriter writer(bytes.data(), sizeof(bytes));
    write_kick_request(writer, msg);
    desc.set_content(bytes.data());
    desc.set_byte_count(sizeof(bytes));
    desc.set_ident(MessageId::kKickRequest);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(MessageGroup::kGame);
    send_to_server(manager->client_app(), &desc);
}

} // namespace

// 0x0053B450
bool cfunc_player_took_flag(script::Struct* params, script::Script* /*script*/)
{
    Hub* manager = net_game_state().online_hub();
    int player = 0;
    int team = 0;
    params->fetch_int_value(kPlayer, &player, 0);
    params->fetch_int_value(kFlagTeam, &team, 0);
    PlayerRecord* taker = manager->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (taker) {
        send_flag_message(manager, {.id = MessageId::kTookFlag, .player = player, .team = team}, [&] { taker->note_flag_pickup(team); });
    }
    return true;
}

// 0x0053B5A0
bool cfunc_player_captured_flag(script::Struct* params, script::Script* /*script*/)
{
    Hub* manager = net_game_state().online_hub();
    int player = 0;
    params->fetch_int_value(kPlayer, &player, 0);
    PlayerRecord* capturer = manager->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (!capturer || core::any(capturer->flag_bits() & PlayerFlag::kCaptureBlocked)) {
        return true;
    }
    int team = capturer->get_flag_team(); // the flag it carries
    send_flag_message(manager, {.id = MessageId::kCapturedFlag, .player = player, .team = team}, [&] {
        auto* skater = player_skater(capturer);
        modules::PointTally* score =
            core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore))->points_held();
        score->set_grand_total(score->get_total_score() + 1); // its captures
        capturer->score_flag_capture(team);
    });
    return true;
}

// 0x0053B710
bool cfunc_player_retrieved_flag(script::Struct* params, script::Script* /*script*/)
{
    Hub* manager = net_game_state().online_hub();
    int player = 0;
    int team = 0;
    params->fetch_int_value("player", &player, 0);
    params->fetch_int_value("flag_team", &team, 0);
    PlayerRecord* retriever = manager->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (retriever) { // (as a capture: message 0x72 and 0x00543A90 - not 0x73 / 0x00544110, the retrieve's)
        send_flag_message(manager, {.id = MessageId::kCapturedFlag, .player = player, .team = team}, [&] { retriever->score_flag_capture(team); });
    }
    return true;
}

// 0x0053B860
bool cfunc_player_has_flag(script::Struct* params, script::Script* /*script*/)
{
    int player = 0;
    params->fetch_int_value("player", &player, 0);
    PlayerRecord* found = net_game_state().online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (!found) {
        return true;
    }
    return found->is_carrying_flag() != 0; // (flags 0x7800: a team's flag)
}

// 0x0053B8A0
bool cfunc_get_num_players_on_team(script::Struct* params, script::Script* script)
{
    Hub* manager = net_game_state().online_hub();
    int team = 0;
    params->fetch_int_value("team", &team, 1);
    script->args()->put_int("num_members", manager->count_team_players(team));
    return true;
}

// 0x0053B8F0
bool cfunc_get_players_team(script::Struct* params, script::Script* script)
{
    int id = 0;
    params->fetch_int_value(kObjId, &id, 0);
    const PlayerRecord* player = net_game_state().online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(id));
    if (player) {
        script->args()->put_int("team", player_team(player));
    }
    return true;
}

// 0x0053B940
bool cfunc_exit_surveyor_mode(script::Struct* /*params*/, script::Script* /*script*/)
{
    Hub* manager = net_game_state().online_hub();
    PlayerRecord* local = find_own_player_record(manager);
    if (!local) {
        return false;
    }
    if (core::any(local->flag_bits() & PlayerFlag::kSurveying)) {
        manager->spectate_player(local);
        local->delete_observer_task(); // the observer task (the player's)
        manager->delete_spectator_pad_listener();
        local->flag_bits() &= ~PlayerFlag::kSurveying;
    }
    return true;
}

// 0x0053D400
bool cfunc_set_host_mode(script::Struct* params, script::Script* /*script*/)
{
    int mode = 0;
    params->fetch_int_value(0U, &mode, 0);
    net_game_state().online_hub()->set_host_mode(mode);
    return true;
}

// 0x0053D440
bool cfunc_entered_network_game(script::Struct* /*params*/, script::Script* /*script*/)
{
    Hub* manager = net_game_state().online_hub();
    if (net_game_state().online_active()) { // (on the internet: the server's GameSpy reporting)
        start_query_reporting(false);
    }
    tasks::PileUp* tasks = get_logic_tasks();
    tasks->queue_job(manager->keyboard_job().get());
    net_game_state().set_text_entry(true);
    tasks->queue_job(manager->score_broadcast_job().get());
    manager->set_show_scores(true);
    return true;
}

// 0x0053D490
bool cfunc_kick_player(script::Struct* /*params*/, script::Script* /*script*/)
{
    kick_or_ban(net_game_state().online_hub(), {.drop = kDropKicked, .request = 0});
    return true;
}

// 0x0053D5E0
bool cfunc_ban_player(script::Struct* /*params*/, script::Script* /*script*/)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap());
    kick_or_ban(net_game_state().online_hub(), {.drop = kDropBanned, .request = 1});
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x0053D760
bool cfunc_cancel_join_server(script::Struct* /*params*/, script::Script* /*script*/)
{
    Hub* manager = net_game_state().online_hub();
    manager->cancel_join();
    manager->set_join_phase(EntryPhase::kCancelled);
    return true;
}

// 0x0053D780
bool cfunc_enter_surveyor_mode(script::Struct* /*params*/, script::Script* /*script*/)
{
    tasks::PileUp* tasks = get_logic_tasks();
    Hub* manager = net_game_state().online_hub();
    pad_input::Hub* input = pad_input::get_input_manager();
    PlayerRecord* local = find_own_player_record(manager);
    if (is_observer(local) || core::any(local->flag_bits() & PlayerFlag::kSurveying)) {
        return false;
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap());
    add_observer_controls(manager, local, tasks, input);
    local->flag_bits() |= PlayerFlag::kSurveying;
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x0053EA20
bool cfunc_drop_pending_players(script::Struct* /*params*/, script::Script* /*script*/)
{
    net_game_state().online_hub()->drop_players_not_ready();
    return true;
}

} // namespace online
