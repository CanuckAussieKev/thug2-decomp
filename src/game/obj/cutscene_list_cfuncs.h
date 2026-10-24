#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The cutscene destroy list (0x007CEDD8: a std::list of CFuncs::CutsceneDestroyEntry, movie_cfuncs.h): what a
// cutscene made and how to get rid of it - each entry's destroy script run with its parameters when the list is
// flushed. Its commands (0x005B5320-0x005B5770, 0x005B5BE0) and the std::list constructor (0x005B4650) the game's
// static lists share. The commands are cdecl (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

// A std::list<T*> as MSVC 7.1 lays it out: the head node (allocated, linked to itself when empty) and the size.
struct PointerList {
    struct Node {
        void* value;
        Node* next;
        Node* prev;
    };
    Node* m_head;
    uint32_t m_size;

    // 0x005B4650: the constructor (no elements: a new head node linked to itself, null if out of memory). Returns
    // this. (Used for 0x006F0DA8, the COIM instance list 0x006F0DB0 and the cutscene destroy list.)
    PointerList* Construct();
};
static_assert(sizeof(PointerList) == 8, "layout must match the original");

bool __cdecl CFunc_AddToCutsceneDestroyList(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_RemoveFromCutsceneDestroyList(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Cutscene_SpawnScript(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_FlushCutsceneDestroyList(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
