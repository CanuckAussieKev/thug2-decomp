#pragma once

#include <cstdint>

// NOT ORIGINAL - the two node commands the engine's time of day and colour cycles call (an object made from a node,
// a node's colour set), answered by the game's script commands (game/commands/node_hooks.h), which install them at
// start-up. Not set: they do nothing and say false.
namespace script {

class Struct;

using NodeCommandById = bool (*)(uint32_t node_name);
using NodeColourCommand = bool (*)(uint32_t node_name, Struct* params);
void set_node_commands(NodeCommandById create_from_node, NodeColourCommand set_color);

// The object of the node named `node_name` made (Obj's create_from_node).
bool create_object_from_node(uint32_t node_name);
// The node `node_name`'s colour set from `params` (color=).
bool set_node_color_from(uint32_t node_name, Struct* params);

} // namespace script
