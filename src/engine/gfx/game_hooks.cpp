// The graphics' hooks into the game (see game_hooks.h).

#include "engine/gfx/game_hooks.h"

namespace render {

namespace {

GameHooks& installed_hooks()
{
    static GameHooks hooks{};
    return hooks;
}

} // namespace

void set_game_hooks(const GameHooks& hooks) { installed_hooks() = hooks; }

const GameHooks& game_hooks() { return installed_hooks(); }

} // namespace render
