// The game's hooks for the system code (see sys_hooks.h). Each is the code the engine file called before (the same
// calls in the same order).

#include "game/skate/sys_hooks.h"

#include "engine/sys/game_hooks.h"
#include "game/front_end/front_end.h"
#include "game/online/gamenet_manager.h"
#include "game/online/thugpro_compat.h"
#include "game/skate/skate_module.h"

namespace sys {

namespace {

void service_network() { online::Hub::service_game_spy(); }
void refresh_position_scale() { online::thugpro::refresh_position_scale(); }
void after_render() { modules::Skate::debug_rails(); }
bool front_end_halted(const modules::FrontEnd* front_end) { return front_end->is_paused(); }

} // namespace

void install_game_hooks()
{
    set_game_hooks({.service_network = &service_network,
                    .refresh_position_scale = &refresh_position_scale,
                    .after_render = &after_render,
                    .front_end_halted = &front_end_halted});
}

} // namespace sys
