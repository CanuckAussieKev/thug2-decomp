#pragma once

// The skate module (modules::Skate, the singleton 0x007CE478) and the game mode (0x007CE470) as the script commands of
// 0x005AAEF0-0x005B0A40 use them (career_cfuncs.cpp, competition_cfuncs.cpp, ...): the fields they read and the
// calls into code that had no source yet.

#include <cstdint>

#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"

namespace objects {
class GameEntity;
struct RiderIdentity;
} // namespace objects

namespace skate_calls {

// The skate module and a field of it.
inline uint8_t* get_skate() { return reinterpret_cast<uint8_t*>(modules::Skate::sole()); }
template <typename T>
T& skate_field(uint32_t offset)
{
    return *reinterpret_cast<T*>(get_skate() + offset);
}
// +0x20: the career. +0x46: the run is to end (EndRun). +0x360: the competition. +0x364: the horse game.
// +0x384: a record was set by the last UpdateRecords.
inline objects::RiderStoryProgress* get_career() { return skate_field<objects::RiderStoryProgress*>(0x20); }
inline bool& end_run_flag() { return skate_field<bool>(0x46); }
inline uint8_t* get_competition() { return skate_field<uint8_t*>(0x360); }
inline uint8_t* get_horse() { return skate_field<uint8_t*>(0x364); }

// 0x005665C0: skater `index` of the list (null past the end). 0x00566520: the first local skater (null: none).
// 0x00566600: the skater whose object id is `id` (null: none).
inline objects::GameEntity* get_rider_at(int index) { return modules::Skate::sole()->get_rider_at(index); }
inline objects::GameEntity* get_home_rider()
{
    // (a Rider is a GameEntity: the same pointer)
    return reinterpret_cast<objects::GameEntity*>(modules::Skate::sole()->get_home_rider());
}
inline objects::GameEntity* get_rider_by_number(uint32_t id)
{
    return reinterpret_cast<objects::GameEntity*>(modules::Skate::sole()->get_rider_by_number(id));
}
// 0x00564FC0: the profile manager's profile `index`. 0x00564F80: the profile `params`' integer `skater` names (none,
// or no params: the current one).
inline objects::RiderIdentity* fetch_account(int index) { return modules::Skate::sole()->fetch_account(index); }
inline objects::RiderIdentity* get_profile_from_params(void* params)
{
    return modules::Skate::sole()->fetch_account(static_cast<script::Struct*>(params));
}
// 0x005651F0: a game of more than one player (a network game, or the game mode's player count 2 or more).
inline bool is_multiplayer() { return modules::Skate::is_multiplayer() != 0; }

// The game mode.
inline void* get_game_mode() { return goals::g_game_mode; }
// 0x00553220: the game mode's integer `property` (is_career, is_horse ...) is non-zero.
inline bool game_mode_is_true(uint32_t property)
{
    return static_cast<goals::MatchRules*>(get_game_mode())->is_true(property);
}
// 0x00553910: elimiskate's end - everyone eliminated but one (or every scavenger hunt player done).
inline bool game_mode_should_end_run()
{
    return static_cast<goals::MatchRules*>(get_game_mode())->finish_rules_reached();
}

} // namespace skate_calls
