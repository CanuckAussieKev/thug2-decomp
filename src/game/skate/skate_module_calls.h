#pragma once

// The skate module (GameModules::Skate, the singleton 0x007CE478) and the game mode (0x007CE470) as the script commands of
// 0x005AAEF0-0x005B0A40 use them (career_cfuncs.cpp, competition_cfuncs.cpp, ...): the fields they read and the
// calls into code that had no source yet.

#include <cstdint>

#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"

namespace GameObjects {
class CGameEntity;
struct CRiderIdentity;
} // namespace Obj

namespace SkateCalls {

// The skate module and a field of it.
inline uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::Skate::Sole()); }
template <typename T>
T& SkateField(uint32_t offset)
{
    return *reinterpret_cast<T*>(Skate() + offset);
}
// +0x20: the career. +0x46: the run is to end (EndRun). +0x360: the competition. +0x364: the horse game.
// +0x384: a record was set by the last UpdateRecords.
inline GameObjects::CRiderStoryProgress* Career() { return SkateField<GameObjects::CRiderStoryProgress*>(0x20); }
inline bool& EndRunFlag() { return SkateField<bool>(0x46); }
inline uint8_t* Competition() { return SkateField<uint8_t*>(0x360); }
inline uint8_t* Horse() { return SkateField<uint8_t*>(0x364); }

// 0x005665C0: skater `index` of the list (null past the end). 0x00566520: the first local skater (null: none).
// 0x00566600: the skater whose object id is `id` (null: none).
inline GameObjects::CGameEntity* GetRiderAt(int index)
{
    return GameModules::Skate::Sole()->GetRiderAt(index);
}
inline GameObjects::CGameEntity* GetHomeRider()
{
    // (a CRider is a CGameEntity: the same pointer)
    return reinterpret_cast<GameObjects::CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
}
inline GameObjects::CGameEntity* GetRiderByNumber(uint32_t id)
{
    return reinterpret_cast<GameObjects::CGameEntity*>(GameModules::Skate::Sole()->GetRiderByNumber(id));
}
// 0x00564FC0: the profile manager's profile `index`. 0x00564F80: the profile `params`' integer `skater` names (none,
// or no params: the current one).
inline GameObjects::CRiderIdentity* FetchAccount(int index)
{
    return GameModules::Skate::Sole()->FetchAccount(index);
}
inline GameObjects::CRiderIdentity* GetProfileFromParams(void* params)
{
    return GameModules::Skate::Sole()->FetchAccount(static_cast<Scripts::CStruct*>(params));
}
// 0x005651F0: a game of more than one player (a network game, or the game mode's player count 2 or more).
inline bool IsMultiplayer() { return GameModules::Skate::IsMultiplayer() != 0; }

// The game mode.
inline void* GameMode() { return GoalSystem::g_gameMode; }
// 0x00553220: the game mode's integer `property` (is_career, is_horse ...) is non-zero.
inline bool GameModeIsTrue(uint32_t property)
{
    return static_cast<GoalSystem::CMatchRules*>(GameMode())->IsTrue(property);
}
// 0x00553910: elimiskate's end - everyone eliminated but one (or every scavenger hunt player done).
inline bool GameModeShouldEndRun() { return static_cast<GoalSystem::CMatchRules*>(GameMode())->FinishRulesReached(); }

} // namespace SkateCalls
