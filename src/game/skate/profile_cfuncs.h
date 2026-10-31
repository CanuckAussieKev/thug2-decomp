#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) and helpers 0x005AA930-0x005AAEEF: a swap table of checksum pairs, the goal editor's
// nodes, preloading the meshes a list names, the skater profiles (pro or not, the current one, templates, temporary
// profiles and their appearance) and the career's goal counts for the menus. The CFunctions are cdecl (params,
// script) and return whether the script's `if` should be taken.
namespace commands {

// The swap table (0x007CEDE0): five slots - in use, key, value.
struct SwapSlot {
    bool used;
    uint8_t pad[3];
    uint32_t key;
    uint32_t value;
};
static_assert(sizeof(SwapSlot) == 12, "layout must match the original");
extern SwapSlot g_swap_table[5];

// 0x005AA930: `a` and `b` swapped in the table of five checksum pairs (0x007CEDE0: in use, key, value): each gets a
// slot (a free one if it has none - the table being full isn't checked, as in the original), then their values are
// exchanged; a slot left mapping its key to itself is freed.
void __cdecl swap_mapping(uint32_t a, uint32_t b);
// 0x005AAAA0 (the array's name in EAX): for each structure of the global array with allow_random, its mesh
// loaded (a model made, the mesh added to it, the model deleted - keeping the mesh in memory).
void preload_meshes(uint32_t array_name);
void preload_meshes_stub();

// 0x005AAA80: AddGoalEditorNodesToLevelNodeArray (on the bottom-up heap). True.
bool __cdecl cfunc_add_goal_editor_nodes_to_level_node_array(script::Struct* params, script::Script* script);
// 0x005AAB50: CurrentSkaterIsPro: whether the skate module's profile - looked up with the params pointer as the
// index, as in the original - is a pro.
bool __cdecl cfunc_current_skater_is_pro(script::Struct* params, script::Script* script);
// 0x005AAB80: GetGoalsCompleted <level>: the level's goal count as "%d/9" and "---" in the script's parameters.
bool __cdecl cfunc_get_goals_completed(script::Struct* params, script::Script* script);
// 0x005AAC10: GetNextLevelRequirements: the first level step of the global array 0x1D8F1A7E still out of reach -
// "goals for next level" / "medals for next level" with how many are missing - or "---".
bool __cdecl cfunc_get_next_level_requirements(script::Struct* params, script::Script* script);
// 0x005AAD70: SetCurrentSkaterProfile <index>. True.
bool __cdecl cfunc_set_current_skater_profile(script::Struct* params, script::Script* script);
// 0x005AADA0: AddSkaterProfile <profile>: the profile manager's add_template.
bool __cdecl cfunc_add_skater_profile(script::Struct* params, script::Script* script);
// 0x005AADC0: AddTemporaryProfile name=<id>: the profile manager's add_extra_profile.
bool __cdecl cfunc_add_temporary_profile(script::Struct* params, script::Script* script);
// 0x005AADF0: RememberTemporaryAppearance name=<id> appearance_structure=<s> | player=<n> [NoFaceTexture]: the
// temporary profile `name` given that appearance (or player n's profile's) - with NoFaceTexture only its
// structure. True.
bool __cdecl cfunc_remember_temporary_appearance(script::Struct* params, script::Script* script);

} // namespace commands
