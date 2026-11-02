#pragma once

#include <cstdint>

#include "engine/script/expression.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"

// Parsing compiled script tokens (see qb.h) into values: parameters of structures, arrays, expressions and
// the arguments of calls. `args` is the calling script's parameters, which <name> and <...> refer to.
namespace script {

class Script;

// The script running now (0x006F3530), and the list of scripts (null: the first).
Script* get_running_script();
Script* step_script_list(Script* script);

// Follow jumps and choose the branch of Random() blocks (weighted, no-repeat and permute) until a token
// that is neither.
const uint8_t* resolve_random_jumps(const uint8_t* token);
// Past one value token (structures and arrays included).
const uint8_t* skip_value(const uint8_t* token);
// The stored no-repeat / permute state of the random table `table` for the current script, or null.
StoredRandom* look_up_saved_random(const uint8_t* table, uint32_t type, uint32_t count);
// A new stored random state (at most 100 are kept: the oldest goes).
StoredRandom* new_stored_random();
// RandomRange(a, b) (`token` at the pair after the RandomRange token): an integer if both ends are whole
// numbers, else a float; `deterministic` uses the second random stream.
const uint8_t* parse_random_range(const uint8_t* token, Component* out, bool deterministic);
// If `component` names a global symbol of a value type, it becomes that value.
void evaluate_name_token(Component* component);

// One value into `out`.
const uint8_t* parse_value(const uint8_t* token, Struct* args, Component* out);
// One parameter, named `name`, added to `dest`.
const uint8_t* add_parameter(const uint8_t* token, uint32_t name, Struct* dest, Struct* args);
// The arguments of a call (to the end of the line or a closing parenthesis) into `dest`.
const uint8_t* parse_arguments(Struct* dest, const uint8_t* token, Struct* args);
// A { ... } structure's parameters into `dest`.
const uint8_t* parse_structure(Struct* dest, const uint8_t* token, Struct* args);
// A [ ... ] array (its type is the type of its elements; later elements' types win, but integers don't
// override floats).
const uint8_t* parse_array(Array* dest, const uint8_t* token, Struct* args);
// A ( ... ) expression, evaluated into `result`.
const uint8_t* parse_expression(const uint8_t* token, Struct* args, Component* result);

// A checksum-name token (0x2B) at `token`: its checksum and its text (in place); the token after it.
const uint8_t* read_name_token(const uint8_t* token, uint32_t* checksum, const char** name);

extern float g_parse_nudge;
extern StoredRandom* g_stored_randoms_newest;

// The script parser's expression evaluator (script/parse.cpp).
extern ValueResolver g_evaluator;

} // namespace script
