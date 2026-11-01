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

// The skate module: its career, the run is to end (EndRun), the competition, the horse game.
inline modules::Skate* get_skate() { return modules::Skate::sole(); }
inline objects::RiderStoryProgress* get_career() { return get_skate()->career_; }
inline bool& end_run_flag() { return get_skate()->run_over_; }
inline uint8_t* get_competition() { return reinterpret_cast<uint8_t*>(get_skate()->competition_); }
inline uint8_t* get_horse() { return reinterpret_cast<uint8_t*>(get_skate()->horse_); }

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
