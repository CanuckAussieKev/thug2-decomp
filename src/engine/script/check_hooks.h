#pragma once

// NOT ORIGINAL - the script check's hooks in the engine's script code (src/script_qb/script_check.h: the headless
// harness that runs every script on the real interpreter, QB and TypeScript, against stubbed commands). In the game
// they are never set and cost one null test where they sit.

#include <cstddef>
#include <cstdint>
#include <optional>

#include "engine/script/fwd.h"

namespace script::check_hooks {

// A C function called inside an expression (push_name): the check's answer (its stub), or none - call it.
using ExpressionCall = std::optional<bool> (*)(uint32_t name, Struct* params, Script* script);
// A Random's branch chosen: the Random token's place in its code block, the branch drawn (of `count`) - the branch to
// take (the check may force another).
using RandomChoice = uint32_t (*)(const Script* script, std::ptrdiff_t place, uint32_t choice, uint32_t count);

struct Hooks {
    ExpressionCall expression_call = nullptr;
    RandomChoice random_choice = nullptr;
};

inline Hooks& hooks()
{
    static Hooks set;
    return set;
}

} // namespace script::check_hooks
