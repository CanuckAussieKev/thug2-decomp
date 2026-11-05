#pragma once

// NOT ORIGINAL - what the engine's input asks of the game above it, without knowing the game's classes: whether the
// front end's menus are up, the port the front end takes its input from, whether the park editor is editing, and
// THAW's BMX's half-stick key. The game installs them once (sio::install_game_hooks, game/front_end/pad_hooks.h),
// from app/main.cpp before any of the original's code runs; they are never changed after that. Each one is the game's
// own code that the original called at that place: one indirect call in place of a direct one (or of an inline read
// of the game's state), never a call added per poll. Not installed, a call is a null call (as the original's
// singletons before start-up).
namespace sio {

struct GameHooks {
    // Whether the front end has a menu up (the game paused, the skate shop or the keyboard: modules::menus_active).
    bool (*menus_active)();
    // The port the front end is taking input from (the profile roster's cursor: 0x00553E90 on 0x007CE4B4).
    int (*menu_port)();
    // Whether there is a park editor and it is in its edit state (ParkBuilderMode::sole, its editor_state).
    bool (*park_editing)();
    // THAW's BMX ([features] thaw_bmx; ours, game/rider/thaw_bmx.h): whether the half-stick key applies (the bike is
    // ridden).
    bool (*half_stick_key)();
};

void set_game_hooks(const GameHooks& hooks);
const GameHooks& game_hooks();

} // namespace sio
