#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/script/expression.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/script/param_struct.h"

// Parsing compiled script tokens (see qb.h) into values: parameters of structures, arrays, expressions and
// the arguments of calls. `args` is the calling script's parameters, which <name> and <...> refer to.
namespace script {

class Script;

// The script running now (0x006F3530).
Script* get_running_script();

// Follow jumps and choose the branch of Random() blocks (weighted, no-repeat and permute) until a token
// that is neither.
Tokens resolve_random_jumps(Tokens token);
// Past one value token (structures and arrays included).
Tokens skip_value(Tokens token);
// The stored no-repeat / permute state of the random table `table` for the current script, or null.
StoredRandom* look_up_saved_random(const uint8_t* table, QbToken type, uint32_t count);
// RandomNoRepeat / RandomPermute's choice by its stored state (found by `table`): QB's and TypeScript's (ts_bridge.cpp).
uint32_t pick_stored_random(const uint8_t* table, QbToken kind, uint32_t count);
// NOT ORIGINAL: drops the stored randoms whose Random lies in the `size` bytes at `begin` (their code freed).
void forget_stored_randoms(const uint8_t* begin, std::size_t size);
// NOT ORIGINAL: a script's RandomNoRepeat / RandomPermute state lasts as long as its definition, not its cached code.
// The cache frees a script's code when it is released or evicted and decompresses it again elsewhere when it is next
// fetched; the original's state stays keyed by the old address (found again only when the allocator happens to give
// the same block back). Here the cache parks a script's entries when its code goes (name + place in the code), puts
// them back at the new code when it is fetched again, and drops them when the definition changes or goes.
void park_stored_randoms(const uint8_t* begin, std::size_t size, uint32_t name);
void unpark_stored_randoms(const uint8_t* code, uint32_t name);
void drop_parked_randoms(uint32_t name);
// (a stored random freed: no longer parked)
void forget_parked(const void* stored);
// A new stored random state (at most 100 are kept: the oldest goes).
StoredRandom* new_stored_random();
// RandomRange(a, b) (`token` at the pair after the RandomRange token): an integer if both ends are whole
// numbers, else a float; `deterministic` uses the second random stream.
Tokens parse_random_range(Tokens token, Component* out, bool deterministic);
// If `component` names a global symbol of a value type, it becomes that value.
void evaluate_name_token(Component* component);

// One value into `out`.
Tokens parse_value(Tokens token, Struct* args, Component* out);
// One parameter, named `name`, added to `dest`.
Tokens add_parameter(Tokens token, uint32_t name, Struct* dest, Struct* args);
// The arguments of a call (to the end of the line or a closing parenthesis) into `dest`.
Tokens parse_arguments(Struct* dest, Tokens token, Struct* args);
// A { ... } structure's parameters into `dest`.
Tokens parse_structure(Struct* dest, Tokens token, Struct* args);
// A [ ... ] array (its type is the type of its elements; later elements' types win, but integers don't
// override floats).
Tokens parse_array(Array* dest, Tokens token, Struct* args);
// A ( ... ) expression, evaluated into `result`.
Tokens parse_expression(Tokens token, Struct* args, Component* result);

// A checksum-name token (0x2B) at `token`: its checksum and its text (in place); the token after it.
Tokens read_name_token(Tokens token, uint32_t* checksum, const char** name);

inline constexpr float kParseNudge{0.1F};

// The script parser's expression evaluator (script/parse.cpp).

} // namespace script
