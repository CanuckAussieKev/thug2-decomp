#pragma once

// Helpers shared by the network manager's message handlers (gamenet_handlers.cpp, gamenet_game_handlers.cpp,
// gamenet_tasks.cpp): the context's parts, the singletons and the player calls not rewritten yet, script parameters.

#include <cstdint>
#include <cstring>

#include "game/core/arena_alloc.h"
#include "game/goals/goal_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"

namespace GameObjects {
class CEntity;
} // namespace Obj

namespace OnlinePlay {
extern void** const kNewPlayerSearchVtable; // 0x0064C82C: ListKit::Hunt<JoinerRecord> (gamenet_handlers.cpp)
} // namespace OnlinePlay

namespace OnlinePlay::Detail {

// Handler results.
constexpr int kContinue = NetLink::kHandlerContinue;
constexpr int kHalt = NetLink::kHandlerHalt;
constexpr int kDone = NetLink::kHandlerMsgDone;
constexpr int kKeep = NetLink::kHandlerKeep;

constexpr uint8_t kGroupGame = 8; // the sequenced group of the game's messages

template <typename T>
T& Prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
// A thiscall into code not rewritten yet.
template <typename R = void, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}

// The same, into code of ours reached through its address (GAME_CODE).
template <typename R = void, typename... A>
R Call(void* code, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(code)(object, args...);
}

inline Hub* ManagerOf(NetLink::InboundMsgCtx* context) { return static_cast<Hub*>(context->m_content); }
inline const uint8_t* Message(NetLink::InboundMsgCtx* context)
{
    return reinterpret_cast<const uint8_t*>(context->m_body);
}
inline uint32_t MessageWord(NetLink::InboundMsgCtx* context, uint32_t offset)
{
    return *reinterpret_cast<const uint32_t*>(context->m_body + offset);
}
inline void Run(uint32_t name, Scripts::CStruct* params = nullptr, GameObjects::CEntity* object = nullptr)
{
    Scripts::LaunchScript(name, params, object, false, nullptr);
}

// The singletons (not rewritten): the skate module, the game mode, the goal manager.
inline uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
inline uint8_t* GameMode() { return reinterpret_cast<uint8_t*>(GoalSystem::g_gameMode); }
inline bool IsSquadMatch() { return Call<bool>(CodeAddress(&GoalSystem::CMatchRules::IsSquadMatch), GameMode()); }
inline uint32_t GameType() { return Call<uint32_t>(CodeAddress(&GoalSystem::CMatchRules::GetNameKey), GameMode()); }
inline uint8_t* ObjectiveBoard() { return reinterpret_cast<uint8_t*>(GoalSystem::GetObjectiveBoard()); }
inline uint8_t* GetGoal(uint8_t* goals, uint32_t name) { return Call<uint8_t*>(CodeAddress(&GoalSystem::CGoalManager::GetGoal), goals, name, 1); }

// The manager's players (OnlinePlay::PlayerRecord, player_info.h; reached as bytes here): +0x14 the skater, +0x18 the
// connection, +0x30 the team, +0x38 the name, +0x1D0 flags.
inline PlayerRecord* AsPlayer(const uint8_t* player) { return reinterpret_cast<PlayerRecord*>(const_cast<uint8_t*>(player)); }
inline uint8_t* PlayerByConnection(Hub* manager, NetLink::Conn* conn)
{
    return manager->PlayerByConnection(conn);
}
// (the original pushes the id as 16 bits)
inline uint8_t* PlayerByObjectId(Hub* manager, uint16_t id) { return manager->PlayerByObjectId(id); }
inline uint8_t* PlayerByObjectId(Hub* manager, const uint8_t* id) // a signed byte, widened
{
    return PlayerByObjectId(manager, static_cast<uint16_t>(static_cast<int8_t>(*id)));
}
inline uint8_t* LocalPlayer(Hub* manager) { return static_cast<uint8_t*>(manager->GetOwnPlayer()); }
inline uint8_t* KingOfTheHill(Hub* manager) { return static_cast<uint8_t*>(manager->GetHillRuler()); }
// (0 or 1 in EAX: the original's callers test AL)
inline bool IsOwnPlayer(const uint8_t* player) { return AsPlayer(player)->IsOwnPlayer() != 0; }
inline bool IsObserver(const uint8_t* player) { return AsPlayer(player)->IsObserver() != 0; }
inline GameObjects::CEntity* PlayerSkater(const uint8_t* player) { return Prop<GameObjects::CEntity*>(player, 0x14); }
inline uint32_t SkaterId(const void* skater) { return Prop<uint32_t>(skater, 0x14); }
inline const char* PlayerName(uint8_t* player) { return reinterpret_cast<const char*>(player + 0x38); }
inline int PlayerTeam(const uint8_t* player) { return Prop<int>(player, 0x30); }

// ListKit::Hunt<PlayerRecord> (vtable 0x0064C1A8) for the manager's BeginPlayerWalk / StepPlayerWalk, and
// ListKit::Hunt<JoinerRecord> (0x0064C82C) for BeginJoinerWalk / StepJoinerWalk.
struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
struct NewPlayerSearch {
    void** m_vtable = kNewPlayerSearchVtable;
    void* m_listLink = nullptr;
};
inline uint8_t* FirstPlayer(Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<uint8_t*>(manager->BeginPlayerWalk(search, observers));
}
inline uint8_t* NextPlayer(Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<uint8_t*>(manager->StepPlayerWalk(search, observers));
}
inline uint8_t* FirstNewPlayer(Hub* manager, NewPlayerSearch* search)
{
    return static_cast<uint8_t*>(manager->BeginJoinerWalk(search));
}
inline uint8_t* NextNewPlayer(Hub* manager, NewPlayerSearch* search)
{
    return static_cast<uint8_t*>(manager->StepJoinerWalk(search));
}

// A player's connection's handle, for AppHost::PutOnOutbox (0x005434A0: 0 without a connection).
inline int PlayerHandle(uint8_t* player) { return AsPlayer(player)->GetLinkHandle(); }

// A message for the server through `app` (a client).
inline void SendToServer(NetLink::AppHost* app, NetLink::NetMessageInfo* desc)
{
    static_cast<NetLink::Guest*>(app)->SendToHost(desc);
}

// Script parameters: a new structure (the original's new CStruct - null if out of memory, then used anyway as the
// original does), deleted with Clear.
inline Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
inline void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// "team_<n + 1>_name" (the original's sprintf).
inline void TeamNameKey(char* out, int team)
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

} // namespace OnlinePlay::Detail
