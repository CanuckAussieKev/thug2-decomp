#pragma once

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for nodes and composite objects, 0x00449280-0x00449F80: node parameters and names,
// the COIM heap, making composite objects and finding free ids, screen positions, running scripts on components and
// objects, local-to-world transforms, destroying particle groups, orienting objects, event handlers on objects. All
// are cdecl (params, script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_AppendNodeParams(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00449280
bool __cdecl CFunc_InitCOIM(Scripts::CStruct* params, Scripts::CScript* script);                  // 0x004492E0
bool __cdecl CFunc_ResizeCOIMHeap(Scripts::CStruct* params, Scripts::CScript* script);            // 0x00449380
bool __cdecl CFunc_CreateCompositeObject(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00449540
bool __cdecl CFunc_GetUniqueCompositeObjectID(Scripts::CStruct* params, Scripts::CScript* script); // 0x004495A0
bool __cdecl CFunc_GetObjScreenCoords(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00449610
bool __cdecl CFunc_RunScriptOnComponentType(Scripts::CStruct* params, Scripts::CScript* script);  // 0x004496B0
bool __cdecl CFunc_TransformLocalToWorld(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00449740
bool __cdecl CFunc_DestroyParticlesByGroupID(Scripts::CStruct* params, Scripts::CScript* script); // 0x004497D0
bool __cdecl CFunc_GetNode(Scripts::CStruct* params, Scripts::CScript* script);                   // 0x00449850
bool __cdecl CFunc_GetNodeName(Scripts::CStruct* params, Scripts::CScript* script);               // 0x00449890
bool __cdecl CFunc_GetTriggerNodeName(Scripts::CStruct* params, Scripts::CScript* script);        // 0x004498C0
bool __cdecl CFunc_RunScriptOnObject(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00449A10
bool __cdecl CFunc_Orient(Scripts::CStruct* params, Scripts::CScript* script);                    // 0x00449AC0
bool __cdecl CFunc_SetEventHandlerOnObject(Scripts::CStruct* params, Scripts::CScript* script);   // 0x00449F80

} // namespace ScriptCommands
