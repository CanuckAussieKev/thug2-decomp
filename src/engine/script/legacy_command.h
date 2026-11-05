#pragma once

#include "engine/script/context.h"

// NOT ORIGINAL - a bridge while the commands move to script::Context (stage 11 round 22): a command still written
// `bool f(Struct* params, Script* script)` goes into a command table as `&script::legacy_command<&f>`, a NativeFunction
// that calls it with the context's script. Gone once every command takes a Context.
namespace script {

template <bool (*Command)(Struct*, Script*)>
bool legacy_command(Struct* params, Context context)
{
    return Command(params, context.script());
}

} // namespace script
