#pragma once

namespace modules {
struct FrontEnd;
}

// NOT ORIGINAL - what the engine's system code (the main loop, debug mode) asks of the game above it, without knowing
// the game's classes: the online session's GameSpy service and THUGPRO's position scale, the skate module's rail
// debugging, the front end's pause. The game installs them once (sys::install_game_hooks, game/skate/sys_hooks.h),
// from app/main.cpp before any of the original's code runs; they are never changed after that. Each one is the game's
// own code that the original called at that place: one indirect call in place of a direct one, never a call added.
// Not installed, a call is a null call (as the original's singletons before start-up).
namespace sys {

struct GameHooks {
    // Once a frame, before the frame's wait: the online session's GameSpy service (online::Hub::service_game_spy).
    void (*service_network)();
    // Once a frame after it (ours: THUGPRO's big-level positions in network games - TP-R6,
    // online::thugpro::refresh_position_scale).
    void (*refresh_position_scale)();
    // After the frame's render: the skate module's rail debugging (modules::Skate::debug_rails).
    void (*after_render)();
    // Whether the front end `front_end` has the game paused (+0x30) - debug mode's frame.
    bool (*front_end_halted)(const modules::FrontEnd* front_end);
};

void set_game_hooks(const GameHooks& hooks);
const GameHooks& game_hooks();

} // namespace sys
