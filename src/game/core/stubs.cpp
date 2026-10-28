// Functions that do (almost) nothing. The linker folded identical functions into one, so each address below
// stands for many functions of the original source (cut debug/console script commands, empty virtual
// methods...), and some are reached through a 5-byte jump of their own. They're too short for the hook's
// jump, so they're registered source-only.

#include "game/core/stubs.h"

#include <cstdint>

#include "game/core/alloc.h"
#include "game/file/async.h"
#include "hook/hook.h"

namespace Stubs {

// 0x005B9760 (also through the jump at 0x005B6570): e.g. the script commands that are stubs returning true
bool ReturnTrue() { return true; }

// 0x005B9E50: ...returning false
bool ReturnFalse() { return false; }

// 0x005BA1D0: functions returning 0 / null
int ReturnZero() { return 0; }

// 0x005480A0: an empty cdecl function - the most called address in the game (528 calls). Compiled-out debug
// code: the callers still push its arguments and pop them after. In our source it appears under its uses'
// names, as empty inline functions: Mem::PushMemProfile / PopMemProfile, Mem::DebugTrackBlock,
// Script::PrintContents (more as their callers are rewritten). (The bytes
// after its RET, 0x005480A1-0x005480BE, are PartyMod's: a code cave its exe patch put in the padding, which
// loads its DLL (the name at 0x0064B198) - not the game's.)
void DoNothing() {}

// 0x005BAB90: functions that ignore their one (stack) argument
void __stdcall IgnoreArgument(int) {}

// 0x0049F4B0, 0x0049F470, 0x0049F450, 0x0049F440, 0x0049F430: methods returning false whatever their (1, 2, 4, 5, 7)
// stack arguments - the engine texture's empty slots, CArray's comparison (different arrays are never equal), ...
bool __stdcall ReturnFalse1(int) { return false; }
bool __stdcall ReturnFalse2(int, int) { return false; }
bool __stdcall ReturnFalse4(int, int, int, int) { return false; }
bool __stdcall ReturnFalse5(int, int, int, int, int) { return false; }
bool __stdcall ReturnFalse7(int, int, int, int, int, int, int) { return false; }

// 0x0049F4A0, 0x0049F4C0, 0x0049F480, 0x0049F490: ...returning 0 / null (1, 2, 3, 6 stack arguments) - the model's
// bone flag (slot 2), the gap checks' "0", ...
int __stdcall ReturnZero1(int) { return 0; }
int __stdcall ReturnZero2(int, int) { return 0; }
int __stdcall ReturnZero3(int, int, int) { return 0; }
int __stdcall ReturnZero6(int, int, int, int, int, int) { return 0; }

// 0x0049F460: a word 0 (the engine texture's width / height slots).
uint16_t ReturnZeroWord() { return 0; }

} // namespace Stubs

REPLACE_SOURCE_ONLY(0x005B9760, Stubs::ReturnTrue);         // 3 bytes
REPLACE_SOURCE_ONLY(0x005B6570, Stubs::ReturnTrue);         // a jump to 0x005B9760
REPLACE_SOURCE_ONLY(0x005B9E50, Stubs::ReturnFalse);        // 3 bytes
REPLACE_SOURCE_ONLY(0x004504F0, Stubs::ReturnFalse);        // a jump to 0x005B9E50
REPLACE_SOURCE_ONLY(0x005BA1D0, Stubs::ReturnZero);         // 3 bytes
REPLACE_SOURCE_ONLY(0x0047AE10, Stubs::ReturnZero);         // a jump to 0x005BA1D0
REPLACE_SOURCE_ONLY(0x005480A0, Stubs::DoNothing);          // 1 byte
REPLACE_SOURCE_ONLY(0x0048C1B0, Stubs::DoNothing);          // a jump to 0x005480A0
REPLACE_SOURCE_ONLY(0x005BAB90, Stubs::IgnoreArgument);     // 3 bytes
REPLACE(0x0049F4B0, Stubs::ReturnFalse1);
REPLACE(0x0049F470, Stubs::ReturnFalse2);
REPLACE(0x0049F450, Stubs::ReturnFalse4);
REPLACE(0x0049F440, Stubs::ReturnFalse5);
REPLACE(0x0049F430, Stubs::ReturnFalse7);
REPLACE(0x0049F4A0, Stubs::ReturnZero1);
REPLACE(0x0049F4C0, Stubs::ReturnZero2);
REPLACE(0x0049F480, Stubs::ReturnZero3);
REPLACE(0x0049F490, Stubs::ReturnZero6);
REPLACE_SOURCE_ONLY(0x0049F460, Stubs::ReturnZeroWord);     // 4 bytes
REPLACE_SOURCE_ONLY(0x005144C0, Stubs::IgnoreArgument);     // a jump to 0x005BAB90
REPLACE_SOURCE_ONLY(0x005B9DF0, File::AsyncInit);           // a jump to 0x005B6560
REPLACE_SOURCE_ONLY(0x005BA590, &Mem::Allocator::DestroyBody); // a jump to 0x005BA140
