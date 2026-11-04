// The engine's way to the game's node commands (see node_hooks.h).

#include "engine/script/node_hooks.h"

#include <cstdint>

namespace script {

namespace {

struct NodeCommands {
    NodeCommandById create_from_node{nullptr};
    NodeColourCommand set_color{nullptr};
};

NodeCommands& node_commands()
{
    static NodeCommands commands;
    return commands;
}

} // namespace

void set_node_commands(NodeCommandById create_from_node, NodeColourCommand set_color)
{
    node_commands() = {.create_from_node = create_from_node, .set_color = set_color};
}

bool create_object_from_node(uint32_t node_name)
{
    return node_commands().create_from_node != nullptr && node_commands().create_from_node(node_name);
}

bool set_node_color_from(uint32_t node_name, Struct* params)
{
    return node_commands().set_color != nullptr && node_commands().set_color(node_name, params);
}

} // namespace script
