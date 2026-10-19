#pragma once

// The path manager (0x004694A0-0x00469AFE; the instance at 0x006F2264): numbers the level's paths (chains of
// linked nodes; each node gets its path's number as PathNum, 0x9C91C3CA) and keeps, for each path, the objects
// on it (smart pointers), plus one extra group. AllocatePathManMemory (a script command) sizes it.

#include <cstddef>
#include <cstdint>

#include "game/obj/smtptr.h"

namespace Script {
class CStruct;
class CScript;
}

namespace Obj {

class CObject;

// A fixed number of slots for objects (8 bytes; the slots are an array new[] of smart pointers).
class CObjectGroup {
public:
    // 0x004694A0 (not a Ghidra function: only the array helpers point at it): the destructor - the slots'
    // smart pointers let go (last first) and the array freed.
    void Destruct();
    // 0x004698C0: `size` empty slots (from the current heap).
    void Init(int size);
    // 0x00469540: every slot let go.
    void ReleaseAll();
    // 0x00469580: every slot holding `object` let go.
    void Remove(CObject* object);
    // 0x004695D0: `object` into the first empty slot (nothing if they're all taken).
    void Add(CObject* object);

    CSmtPtr<CObject>* m_slots;
    int m_size;
};
static_assert(sizeof(CObjectGroup) == 8, "layout must match the original");

class CPathManager {
public:
    // 0x004694E0: the instance, made on first use (empty).
    static CPathManager* Instance();
    // 0x00469620: `object` into the group of the path its node `node` is on; returns that group.
    CObjectGroup* AddToPath(CObject* object, uint32_t node);
    // 0x00469670: `object` into the extra group (the node's path is read, not used); returns the extra group.
    CObjectGroup* AddToExtraGroup(CObject* object, uint32_t node);
    // 0x004696C0: the groups emptied and every path numbered again (nodes of a path's class with links - or of
    // class 0x4C23A77E - that have no PathNum start one and give it to every node down their first links; a
    // chain that runs into a numbered node takes that number instead). Nothing if not allocated.
    void NumberPaths();
    // 0x00469820: the groups and the extra group freed.
    void Free();
    // 0x00469960: `groups` groups of `groupSize` slots and an extra group of `extraSize` (bottom-up heap).
    void Allocate(int groups, int groupSize, int extraSize);

    CObjectGroup* m_groups;     // +0x00 (new[]: the count before it)
    CObjectGroup* m_extra;      // +0x04
    int m_pathCount;            // +0x08
    int m_groupCount;           // +0x0C
};
static_assert(sizeof(CPathManager) == 0x10, "layout must match the original");

// 0x00469A80: the script command AllocatePathManMemory (0x0C230B04 groups, default 200; 0xF6A4081D slots each,
// default 20; 0x13AFFFF9 extra slots, default 200). True.
bool __cdecl CFunc_AllocatePathManMemory(Script::CStruct* params, Script::CScript* script);

} // namespace Obj
