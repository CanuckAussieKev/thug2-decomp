#pragma once

// NOT ORIGINAL - the simulation trace's hook in the script code (devtools/demo.h -demo-trace): each command a script
// runs - a statement's script, C function or member function, a C function inside an expression - told to the trace
// with its arguments, the same from a QB statement and from a TypeScript level's call (docs/design/simulation-rate.md
// "The trace"). Never set unless a playback asks for the trace: one null test where it sits.

#include <cstdint>

#include "engine/script/fwd.h"

namespace script::trace_hooks {

enum class CommandKind : uint8_t {
    kStatement = 0, // a statement's command (on the script's object, or on `object` when named)
    kExpression = 1, // a C function inside an expression
    kStart = 2,      // a script record given a script to run (its arguments; the trace adds who started it)
};

using Command = void (*)(const Script* script, CommandKind kind, uint32_t name, uint32_t object, Struct* args);

struct Hooks {
    Command command = nullptr;
};

inline Hooks& hooks()
{
    static Hooks set;
    return set;
}

// Tell the trace (when it is on).
inline void note_command(const Script* script, CommandKind kind, uint32_t name, uint32_t object, Struct* args)
{
    if (const Command hook = hooks().command) {
        hook(script, kind, name, object, args);
    }
}

} // namespace script::trace_hooks
