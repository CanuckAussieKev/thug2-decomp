#pragma once

#include <cstdint>
#include "engine/lists/pointer_ring.h"

namespace script {
class Context;
class Struct;
} // namespace script

// The cutscene destroy list (0x007CEDD8: a std::list of commands::CutsceneDestroyEntry, movie_cfuncs.h): what a
// cutscene made and how to get rid of it - each entry's destroy script run with its parameters when the list is
// flushed. Its commands (0x005B5320-0x005B5770, 0x005B5BE0) and the std::list constructor (0x005B4650) the game's
// static lists share. The commands are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

struct CutsceneDestroyEntry;
// The cutscene destroy list (lists::PointerRing: its constructor 0x005B4650 is shared with 0x006F0DA8 and the COIM
// instance list 0x006F0DB0).
using CutsceneDestroyList = lists::PointerRing<CutsceneDestroyEntry>;

bool cfunc_add_to_cutscene_destroy_list(script::Struct* params, script::Context context);
bool cfunc_remove_from_cutscene_destroy_list(script::Struct* params, script::Context context);
bool cfunc_cutscene_spawn_script(script::Struct* params, script::Context context);
bool cfunc_flush_cutscene_destroy_list(script::Struct* params, script::Context context);

} // namespace commands
