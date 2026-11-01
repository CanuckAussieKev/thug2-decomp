#pragma once

// Helpers shared by the network manager's message handlers (gamenet_handlers.cpp, gamenet_game_handlers.cpp,
// gamenet_tasks.cpp): the context's parts, the singletons and the player calls not rewritten yet, script parameters.

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/arena_alloc.h"
#include "game/goals/goal_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"
#include "game/rider/rider.h"
#include "engine/objects/entity.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

namespace objects {
class Entity;
} // namespace objects

namespace online::detail {

using online::NewPlayerSearch;
using online::PlayerSearch;

// Handler results.
constexpr int kContinue = net::kResultPassOn;
constexpr int kHalt = net::kResultStop;
constexpr int kDone = net::kResultConsumed;
constexpr int kKeep = net::kHandlerKeep;

constexpr uint8_t kGroupGame = 8; // the sequenced group of the game's messages

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *cell::pointer<T>(cell::address(object) + offset);
}

inline Hub* manager_of(net::InboundMsgCtx* context) { return static_cast<Hub*>(context->content_); }
inline const uint8_t* get_message(net::InboundMsgCtx* context)
{
    return raw::bytes(context->body_);
}
inline uint32_t message_word(net::InboundMsgCtx* context, uint32_t offset)
{
    return *raw::field_at<const uint32_t>(context->body_, offset);
}
inline void get_run(uint32_t name, script::Struct* params = nullptr, objects::Entity* object = nullptr)
{
    script::launch_script(name, params, object, false, nullptr);
}

// The singletons: the skate module, the game mode, the goal manager.
inline modules::Skate* get_skate() { return modules::g_skate_module; }
inline goals::MatchRules* get_game_mode() { return goals::g_game_mode; }
inline bool is_squad_match() { return get_game_mode()->is_squad_match(); }
inline uint32_t get_game_type() { return get_game_mode()->get_name_key(); }
inline goals::GoalManager* objective_board() { return goals::get_objective_board(); }
inline goals::Objective* get_goal(goals::GoalManager* goals, uint32_t name) { return goals->get_goal(name, 1); }

// The manager's players (online::PlayerRecord, player_info.h) and joining players (JoinerRecord).
inline PlayerRecord* find_player_by_sender(Hub* manager, net::Conn* conn)
{
    return manager->find_player_by_sender(conn);
}
// (the original pushes the id as 16 bits)
inline PlayerRecord* find_player_slot_by_obj(Hub* manager, uint16_t id) { return manager->find_player_slot_by_obj(id); }
inline PlayerRecord* find_player_slot_by_obj(Hub* manager, const uint8_t* id) // a signed byte, widened
{
    return find_player_slot_by_obj(manager, static_cast<uint16_t>(static_cast<int8_t>(*id)));
}
inline PlayerRecord* find_own_player_record(Hub* manager) { return manager->get_own_player(); }
inline PlayerRecord* is_crown_rule_active(Hub* manager) { return manager->get_hill_ruler(); }
// (0 or 1 in EAX: the original's callers test AL)
inline bool is_own_player(PlayerRecord* player) { return player->is_own_player() != 0; }
inline bool is_observer(PlayerRecord* player) { return player->is_observer() != 0; }
inline objects::Entity* player_skater(const PlayerRecord* player) { return player->skater_; }
inline uint32_t get_skater_id(const objects::Entity* skater) { return skater->ident_; }
inline const char* player_name(const PlayerRecord* player) { return player->label_.data(); }
inline int player_team(const PlayerRecord* player) { return player->team_; }

inline PlayerRecord* start_roster_sweep(Hub* manager, PlayerSearch* search, bool observers)
{
    return manager->begin_player_walk(search, observers);
}
inline PlayerRecord* step_roster_sweep(Hub* manager, PlayerSearch* search, bool observers)
{
    return manager->step_player_walk(search, observers);
}
inline JoinerRecord* first_new_player(Hub* manager, NewPlayerSearch* search)
{
    return manager->begin_joiner_walk(search);
}
inline JoinerRecord* next_new_player(Hub* manager, NewPlayerSearch* search)
{
    return manager->step_joiner_walk(search);
}

// A player's connection's handle, for AppHost::put_on_outbox (0x005434A0: 0 without a connection).
inline int player_handle(PlayerRecord* player) { return player->get_link_handle(); }

// A message for the server through `app` (a client).
inline void send_to_server(net::AppHost* app, net::NetMessageInfo* desc)
{
    core::down_cast<net::Guest>(app)->send_to_host(desc);
}

// Script parameters: a new structure (the original's new CStruct - null if out of memory, then used anyway as the
// original does), deleted with Clear.
inline script::Struct* new_params()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}
inline void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// "team_<n + 1>_name" (the original's sprintf).
inline void team_name_key(char* out, int team)
{
    std::array<char, 12> digits{};
    int length = 0;
    auto value = static_cast<unsigned int>(team + 1);
    const bool negative = team + 1 < 0;
    if (negative)
        value = 0u - value;
    do {
        digits[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value);
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result): the terminator follows with the next copy
    std::memcpy(out, "team_", 5);
    char* p = out + 5;
    if (negative)
        *p++ = '-';
    while (length)
        *p++ = digits[--length];
    std::memcpy(p, "_name", 6);
}

} // namespace online::detail
