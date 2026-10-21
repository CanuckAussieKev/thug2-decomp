#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for nodes and composite objects, 0x00449280-0x00449F80: node parameters and names,
// the COIM heap, making composite objects and finding free ids, screen positions, running scripts on components and
// objects, local-to-world transforms, destroying particle groups, orienting objects, event handlers on objects. All
// are cdecl (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_AppendNodeParams(Script::CStruct* params, Script::CScript* script);          // 0x00449280
bool __cdecl CFunc_InitCOIM(Script::CStruct* params, Script::CScript* script);                  // 0x004492E0
bool __cdecl CFunc_ResizeCOIMHeap(Script::CStruct* params, Script::CScript* script);            // 0x00449380
bool __cdecl CFunc_CreateCompositeObject(Script::CStruct* params, Script::CScript* script);     // 0x00449540
bool __cdecl CFunc_GetUniqueCompositeObjectID(Script::CStruct* params, Script::CScript* script); // 0x004495A0
bool __cdecl CFunc_GetObjScreenCoords(Script::CStruct* params, Script::CScript* script);        // 0x00449610
bool __cdecl CFunc_RunScriptOnComponentType(Script::CStruct* params, Script::CScript* script);  // 0x004496B0
bool __cdecl CFunc_TransformLocalToWorld(Script::CStruct* params, Script::CScript* script);     // 0x00449740
bool __cdecl CFunc_DestroyParticlesByGroupID(Script::CStruct* params, Script::CScript* script); // 0x004497D0
bool __cdecl CFunc_GetNode(Script::CStruct* params, Script::CScript* script);                   // 0x00449850
bool __cdecl CFunc_GetNodeName(Script::CStruct* params, Script::CScript* script);               // 0x00449890
bool __cdecl CFunc_GetTriggerNodeName(Script::CStruct* params, Script::CScript* script);        // 0x004498C0

} // namespace CFuncs
