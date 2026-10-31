#pragma once

// Helpers shared by the network manager's message handlers (gamenet_handlers.cpp, gamenet_game_handlers.cpp,
// gamenet_tasks.cpp): the context's parts, the singletons and the player calls not rewritten yet, script parameters.

#include <cstdint>
#include <cstring>

#include "engine/core/arena_alloc.h"
#include "game/goals/goal_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"

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
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
// A thiscall into code not rewritten yet.
template <typename R = void, typename... A>
R get_call(uint32_t address, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}

// The same, into code of ours reached through its address (GAME_CODE).
template <typename R = void, typename... A>
R get_call(void* code, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(code)(object, args...);
}

inline Hub* manager_of(net::InboundMsgCtx* context) { return static_cast<Hub*>(context->content_); }
inline const uint8_t* get_message(net::InboundMsgCtx* context)
{
    return reinterpret_cast<const uint8_t*>(context->body_);
}
inline uint32_t message_word(net::InboundMsgCtx* context, uint32_t offset)
{
    return *reinterpret_cast<const uint32_t*>(context->body_ + offset);
}
inline void get_run(uint32_t name, script::Struct* params = nullptr, objects::Entity* object = nullptr)
{
    script::launch_script(name, params, object, false, nullptr);
}

// The singletons (not rewritten): the skate module, the game mode, the goal manager.
inline uint8_t* get_skate() { return reinterpret_cast<uint8_t*>(modules::g_skate_module); }
inline uint8_t* get_game_mode() { return reinterpret_cast<uint8_t*>(goals::g_game_mode); }
inline bool is_squad_match()
{
    return get_call<bool>(code_address(&goals::MatchRules::is_squad_match), get_game_mode());
}
inline uint32_t get_game_type()
{
    return get_call<uint32_t>(code_address(&goals::MatchRules::get_name_key), get_game_mode());
}
inline uint8_t* objective_board() { return reinterpret_cast<uint8_t*>(goals::get_objective_board()); }
inline uint8_t* get_goal(uint8_t* goals, uint32_t name)
{
    return get_call<uint8_t*>(code_address(&goals::GoalManager::get_goal), goals, name, 1);
}

// The manager's players (online::PlayerRecord, player_info.h; reached as bytes here): +0x14 the skater, +0x18 the
// connection, +0x30 the team, +0x38 the name, +0x1D0 flags.
inline PlayerRecord* as_player(const uint8_t* player)
{
    return reinterpret_cast<PlayerRecord*>(const_cast<uint8_t*>(player));
}
inline uint8_t* find_player_by_sender(Hub* manager, net::Conn* conn) { return manager->find_player_by_sender(conn); }
// (the original pushes the id as 16 bits)
inline uint8_t* find_player_slot_by_obj(Hub* manager, uint16_t id) { return manager->find_player_slot_by_obj(id); }
inline uint8_t* find_player_slot_by_obj(Hub* manager, const uint8_t* id) // a signed byte, widened
{
    return find_player_slot_by_obj(manager, static_cast<uint16_t>(static_cast<int8_t>(*id)));
}
inline uint8_t* find_own_player_record(Hub* manager) { return static_cast<uint8_t*>(manager->get_own_player()); }
inline uint8_t* is_crown_rule_active(Hub* manager) { return static_cast<uint8_t*>(manager->get_hill_ruler()); }
// (0 or 1 in EAX: the original's callers test AL)
inline bool is_own_player(const uint8_t* player) { return as_player(player)->is_own_player() != 0; }
inline bool is_observer(const uint8_t* player) { return as_player(player)->is_observer() != 0; }
inline objects::Entity* player_skater(const uint8_t* player) { return prop<objects::Entity*>(player, 0x14); }
inline uint32_t get_skater_id(const void* skater) { return prop<uint32_t>(skater, 0x14); }
inline const char* player_name(uint8_t* player) { return reinterpret_cast<const char*>(player + 0x38); }
inline int player_team(const uint8_t* player) { return prop<int>(player, 0x30); }

inline uint8_t* start_roster_sweep(Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<uint8_t*>(manager->begin_player_walk(search, observers));
}
inline uint8_t* step_roster_sweep(Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<uint8_t*>(manager->step_player_walk(search, observers));
}
inline uint8_t* first_new_player(Hub* manager, NewPlayerSearch* search)
{
    return static_cast<uint8_t*>(manager->begin_joiner_walk(search));
}
inline uint8_t* next_new_player(Hub* manager, NewPlayerSearch* search)
{
    return static_cast<uint8_t*>(manager->step_joiner_walk(search));
}

// A player's connection's handle, for AppHost::put_on_outbox (0x005434A0: 0 without a connection).
inline int player_handle(uint8_t* player) { return as_player(player)->get_link_handle(); }

// A message for the server through `app` (a client).
inline void send_to_server(net::AppHost* app, net::NetMessageInfo* desc)
{
    static_cast<net::Guest*>(app)->send_to_host(desc);
}

// Script parameters: a new structure (the original's new CStruct - null if out of memory, then used anyway as the
// original does), deleted with Clear.
inline script::Struct* new_params()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
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
    char digits[12];
    int length = 0;
    unsigned int value = static_cast<unsigned int>(team + 1);
    bool negative = team + 1 < 0;
    if (negative)
        value = 0u - value;
    do {
        digits[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value);
    std::memcpy(out, "team_", 5);
    char* p = out + 5;
    if (negative)
        *p++ = '-';
    while (length)
        *p++ = digits[--length];
    std::memcpy(p, "_name", 6);
}

} // namespace online::detail
