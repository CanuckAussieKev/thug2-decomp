#pragma once

// The skate module (Mdl::Skate, the singleton 0x007CE478) and the game mode (0x007CE470) as the script commands of
// 0x005AAEF0-0x005B0A40 use them (career_cfuncs.cpp, competition_cfuncs.cpp, ...): the fields they read and the
// calls into code that has no source yet (each a GameFunction at its address).

#include <cstdint>

#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"

namespace Obj {
class CCompositeObject;
struct CSkaterProfile;
} // namespace Obj

namespace SkateCalls {

// The skate module and a field of it.
inline uint8_t* Skate() { return reinterpret_cast<uint8_t*>(Mdl::Skate::Instance()); }
template <typename T>
T& SkateField(uint32_t offset)
{
    return *reinterpret_cast<T*>(Skate() + offset);
}
// +0x20: the career. +0x46: the run is to end (EndRun). +0x360: the competition. +0x364: the horse game.
// +0x384: a record was set by the last UpdateRecords.
inline Obj::CSkaterCareer* Career() { return SkateField<Obj::CSkaterCareer*>(0x20); }
inline bool& EndRunFlag() { return SkateField<bool>(0x46); }
inline uint8_t* Competition() { return SkateField<uint8_t*>(0x360); }
inline uint8_t* Horse() { return SkateField<uint8_t*>(0x364); }

// 0x005665C0: skater `index` of the list (null past the end). 0x00566520: the first local skater (null: none).
// 0x00566600: the skater whose object id is `id` (null: none).
inline Obj::CCompositeObject* GetSkater(int index)
{
    return Mdl::Skate::Instance()->GetSkater(index);
}
inline Obj::CCompositeObject* GetLocalSkater()
{
    // (a CSkater is a CCompositeObject: the same pointer)
    return reinterpret_cast<Obj::CCompositeObject*>(Mdl::Skate::Instance()->GetLocalSkater());
}
inline Obj::CCompositeObject* GetSkaterById(uint32_t id)
{
    return reinterpret_cast<Obj::CCompositeObject*>(Mdl::Skate::Instance()->GetSkaterById(id));
}
// 0x00564FC0: the profile manager's profile `index`. 0x00564F80: the profile `params`' integer `skater` names (none,
// or no params: the current one).
inline Obj::CSkaterProfile* GetProfile(int index)
{
    return Mdl::Skate::Instance()->GetProfile(index);
}
inline Obj::CSkaterProfile* GetProfileFromParams(void* params)
{
    return Mdl::Skate::Instance()->GetProfile(static_cast<Script::CStruct*>(params));
}
// 0x005651F0: a game of more than one player (a network game, or the game mode's player count 2 or more).
inline bool IsMultiplayer() { return Mdl::Skate::IsMultiplayer() != 0; }

// The game mode.
inline void* GameMode() { return Game::g_gameMode; }
// 0x00553220: the game mode's integer `property` (is_career, is_horse ...) is non-zero.
inline bool GameModeIsTrue(uint32_t property)
{
    return static_cast<Game::CGameMode*>(GameMode())->IsTrue(property);
}
// 0x00553910: elimiskate's end - everyone eliminated but one (or every scavenger hunt player done).
inline bool GameModeShouldEndRun() { return static_cast<Game::CGameMode*>(GameMode())->EndConditionsMet(); }

} // namespace SkateCalls
