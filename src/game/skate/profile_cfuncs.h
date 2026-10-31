#pragma once

#include <cstdint>

#include "hook/game_global.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) and helpers 0x005AA930-0x005AAEEF: a swap table of checksum pairs, the goal editor's
// nodes, preloading the meshes a list names, the skater profiles (pro or not, the current one, templates, temporary
// profiles and their appearance) and the career's goal counts for the menus. The CFunctions are cdecl (params,
// script) and return whether the script's `if` should be taken.
namespace CFuncs {

// The swap table (0x007CEDE0): five slots - in use, key, value.
struct SwapSlot {
    bool used;
    uint8_t pad[3];
    uint32_t key;
    uint32_t value;
};
static_assert(sizeof(SwapSlot) == 12, "layout must match the original");
GAME_GLOBAL_EXTERN(SwapSlot[5], g_swapTable);

// 0x005AA930: `a` and `b` swapped in the table of five checksum pairs (0x007CEDE0: in use, key, value): each gets a
// slot (a free one if it has none - the table being full isn't checked, as in the original), then their values are
// exchanged; a slot left mapping its key to itself is freed.
void __cdecl SwapMapping(uint32_t a, uint32_t b);
// 0x005AAAA0 (the array's name in EAX): for each structure of the global array with allow_random, its mesh
// loaded (a model made, the mesh added to it, the model deleted - keeping the mesh in memory).
void PreloadMeshes(uint32_t arrayName);
void PreloadMeshesStub();

// 0x005AAA80: AddGoalEditorNodesToLevelNodeArray (on the bottom-up heap). True.
bool __cdecl CFunc_AddGoalEditorNodesToLevelNodeArray(Script::CStruct* params, Script::CScript* script);
// 0x005AAB50: CurrentSkaterIsPro: whether the skate module's profile - looked up with the params pointer as the
// index, as in the original - is a pro.
bool __cdecl CFunc_CurrentSkaterIsPro(Script::CStruct* params, Script::CScript* script);
// 0x005AAB80: GetGoalsCompleted <level>: the level's goal count as "%d/9" and "---" in the script's parameters.
bool __cdecl CFunc_GetGoalsCompleted(Script::CStruct* params, Script::CScript* script);
// 0x005AAC10: GetNextLevelRequirements: the first level step of the global array 0x1D8F1A7E still out of reach -
// "goals for next level" / "medals for next level" with how many are missing - or "---".
bool __cdecl CFunc_GetNextLevelRequirements(Script::CStruct* params, Script::CScript* script);
// 0x005AAD70: SetCurrentSkaterProfile <index>. True.
bool __cdecl CFunc_SetCurrentSkaterProfile(Script::CStruct* params, Script::CScript* script);
// 0x005AADA0: AddSkaterProfile <profile>: the profile manager's AddTemplate.
bool __cdecl CFunc_AddSkaterProfile(Script::CStruct* params, Script::CScript* script);
// 0x005AADC0: AddTemporaryProfile name=<id>: the profile manager's AddExtraProfile.
bool __cdecl CFunc_AddTemporaryProfile(Script::CStruct* params, Script::CScript* script);
// 0x005AADF0: RememberTemporaryAppearance name=<id> appearance_structure=<s> | player=<n> [NoFaceTexture]: the
// temporary profile `name` given that appearance (or player n's profile's) - with NoFaceTexture only its
// structure. True.
bool __cdecl CFunc_RememberTemporaryAppearance(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
