// The game's hooks for the graphics (see render_hooks.h). Each is the code the engine file called before (the same
// calls in the same order).

#include "game/skate/render_hooks.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "base/maths/vec4.h"
#include "engine/gfx/game_hooks.h"
#include "engine/objects/rail_manager.h"
#include "game/front_end/front_end.h"
#include "game/park_editing/park_editor.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"

namespace render {

namespace {

uint8_t career_level()
{
    const modules::Skate* skate = modules::Skate::sole();
    if (!skate) {
        return 0;
    }
    return static_cast<uint8_t>(skate->career()->level()); // (its low byte, as the original reads it)
}

bool multiplayer() { return (modules::Skate::is_multiplayer() & 1) != 0; }
uint32_t game_mode_name() { return goals::game_mode()->get_name_key(); }
bool game_halted() { return modules::front_end_instance()->is_halted(); }
uint32_t park_shell() { return park_editing::ParkBuilderMode::get_shell_checksum(); }
uint32_t level_script()
{
    const modules::Skate* skate = modules::Skate::sole();
    return skate ? skate->level() : 0;
}
// NOT ORIGINAL - the decals and clutter keep clear of the rails (engine/gfx/scatter.h)
void level_rails(std::vector<std::array<float, 6>>* segments)
{
    segments->clear();
    const modules::Skate* skate = modules::Skate::sole();
    objects::GrindRailHub* hub = skate ? skate->rail_manager().get() : nullptr;
    if (hub == nullptr) {
        return;
    }
    for (const objects::GrindPoint& node : hub->node_run()) {
        if ((node.flag_bits() & objects::kRailFlagClimbing) != 0U) {
            continue;
        }
        maths::Vec4 from;
        maths::Vec4 to;
        hub->get_absolute_pos(&from, &node);
        hub->get_absolute_pos(&to, node.succ() != nullptr ? node.succ() : &node);
        segments->push_back({from.x, from.y, from.z, to.x, to.y, to.z});
    }
}

} // namespace

void install_game_hooks()
{
    set_game_hooks({.career_level = &career_level,
                    .multiplayer = &multiplayer,
                    .game_mode_name = &game_mode_name,
                    .game_halted = &game_halted,
                    .park_shell = &park_shell,
                    .level_script = &level_script,
                    .level_rails = &level_rails});
}

} // namespace render
