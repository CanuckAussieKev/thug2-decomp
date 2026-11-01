#pragma once

#include <cstdint>

// The level's nodes (level_nodes): each node is a structure in a node array (node_manager.h), known by its name=
// checksum; links= lists the names of the nodes it links to; pos= (or the file's position=, z negated) / angles= /
// orientation= place it. Nodes are found through the node manager's name hash (0x006F0D84). From the disassembly of
// 0x0045BE10-0x0045C590 (all cdecl).
namespace maths {
class Vec4;
class Mat4;
} // namespace maths
namespace script {
class Array;
class Struct;
} // namespace script

namespace level_nodes {

// --- a node, by its structure ---
// 0x0045BE40: its name= (0 if none).
uint32_t get_node_name(script::Struct* node);
// 0x0045C020: how many links= it has (0 if none).
uint32_t get_link_count(script::Struct* node);
// 0x0045BE10: the name of its link `index` [it has to have links=: the original reads a null array otherwise].
uint32_t fetch_link(script::Struct* node, int index);
// 0x0045C420: the node its link `index` names (fetch_vertex, asserting).
script::Struct* fetch_neighbour_node(script::Struct* node, int index);
// 0x0045C4B0: whether one of its links is `target`.
bool has_link_to(script::Struct* node, script::Struct* target);
// 0x0045C050: `out` = its pos=, else its position= with z negated, else (0, 0, 0, 1). Whether it had either.
bool fetch_place(script::Struct* node, maths::Vec4* out);
// 0x0045C180: `out` = its angles= (default (0, 0, 0, 1)). Whether it had them.
bool read_turn_angles(script::Struct* node, maths::Vec4* out);
// 0x0045C0B0: `out` = the rotation of its orientation= (a unit quaternion's x, y, z; w = sqrt(1 - x2 - y2 - z2),
// 0 if they're longer than 1); the identity if it has none.
void fetch_facing(script::Struct* node, maths::Mat4* out);

// --- a node, by its name ---
// 0x0045C1B0: the node named `name` (null if there's none; `assert` isn't used).
script::Struct* fetch_vertex(uint32_t name, bool assert);
// 0x0045BE70: whether there's a node named `name`.
bool node_exists(uint32_t name);
// 0x0045C3A0 / 0x0045C3E0 / 0x0045C460 / 0x0045C560 / 0x0045C590: as above, for the node named `name`.
uint32_t get_link_count(uint32_t name);
uint32_t fetch_link(uint32_t name, int index);
script::Struct* fetch_neighbour_node(uint32_t name, int index);
bool fetch_place(uint32_t name, maths::Vec4* out);
bool read_turn_angles(uint32_t name, maths::Vec4* out);
// 0x0045C520: whether the node named `name` links to the node named `target`.
bool has_link_to(uint32_t name, uint32_t target);
// 0x0045C2D0: of the nodes whose names start with `prefix` (node_prefixes.h), the one nearest `pos` (the first
// of equals; null if there are none) [a node without pos= is taken to be where the one before it was].
script::Struct* find_closest_node_with_prefix(uint32_t prefix, const maths::Vec4* pos);

// --- the calls scripts make ---
// What a call is handed to: its arguments (parsed with the calling script's) and its first token.
using ScriptCallCallback = void (*)(script::Struct* params, const uint8_t* call);
// 0x0045C950: every call to `target` (a script, C function or member function) in the script `script`, the
// scripts it calls, the scripts SetEventHandler's scr= and (when the target is EndGap) EndGap's gapscript= name,
// down to 5 levels: `callback` for each (a call to a script is followed into whether or not it is the target).
// The statements are read from the start of each line, except that a name that isn't a script or function, or is
// assigned to, is passed over by itself (its arguments read as statements). Called with `params` for the
// arguments of the script's own call, `depth` 0.
void for_each_script_call(uint32_t script, uint32_t target, ScriptCallCallback callback, script::Struct* params,
                          int depth);
// 0x0045CCA0: for_each_script_call over the script each node names by `script_param` (e.g. TriggerScript=), with the
// script flag 0x006F2564 cleared meanwhile (the gap list finds every EndGap this way).
void for_each_node_script_call(uint32_t script_param, uint32_t target, ScriptCallCallback callback);

// --- the node manager's walk and last node array, on the instance (0x0045C1D0-0x0045C230) ---
void start_node_walk(uint32_t name);
script::Struct* get_next_node();
void end_node_walk();
script::Array* get_last_node_array();
uint32_t get_node_owner_value58();
uint32_t get_last_node_array_name();

} // namespace level_nodes
