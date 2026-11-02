// Functions that do (almost) nothing. The linker folded identical functions into one, so each address below
// stands for many functions of the original source (cut debug/console script commands, empty virtual
// methods...), and some are reached through a 5-byte jump of their own. They're too short for the hook's
// jump, so they're registered source-only.

#include "engine/core/stubs.h"

#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/file/async.h"

namespace stubs {

// 0x005B9760 (also through the jump at 0x005B6570): e.g. the script commands that are stubs returning true
bool return_true() { return true; }

// 0x005B9E50: ...returning false
bool return_false() { return false; }

// 0x005BA1D0: functions returning 0 / null
int return_zero() { return 0; }

// 0x005480A0: an empty cdecl function - the most called address in the game (528 calls). Compiled-out debug
// code: the callers still push its arguments and pop them after. In our source it appears under its uses'
// names, as empty inline functions: memory::PushMemProfile / PopMemProfile, memory::debug_track_block,
// Scripts::dump_to_log (more as their callers are rewritten). (The bytes
// after its RET, 0x005480A1-0x005480BE, are PartyMod's: a code cave its exe patch put in the padding, which
// loads its DLL (the name at 0x0064B198) - not the game's.)
void do_nothing() {}

// 0x005BAB90: functions that ignore their one (stack) argument
void ignore_argument(int) {}
void ignore_argument(const void*) {}
// 0x0048D090: ...their three - the engine font's string size and the geom's slot 41
void ignore_arguments3(int, int, int) {}
void ignore_arguments3(const void*, int, int) {}

// 0x0049F4C0: ...returning 0 / null whatever their two - the gap checks' "0"
int return_zero2(int, int) { return 0; }
int return_zero2(const void*, int) { return 0; }

// 0x0049F460: a word 0 (the engine texture's width / height slots).
uint16_t return_zero_word() { return 0; }

} // namespace stubs
