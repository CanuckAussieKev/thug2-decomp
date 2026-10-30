#pragma once

#include <cstdint>
#include "hook/game_global.h"

namespace Script {
class CStruct;
}

// The goal editor's (create-a-goal's) small helpers: the objects a goal places (the S-K-A-T-E and C-O-M-B-O
// letters, the cone) and their models / names, the goal types' script structures, the trigger node names of an
// edited goal, and a goal's defaults. Goal types: 0x54166ACD (the SKATE letters), 0x4EC3CFB5 (the COMBO letters),
// 0x25904450 (the cone), and others without objects (0x0FC6F698, 0x61C5D092, 0x6FE44C6D, 0x7FE2238A, 0xD0DD047B,
// 0xEA7BA666, 0xEC414B76).
namespace Ed {

// 0x004E6600 (type in ECX, index in EDX): the model of the goal's object `index` (null without one).
const char* __fastcall GoalObjectModel(uint32_t type, int index);
// 0x004E66A0 (type in ECX, index in EDX): the name checksum of the goal's object `index` (0 without one).
uint32_t __fastcall GoalObjectName(uint32_t type, int index);
// 0x004E6730 (which in ECX, goal in EDX): the checksum of the edited goal's node TRG_EditedGoal<goal>_Pro (0),
// _Restart (1) or _Object_<which - 2>.
uint32_t __fastcall EditedGoalNodeName(int which, int goal);
// 0x004E67A0 (type in ECX): the goal type's script structure of one kind, or null.
Script::CStruct* __fastcall GoalTypeStructure(uint32_t type);
// 0x004E6890 (type in EAX): the goal type's script structure of another kind, or null (0xD0DD047B's also runs
// 0xFA54B475 once into it).
Script::CStruct* GoalTypeDefaults(uint32_t type);
// 0x004E69C0: 0x82.
int __cdecl GoalEditorConstant82();
// 0x004E69D0 (the goal in ECX): an edited goal's defaults (the SKATE letters; 100, 120, 3000, 360... and the
// thirteen entries cleared).
void __fastcall InitEditedGoal(uint8_t* goal);
// 0x004E6AE0: GoalEditorMarkerObject<index> passed to the script 0xDCF3A25E as 0xA1DC81F9.
void __stdcall RunMarkerObjectScript(int index);

GAME_GLOBAL_EXTERN(char[24], g_createdGoalName);
GAME_GLOBAL_EXTERN(char[104], g_goalNameBuffer);

} // namespace Ed
