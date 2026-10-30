#pragma once

// Helpers shared by the network manager's message handlers (gamenet_handlers.cpp, gamenet_game_handlers.cpp,
// gamenet_tasks.cpp): the context's parts, the singletons and the player calls not rewritten yet, script parameters.

#include <cstdint>
#include <cstring>

#include "game/core/alloc.h"
#include "game/goals/goal_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"
#include "hook/game_code.h"

namespace Obj {
class CObject;
} // namespace Obj

namespace GameNet {
extern void** const kNewPlayerSearchVtable; // 0x0064C82C: Lst::Search<NewPlayerInfo> (gamenet_handlers.cpp)
} // namespace GameNet

namespace GameNet::Detail {

// Handler results.
constexpr int kContinue = Net::kHandlerContinue;
constexpr int kHalt = Net::kHandlerHalt;
constexpr int kDone = Net::kHandlerMsgDone;
constexpr int kKeep = Net::kHandlerKeep;

constexpr uint8_t kGroupGame = 8; // the sequenced group of the game's messages

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
// A thiscall into code not rewritten yet.
template <typename R = void, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return GameFunction<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}

// The same, into code of ours reached through its address (GAME_CODE).
template <typename R = void, typename... A>
R Call(void* code, const void* object, A... args)
{
    return GameFunction<R(__thiscall*)(const void*, A...)>(reinterpret_cast<uintptr_t>(code))(object, args...);
}

inline Manager* ManagerOf(Net::MsgHandlerContext* context) { return static_cast<Manager*>(context->m_data); }
inline const uint8_t* Message(Net::MsgHandlerContext* context)
{
    return reinterpret_cast<const uint8_t*>(context->m_Msg);
}
inline uint32_t MessageWord(Net::MsgHandlerContext* context, uint32_t offset)
{
    return *reinterpret_cast<const uint32_t*>(context->m_Msg + offset);
}
inline void Run(uint32_t name, Script::CStruct* params = nullptr, Obj::CObject* object = nullptr)
{
    Script::RunScript(name, params, object, false, nullptr);
}

// The singletons (not rewritten): the skate module, the game mode, the goal manager.
inline uint8_t* Skate() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); }
inline uint8_t* GameMode() { return reinterpret_cast<uint8_t*>(Game::g_gameMode); }
inline bool IsTeamGame() { return Call<bool>(GAME_CODE(0x005535F0, &Game::CGameMode::IsTeamGame), GameMode()); }
inline uint32_t GameType() { return Call<uint32_t>(GAME_CODE(0x005532A0, &Game::CGameMode::GetNameChecksum), GameMode()); }
inline uint8_t* GoalManager() { return reinterpret_cast<uint8_t*>(Game::GetGoalManager()); }
inline uint8_t* GetGoal(uint8_t* goals, uint32_t name) { return Call<uint8_t*>(GAME_CODE(0x00557F90, &Game::CGoalManager::GetGoal), goals, name, 1); }

// The manager's players (GameNet::PlayerInfo, player_info.h; reached as bytes here): +0x14 the skater, +0x18 the
// connection, +0x30 the team, +0x38 the name, +0x1D0 flags.
inline PlayerInfo* AsPlayer(const uint8_t* player) { return reinterpret_cast<PlayerInfo*>(const_cast<uint8_t*>(player)); }
inline uint8_t* PlayerByConnection(Manager* manager, Net::Conn* conn)
{
    return manager->PlayerByConnection(conn);
}
// (the original pushes the id as 16 bits)
inline uint8_t* PlayerByObjectId(Manager* manager, uint16_t id) { return manager->PlayerByObjectId(id); }
inline uint8_t* PlayerByObjectId(Manager* manager, const uint8_t* id) // a signed byte, widened
{
    return PlayerByObjectId(manager, static_cast<uint16_t>(static_cast<int8_t>(*id)));
}
inline uint8_t* LocalPlayer(Manager* manager) { return static_cast<uint8_t*>(manager->GetLocalPlayer()); }
inline uint8_t* KingOfTheHill(Manager* manager) { return static_cast<uint8_t*>(manager->GetKingOfTheHill()); }
// (0 or 1 in EAX: the original's callers test AL)
inline bool IsLocalPlayer(const uint8_t* player) { return AsPlayer(player)->IsLocalPlayer() != 0; }
inline bool IsObserver(const uint8_t* player) { return AsPlayer(player)->IsObserver() != 0; }
inline Obj::CObject* PlayerSkater(const uint8_t* player) { return Field<Obj::CObject*>(player, 0x14); }
inline uint32_t SkaterId(const void* skater) { return Field<uint32_t>(skater, 0x14); }
inline const char* PlayerName(uint8_t* player) { return reinterpret_cast<const char*>(player + 0x38); }
inline int PlayerTeam(const uint8_t* player) { return Field<int>(player, 0x30); }

// Lst::Search<PlayerInfo> (vtable 0x0064C1A8) for the manager's FirstPlayerInfo / NextPlayerInfo, and
// Lst::Search<NewPlayerInfo> (0x0064C82C) for FirstNewPlayerInfo / NextNewPlayerInfo.
struct PlayerSearch {
    void** m_vtable = reinterpret_cast<void**>(0x0064C1A8);
    void* m_node = nullptr;
};
struct NewPlayerSearch {
    void** m_vtable = kNewPlayerSearchVtable;
    void* m_node = nullptr;
};
inline uint8_t* FirstPlayer(Manager* manager, PlayerSearch* search, bool observers)
{
    return static_cast<uint8_t*>(manager->FirstPlayerInfo(search, observers));
}
inline uint8_t* NextPlayer(Manager* manager, PlayerSearch* search, bool observers)
{
    return static_cast<uint8_t*>(manager->NextPlayerInfo(search, observers));
}
inline uint8_t* FirstNewPlayer(Manager* manager, NewPlayerSearch* search)
{
    return static_cast<uint8_t*>(manager->FirstNewPlayerInfo(search));
}
inline uint8_t* NextNewPlayer(Manager* manager, NewPlayerSearch* search)
{
    return static_cast<uint8_t*>(manager->NextNewPlayerInfo(search));
}

// A player's connection's handle, for App::EnqueueMessage (0x005434A0: 0 without a connection).
inline int PlayerHandle(uint8_t* player) { return AsPlayer(player)->GetConnHandle(); }

// A message for the server through `app` (a client).
inline void SendToServer(Net::App* app, Net::MsgDesc* desc)
{
    static_cast<Net::Client*>(app)->EnqueueMessageToServer(desc);
}

// Script parameters: a new structure (the original's new CStruct - null if out of memory, then used anyway as the
// original does), deleted with Clear.
inline Script::CStruct* NewParams()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
inline void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
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

} // namespace GameNet::Detail
