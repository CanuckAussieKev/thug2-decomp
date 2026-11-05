// The game's node commands for the engine (see node_hooks.h).

#include "game/commands/node_hooks.h"

#include "game/commands/game_cfuncs.h"
#include "engine/script/node_hooks.h"

namespace commands {

void install_node_hooks() { script::set_node_commands(&create_from_node_name, &set_node_color); }

} // namespace commands
