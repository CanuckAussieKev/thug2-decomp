// The network game's script commands: capture the flag, the teams, surveyor mode, the host mode, kick / ban, cancelling
// a join (0x0053B450-0x0053D900). See gamenet_game_cfuncs.h.

#include "engine/net/gamenet_game_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_observer.h"
#include "engine/net/gamenet_qr2.h"
#include "engine/net/net_app.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "engine/sys/mlp.h"
#include "engine/core/code_address.h"
#include "engine/assets/resource_registry.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copy of the player's name)

namespace online {

using namespace detail;

namespace {

constexpr uint8_t kMsgTookFlag = 0x71;
constexpr uint8_t kMsgCapturedFlag = 0x72;
constexpr uint8_t kMsgKickRequest = 0x61; // (a client asks the server to kick or ban)

constexpr uint32_t kPlayer = 0x67E6859A;      // player
constexpr uint32_t kFlagTeam = 0xD61CFF21;    // flag_team
constexpr uint32_t kObjId = 0x0830ECAF;       // objId
constexpr uint32_t kSkaterScore = 0xD262802F; // the skater's score component

// PlayerRecord flags (+0x1D0).
constexpr uint32_t kPlayerCaptureBlocked = 0x8000;
constexpr uint32_t kPlayerMarked = 0x400; // chosen in the kick / ban menu
// (kPlayerSurveying, 0x10000: player_info.h)

// Why a player is dropped (0x0053CB70).
constexpr int kDropBanned = 3;
constexpr int kDropKicked = 4;

Hub* net_session_slot() { return g_online_hub; }
tasks::PileUp* get_logic_tasks()
{
    return reinterpret_cast<tasks::PileUp*>(reinterpret_cast<uint8_t*>(assets::g_main_loop_manager) + 4);
}

// The flag message (took / captured): the player's object id and the team, to every remote player through the server;
// `local` for this machine's.
template <typename Local>
void send_flag_message(Hub* manager, uint8_t id, int player, int team, Local local)
{
    uint8_t msg[2] = {static_cast<uint8_t>(player), static_cast<uint8_t>(team)};
    net::NetMessageInfo desc;
    desc.make_new();
    desc.byte_count_ = sizeof(msg);
    desc.content_ = msg;
    desc.ident_ = id;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    net::AppHost* server = manager->host_;
    PlayerSearch search;
    for (uint8_t* other = start_roster_sweep(manager, &search, true); other;
         other = step_roster_sweep(manager, &search, true)) {
        if (is_own_player(other))
            local();
        else
            server->put_on_outbox(player_handle(other), &desc);
    }
}

// Kick or ban: the first player marked for it (counting every player before it).
void kick_or_ban(Hub* manager, int drop, uint8_t request)
{
    PlayerSearch search;
    int index = 0;
    uint8_t* player = start_roster_sweep(manager, &search, true);
    for (; player && !(prop<uint32_t>(player, 0x1D0) & kPlayerMarked);
         player = step_roster_sweep(manager, &search, true))
        ++index;
    if (!player)
        return;
    if (manager->get_on_server()) {
        objects::Entity* skater = player_skater(player);
        bool observer = is_observer(player);
        manager->evict_player(player, drop);
        if (!observer)
            get_call(code_address(&modules::Skate::drop_skater_from_roster), get_skate(), skater); // its skater removed
        return;
    }
    struct {
        uint8_t request;
        uint8_t index;
        char name[kNameSize];
        uint8_t pad[2];
    } msg;
    net::NetMessageInfo desc;
    desc.make_new();
    msg.request = request;
    msg.index = static_cast<uint8_t>(index);
    std::strcpy(msg.name, player_name(player));
    desc.content_ = &msg;
    desc.byte_count_ = 2 + kNameSize;
    desc.ident_ = kMsgKickRequest;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    send_to_server(manager->client_app_, &desc);
}

// A task node / input handler's scalar deleting destructor (vtable slot 2).
void unlink_list_node(void* node)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(node))[2])(node, 1);
}

} // namespace

// 0x0053B450
bool __cdecl cfunc_player_took_flag(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    int player = 0;
    int team = 0;
    params->fetch_int_value(kPlayer, &player, 0);
    params->fetch_int_value(kFlagTeam, &team, 0);
    uint8_t* taker = manager->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (taker)
        send_flag_message(manager, kMsgTookFlag, player, team, [&] { as_player(taker)->note_flag_pickup(team); });
    return true;
}

// 0x0053B5A0
bool __cdecl cfunc_player_captured_flag(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    int player = 0;
    params->fetch_int_value(kPlayer, &player, 0);
    uint8_t* capturer = manager->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (!capturer || (prop<uint32_t>(capturer, 0x1D0) & kPlayerCaptureBlocked))
        return true;
    int team = as_player(capturer)->get_flag_team(); // the flag it carries
    send_flag_message(manager, kMsgCapturedFlag, player, team, [&] {
        auto* skater = reinterpret_cast<objects::GameEntity*>(player_skater(capturer));
        uint8_t* score = prop<uint8_t*>(skater->fetch_module(kSkaterScore), 0x18);
        get_call(code_address(&modules::PointTally::set_grand_total), score,
                 get_call<int>(code_address(&modules::PointTally::get_total_score), score) + 1); // its captures
        as_player(capturer)->score_flag_capture(team);
    });
    return true;
}

// 0x0053B710
bool __cdecl cfunc_player_retrieved_flag(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    int player = 0;
    int team = 0;
    params->fetch_int_value("player", &player, 0);
    params->fetch_int_value("flag_team", &team, 0);
    uint8_t* retriever = manager->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (retriever) // (as a capture: message 0x72 and 0x00543A90 - not 0x73 / 0x00544110, the retrieve's)
        send_flag_message(manager, kMsgCapturedFlag, player, team,
                          [&] { as_player(retriever)->score_flag_capture(team); });
    return true;
}

// 0x0053B860
bool __cdecl cfunc_player_has_flag(script::Struct* params, script::Script*)
{
    int player = 0;
    params->fetch_int_value("player", &player, 0);
    uint8_t* found = net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(player));
    if (!found)
        return true;
    return as_player(found)->is_carrying_flag() != 0; // (flags 0x7800: a team's flag)
}

// 0x0053B8A0
bool __cdecl cfunc_get_num_players_on_team(script::Struct* params, script::Script* script)
{
    Hub* manager = net_session_slot();
    int team = 0;
    params->fetch_int_value("team", &team, 1);
    script->args_->put_int("num_members", manager->count_team_players(team));
    return true;
}

// 0x0053B8F0
bool __cdecl cfunc_get_players_team(script::Struct* params, script::Script* script)
{
    int id = 0;
    params->fetch_int_value(kObjId, &id, 0);
    uint8_t* player = net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(id));
    if (player)
        script->args_->put_int("team", player_team(player));
    return true;
}

// 0x0053B940
bool __cdecl cfunc_exit_surveyor_mode(script::Struct*, script::Script*)
{
    Hub* manager = net_session_slot();
    uint8_t* local = find_own_player_record(manager);
    if (!local)
        return false;
    if (prop<uint32_t>(local, 0x1D0) & kPlayerSurveying) {
        manager->spectate_player(local);
        if (void* task = prop<void*>(local, 0x1EC))
            unlink_list_node(task);
        prop<void*>(local, 0x1EC) = nullptr;
        if (manager->spectator_pad_listener_)
            unlink_list_node(manager->spectator_pad_listener_);
        manager->spectator_pad_listener_ = nullptr;
        prop<uint32_t>(local, 0x1D0) &= ~kPlayerSurveying;
    }
    return true;
}

// 0x0053D400
bool __cdecl cfunc_set_host_mode(script::Struct* params, script::Script*)
{
    int mode = 0;
    params->fetch_int_value(0u, &mode, 0);
    net_session_slot()->set_host_mode(mode);
    return true;
}

bool g_text_entry;

// 0x0053D440
bool __cdecl cfunc_entered_network_game(script::Struct*, script::Script*)
{
    Hub* manager = net_session_slot();
    if (g_online_active) // (on the internet: the server's GameSpy reporting)
        start_query_reporting(false);
    tasks::PileUp* tasks = get_logic_tasks();
    tasks->queue_job(prop<tasks::SchedulerTask*>(manager, 0xD4));
    g_text_entry = true;
    tasks->queue_job(prop<tasks::SchedulerTask*>(manager, 0xC8));
    prop<bool>(manager, 0x46) = true;
    return true;
}

// 0x0053D490
bool __cdecl cfunc_kick_player(script::Struct*, script::Script*)
{
    kick_or_ban(net_session_slot(), kDropKicked, 0);
    return true;
}

// 0x0053D5E0
bool __cdecl cfunc_ban_player(script::Struct*, script::Script*)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap_);
    kick_or_ban(net_session_slot(), kDropBanned, 1);
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x0053D760
bool __cdecl cfunc_cancel_join_server(script::Struct*, script::Script*)
{
    Hub* manager = net_session_slot();
    manager->cancel_join();
    manager->join_phase_ = 10;
    return true;
}

// 0x0053D780
bool __cdecl cfunc_enter_surveyor_mode(script::Struct*, script::Script*)
{
    tasks::PileUp* tasks = get_logic_tasks();
    Hub* manager = net_session_slot();
    uint8_t* input = reinterpret_cast<uint8_t*>(pad_input::g_input_manager); // the input manager
    uint8_t* local = find_own_player_record(manager);
    if (is_observer(local) || (prop<uint32_t>(local, 0x1D0) & kPlayerSurveying))
        return false;
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->network_heap_);
    add_observer_controls(manager, local, tasks, input);
    prop<uint32_t>(local, 0x1D0) |= kPlayerSurveying;
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x0053EA20
bool __cdecl cfunc_drop_pending_players(script::Struct*, script::Script*)
{
    net_session_slot()->drop_players_not_ready();
    return true;
}

} // namespace online
