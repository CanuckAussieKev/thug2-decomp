#pragma once

#include <cstdint>

#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/game_global.h"

// Parsing compiled script tokens (see qb.h) into values: parameters of structures, arrays, expressions and
// the arguments of calls. `args` is the calling script's parameters, which <name> and <...> refer to.
namespace Script {

class CScript;

// The script running now (0x006F3530), and the list of scripts (null: the first).
CScript* GetCurrentScript();
CScript* GetNextScript(CScript* script);

// Follow jumps and choose the branch of Random() blocks (weighted, no-repeat and permute) until a token
// that is neither.
const uint8_t* ResolveRandomJumps(const uint8_t* token);
// Past one value token (structures and arrays included).
const uint8_t* SkipValue(const uint8_t* token);
// The stored no-repeat / permute state of the random table `table` for the current script, or null.
CStoredRandom* FindStoredRandom(const uint8_t* table, uint32_t type, uint32_t count);
// A new stored random state (at most 100 are kept: the oldest goes).
CStoredRandom* NewStoredRandom();
// RandomRange(a, b) (`token` at the pair after the RandomRange token): an integer if both ends are whole
// numbers, else a float; `deterministic` uses the second random stream.
const uint8_t* ParseRandomRange(const uint8_t* token, CComponent* out, bool deterministic);
// If `component` names a global symbol of a value type, it becomes that value.
void ResolveNameComponent(CComponent* component);

// One value into `out`.
const uint8_t* ParseValue(const uint8_t* token, CStruct* args, CComponent* out);
// One parameter, named `name`, added to `dest`.
const uint8_t* AddParameter(const uint8_t* token, uint32_t name, CStruct* dest, CStruct* args);
// The arguments of a call (to the end of the line or a closing parenthesis) into `dest`.
const uint8_t* ParseArguments(CStruct* dest, const uint8_t* token, CStruct* args);
// A { ... } structure's parameters into `dest`.
const uint8_t* ParseStructure(CStruct* dest, const uint8_t* token, CStruct* args);
// A [ ... ] array (its type is the type of its elements; later elements' types win, but integers don't
// override floats).
const uint8_t* ParseArray(CArray* dest, const uint8_t* token, CStruct* args);
// A ( ... ) expression, evaluated into `result`.
const uint8_t* ParseExpression(const uint8_t* token, CStruct* args, CComponent* result);

// The entry points for the game's callers, which pass arguments in registers (also called by the self-tests).
void FindStoredRandomStub();
void SkipValueStub();
void ParseRandomRangeStub();
void AddParameterStub();

GAME_GLOBAL_EXTERN(float, g_parseNudge);
GAME_GLOBAL_EXTERN(CStoredRandom*, g_storedRandomsNewest);

} // namespace Script
