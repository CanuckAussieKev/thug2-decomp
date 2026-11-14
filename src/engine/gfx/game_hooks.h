#pragma once

#include <cstdint>

// NOT ORIGINAL - what the engine's graphics ask of the game above them, without knowing the game's classes: the skate
// module's career level and players, the game mode, the front end's pause and the park editor's shell (the screen's
// aspect and the time of day's lights). The game installs them once (render::install_game_hooks,
// game/skate/render_hooks.h), from app/main.cpp before any of the original's code runs; they are never changed after
// that. Each one is the game's own code that the original called at that place: one indirect call in place of a
// direct one, never a call added per object or per draw. Not installed, a call is a null call (as the original's
// singletons before start-up).
namespace render {

struct GameHooks {
    // The career's level (modules::Skate's career, its low byte as the original reads it): 0 in the front end, and
    // 0 without a skate module.
    uint8_t (*career_level)();
    // Whether this is a multiplayer game (modules::Skate::is_multiplayer, bit 0: a network game or more than one
    // local player).
    bool (*multiplayer)();
    // The game mode's name (goals::game_mode() 0x007CE470: MatchRules::get_name_key).
    uint32_t (*game_mode_name)();
    // Whether the front end (0x007CE464) has the game paused (+0x30).
    bool (*game_halted)();
    // The park editor's shell: the scene of the level being played (ParkBuilderMode::get_shell_checksum).
    uint32_t (*park_shell)();
    // (added by the time of day - engine/gfx/day_clock.h) The skate module's level - its load script's checksum
    // (load_mainmenu in the front end; modules::Skate::level) -, 0 without a skate module.
    uint32_t (*level_script)();
};

void set_game_hooks(const GameHooks& hooks);
const GameHooks& game_hooks();

} // namespace render
