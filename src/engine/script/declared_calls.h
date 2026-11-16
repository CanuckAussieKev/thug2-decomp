#pragma once

#include <cstdint>
#include <span>

#include "base/core/api.h"
#include "engine/script/nodes.h"

// NOT ORIGINAL - what a script does when a node's TriggerScript runs it, as the engine asks it: the calls the script
// makes to the declared functions (each with its arguments) and whether it keeps the editor camera out. Read by the gap
// list (game/skate/gap_checklist.cpp, through level_nodes::for_each_node_script_call), the level editor's trigger kinds
// (editor/level_editor_triggers*.cpp) and the editor camera (game/world/editor_camera.cpp) - never by reading script
// code themselves.
//
// A script's declaration comes from where the script does:
//   - a TypeScript script's is declared data: its file's .json, "triggers" (tools/qbjs/trigger_data.ts makes it from the
//     .qb the TypeScript is made from - docs/qb-ts.md "Trigger data"); the bridge registers it (set_source);
//   - a QB script's is read from its code by the same rules - the original's: level_nodes::for_each_script_call
//     (0x0045C950) per function, and the editor camera's walk (0x004E4F50). Reading a call's arguments evaluates them,
//     a Random's choice drawn: from streams of their own (maths::OwnStreams), never the game's - reading a script's
//     calls is no part of the simulation, so QB and TypeScript scripts leave the game's random numbers the same.
// No declared value depends on a random draw (the tool reports one that would); Shatter's and ShatterAndDie's calls are
// declared without their arguments (no reader wants them).
namespace declared_calls {

// The functions a declaration lists the calls of (tools/qbjs/trigger_data.ts DECLARED_FUNCTIONS - the same list):
// EndGap, StartGap, SK3_KillSkater, SK3_KillSkater_Water, SK3_KillSkater_Pungee, SK3_Killskater_Finish, KillSkater,
// TeleportSkaterToNode, trigger_jump_boost, Shatter, ShatterAndDie, RailAccelerate, ed_RailSpeed,
// LevelEditor_PlayCamera.
std::span<const uint32_t> functions();

// Every call script `script` makes to `function` (one of functions()), as reached from it with no arguments (the
// scripts it calls followed with theirs, SetEventHandler's scr= handlers, EndGap's gapscript= when `function` is EndGap,
// 5 levels deep): `callback` with each call's arguments, in order.
THUG2_API void for_each_call(uint32_t script, uint32_t function, level_nodes::ScriptCallCallback callback);

// Whether script `script` keeps the editor camera out: it is - or calls by name at a statement's start, at any depth -
// SK3_KillSkater, SK3_KillSkater_Finish, TeleportSkaterToNode or CAG_Boundary (EditorCamera itself never).
bool keeps_editor_camera_out(uint32_t script);

// The declarations of the scripts that aren't QB (the TypeScript bridge's).
class Source {
public:
    Source() = default;
    virtual ~Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    Source(Source&&) = delete;
    Source& operator=(Source&&) = delete;
    // Whether `script` is one of its scripts (its declaration - none is an empty one - is the answer, not its code).
    virtual bool owns(uint32_t script) = 0;
    virtual void for_each_call(uint32_t script, uint32_t function, level_nodes::ScriptCallCallback callback) = 0;
    virtual bool keeps_editor_camera_out(uint32_t script) = 0;
};
void set_source(Source* source);

} // namespace declared_calls
